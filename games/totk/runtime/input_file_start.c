#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#if !defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 200809L
#undef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "input_file_start.h"
/* Start the diagnostic command thread once the path is known.
   The Mac child is exec'd with TOTK_INPUT_FILE already set, so a constructor can start it.
   iOS sets the variable later, in didFinishLaunching. A call before the path exists must not
   count as "already started", or the later call cannot recover. */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static pthread_mutex_t input_file_start_mu = PTHREAD_MUTEX_INITIALIZER;
static int input_file_started;

int input_file_ensure_started(const char *path, void *(*worker)(void *), void (*fail)(const char *)) {
    if (!path || !path[0]) return 0;
    pthread_mutex_lock(&input_file_start_mu);
    if (input_file_started) { pthread_mutex_unlock(&input_file_start_mu); return 0; }
    if (!worker) { pthread_mutex_unlock(&input_file_start_mu); if (fail) fail("diagnostic input thread"); return -1; }
    size_t path_len = strlen(path);
    char *owned = malloc(path_len + 1);
    if (!owned) { pthread_mutex_unlock(&input_file_start_mu); if (fail) fail("diagnostic input path"); return -1; }
    memcpy(owned, path, path_len + 1);
    pthread_t thread;
    if (pthread_create(&thread, NULL, worker, owned)) {
        free(owned);
        pthread_mutex_unlock(&input_file_start_mu);
        if (fail) fail("diagnostic input thread");
        return -1;
    }
    pthread_detach(thread);
    input_file_started = 1;
    pthread_mutex_unlock(&input_file_start_mu);
    fprintf(stderr, "[totk-input] command file %s\n", path);
    return 1;
}

void input_file_reader_init(input_file_reader *reader, const char *path) {
    size_t n = strlen(path);
    memset(reader, 0, sizeof *reader);
    reader->fd = -1;
    reader->path = malloc(n + 1);
    if (!reader->path) return;
    memcpy(reader->path, path, n + 1);
    reader->fd = -1;
}

void input_file_reader_close(input_file_reader *reader) {
    if (reader->fd >= 0) close(reader->fd);
    free(reader->path);
    memset(reader, 0, sizeof *reader);
    reader->fd = -1;
}

static const uint64_t READER_HASH_INIT = 14695981039346656037ull;
static uint64_t reader_hash(uint64_t hash, const char *bytes, size_t n) {
    for (size_t i = 0; i < n; i++) { hash ^= (unsigned char)bytes[i]; hash *= 1099511628211ull; }
    return hash;
}
static int reader_reopen(input_file_reader *reader, void (*fail)(const char *)) {
    int fd = open(reader->path, O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT) return 0;
        if (fail) fail("cannot open TOTK_INPUT_FILE");
        return -1;
    }
    struct stat opened;
    if (fstat(fd, &opened) != 0) { close(fd); if (fail) fail("diagnostic input stat"); return -1; }
    if (reader->fd >= 0) close(reader->fd);
    reader->fd = fd;
    reader->dev = opened.st_dev;
    reader->ino = opened.st_ino;
    reader->open = 1;
    reader->consumed = 0;
    reader->hash = READER_HASH_INIT;
    reader->partial_len = 0;
    return 1;
}
/* 1 when the bytes already consumed are still the prefix. pread sees a rewrite of this inode. */
static int reader_prefix_same(input_file_reader *reader) {
    if (!reader->consumed) return 1;
    uint64_t hash = READER_HASH_INIT;
    long done = 0;
    while (done < reader->consumed) {
        char buf[1024];
        size_t want = (size_t)(reader->consumed - done < 1024 ? reader->consumed - done : 1024);
        ssize_t n = pread(reader->fd, buf, want, done);
        if (n <= 0) return 0;
        hash = reader_hash(hash, buf, (size_t)n);
        done += (long)n;
    }
    return hash == reader->hash;
}
int input_file_reader_poll(input_file_reader *reader, void (*on_line)(const char *line, void *ctx), void *ctx, void (*fail)(const char *)) {
    struct stat st;
    if (!reader->path) return 0;
    if (stat(reader->path, &st) != 0) {
        if (errno == ENOENT) return 0;
        if (fail) fail("diagnostic input stat");
        return -1;
    }
    if (st.st_size > 16L * 1024L * 1024L) { if (fail) fail("diagnostic input too large"); return -1; }
    int replaced = reader->open && (reader->dev != st.st_dev || reader->ino != st.st_ino);
    if (!reader->open || replaced) {
        if (replaced) fprintf(stderr, "[totk-input] command file replaced\n");
        int opened = reader_reopen(reader, fail);
        if (opened <= 0) return opened;
    } else if (st.st_size < reader->consumed || !reader_prefix_same(reader)) {
        reader->consumed = 0;
        reader->hash = READER_HASH_INIT;
        reader->partial_len = 0;
    }
    if (reader->fd < 0 || st.st_size == reader->consumed) return 0;
    int delivered = 0;
    while (reader->consumed < st.st_size) {
        char buf[1024];
        size_t want = (size_t)(st.st_size - reader->consumed);
        if (want > sizeof buf) want = sizeof buf;
        ssize_t n = pread(reader->fd, buf, want, reader->consumed);
        if (n < 0) { if (fail) fail("diagnostic input read"); return -1; }
        if (n == 0) break;
        reader->consumed += (long)n;
        reader->hash = reader_hash(reader->hash, buf, (size_t)n);
        for (ssize_t i = 0; i < n; i++) {
            if (reader->partial_len + 1 >= sizeof reader->partial) { if (fail) fail("diagnostic input line too long"); return -1; }
            reader->partial[reader->partial_len++] = buf[i];
            if (buf[i] != '\n') continue;
            reader->partial[reader->partial_len] = 0;
            if (on_line) on_line(reader->partial, ctx);
            reader->partial_len = 0;
            delivered++;
        }
    }
    return delivered;
}
