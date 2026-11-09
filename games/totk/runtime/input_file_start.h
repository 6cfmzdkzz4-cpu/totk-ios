#ifndef TOTK_INPUT_FILE_START_H
#define TOTK_INPUT_FILE_START_H
/* Start the command-file thread once. A null or empty path does not count as started.
   Returns 1 when this call starts the thread, 0 when there is nothing to do or it is already running,
   and -1 after fail() when the thread cannot be created. fail() may not return. */
int input_file_ensure_started(const char *path, void *(*worker)(void *), void (*fail)(const char *));
#endif
