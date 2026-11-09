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

int main(void) {
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
