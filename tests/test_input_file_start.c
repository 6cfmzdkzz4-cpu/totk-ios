/* Late configuration must start the command thread, and a second start must not read the file twice. */
#define _GNU_SOURCE
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../games/totk/runtime/input_file_start.c"

static _Atomic int flags, reads;

static void fail(const char *message) {
    fprintf(stderr, "fail %s\n", message);
    exit(1);
}

static void *worker(void *arg) {
    FILE *f = fopen(arg, "r");
    if (!f) fail("open command file");
    char line[256];
    for (;;) {
        if (!fgets(line, sizeof line, f)) { clearerr(f); usleep(20000); continue; }
        atomic_fetch_add(&reads, 1);
        if (!strncmp(line, "flag mask_draw ", 15) && atoi(line + 15) == 1) atomic_fetch_add(&flags, 1);
        if (!strncmp(line, "stop", 4)) break;
    }
    fclose(f);
    return NULL;
}

static void wait_for(int value) {
    for (int i = 0; i < 100 && atomic_load(&flags) < value; i++) usleep(20000);
}

static char seen[16][160];
static int nseen;
static void collect(const char *line, void *ctx) { (void)ctx; if (nseen < 16) snprintf(seen[nseen], sizeof seen[0], "%s", line); nseen++; }
static void expect_poll(input_file_reader *reader, int count, const char *last) {
    int before = nseen;
    int got = input_file_reader_poll(reader, collect, NULL, fail);
    if (got != count || nseen != before + count) { fprintf(stderr, "poll %d lines, collected %d\n", got, nseen - before); exit(10); }
    if (count && last && !strstr(seen[nseen - 1], last)) { fprintf(stderr, "line %s\n", seen[nseen - 1]); exit(11); }
    if (input_file_reader_poll(reader, collect, NULL, fail) != 0) exit(12);
}
static void append_text(const char *path, const char *text) {
    FILE *f = fopen(path, "a"); if (!f) exit(1); fputs(text, f); fclose(f);
}
static void replace_text(const char *path, const char *text) {
    char tmp[512]; snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w"); if (!f) exit(1); fputs(text, f); fclose(f);
    if (rename(tmp, path) != 0) exit(1);
}
static void rewrite_text(const char *path, const char *text) {
    FILE *f = fopen(path, "w"); if (!f) exit(1); fputs(text, f); fclose(f);
}
static void check_follow(void) {
    char dir[] = "/tmp/totk-follow-XXXXXX";
    if (!mkdtemp(dir)) exit(1);
    char path[256]; snprintf(path, sizeof path, "%s/input.txt", dir);
    FILE *f = fopen(path, "w"); if (!f) exit(1); fclose(f);
    input_file_reader reader; input_file_reader_init(&reader, path);
    expect_poll(&reader, 0, NULL);
    append_text(path, "flag mask_draw 1\n");
    expect_poll(&reader, 1, "mask_draw 1");
    append_text(path, "flag mask_draw");
    expect_poll(&reader, 0, NULL);
    append_text(path, " 2\n");
    expect_poll(&reader, 1, "mask_draw 2");
    append_text(path, "zzz-partial");
    expect_poll(&reader, 0, NULL);
    replace_text(path, "flag mask_draw 3\n");
    expect_poll(&reader, 1, "mask_draw 3");
    if (strstr(seen[nseen - 1], "zzz-partial")) exit(13);
    rewrite_text(path, "flag mask_draw 4\n");
    expect_poll(&reader, 1, "mask_draw 4");
    rewrite_text(path, "flag mask_draw 8\nflag mask_draw 9\n");
    expect_poll(&reader, 2, "mask_draw 9");
    append_text(path, "flag mask_draw 5\n");
    expect_poll(&reader, 1, "mask_draw 5");
    replace_text(path, "flag mask_draw 6\nflag mask_draw 7\n");
    expect_poll(&reader, 2, "mask_draw 7");
    input_file_reader_close(&reader);
}

int main(void) {
    check_follow();
    if (input_file_ensure_started(NULL, worker, fail) != 0) return 1;
    if (input_file_ensure_started("", worker, fail) != 0) return 1;
    char dir[] = "/tmp/totk-input-XXXXXX";
    if (!mkdtemp(dir)) return 1;
    char path[256];
    snprintf(path, sizeof path, "%s/input.txt", dir);
    FILE *f = fopen(path, "w");
    if (!f) return 1;
    fclose(f);
    if (input_file_ensure_started(path, worker, fail) != 1) return 2;
    if (input_file_ensure_started(path, worker, fail) != 0) return 3;
    f = fopen(path, "a");
    if (!f) return 1;
    fputs("flag mask_draw 1\n", f);
    fclose(f);
    wait_for(1);
    if (atomic_load(&flags) != 1 || atomic_load(&reads) != 1) return 4;
    f = fopen(path, "a");
    if (!f) return 1;
    fputs("flag mask_draw 1\n", f);
    fclose(f);
    wait_for(2);
    if (atomic_load(&flags) != 2 || atomic_load(&reads) != 2) return 5;
    f = fopen(path, "a");
    if (!f) return 1;
    fputs("stop\n", f);
    fclose(f);
    printf("input file start checks passed\n");
    return 0;
}
