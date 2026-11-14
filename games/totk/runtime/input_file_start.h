#ifndef TOTK_INPUT_FILE_START_H
#define TOTK_INPUT_FILE_START_H
#include <stdint.h>
#include <sys/types.h>
/* Start the command-file thread once. A null or empty path does not count as started.
   Returns 1 when this call starts the thread, 0 when there is nothing to do or it is already running,
   and -1 after fail() when the thread cannot be created. fail() may not return. */
int input_file_ensure_started(const char *path, void *(*worker)(void *), void (*fail)(const char *));

/* Follows the path. An append is read from the previous offset. A replaced directory entry
   (atomic rename) or a shorter file is read from the start. Unchanged polls deliver nothing.
   A line is delivered only when its newline has arrived. */
typedef struct input_file_reader {
    char *path;
    int fd;
    dev_t dev;
    ino_t ino;
    int open;
    long consumed;
    uint64_t hash;
    char partial[1200];
    size_t partial_len;
} input_file_reader;
void input_file_reader_init(input_file_reader *reader, const char *path);
void input_file_reader_close(input_file_reader *reader);
int input_file_reader_poll(input_file_reader *reader, void (*on_line)(const char *line, void *ctx), void *ctx, void (*fail)(const char *));
#endif
