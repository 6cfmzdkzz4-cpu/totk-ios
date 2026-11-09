/* Start the diagnostic command thread once the path is known.
   The Mac child is exec'd with TOTK_INPUT_FILE already set, so a constructor can start it.
   iOS sets the variable later, in didFinishLaunching. A call before the path exists must not
   count as "already started", or the later call cannot recover. */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
