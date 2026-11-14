#include "totk_frame_cap.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

static _Atomic int g_requested;

void totk_frame_cap_set(int hz) {
    if (totk_frame_cap_unavailable(hz)) {
        fprintf(stderr, "[totk] frame cap %d is unavailable and was not selected. Pace at 30 or 60; 90 and 120 are not game intervals in this build.\n", hz);
        return;
    }
    int normalized = totk_frame_cap_normalize(hz);
    if (!normalized) normalized = TOTK_FRAME_CAP_DEFAULT;
    int previous = atomic_exchange_explicit(&g_requested, normalized, memory_order_acq_rel);
    if (previous == normalized) return;
    fprintf(stderr, "[totk] frame cap %d Hz\n", normalized);
}

int totk_frame_cap_get(void) {
    int value = atomic_load_explicit(&g_requested, memory_order_acquire);
    if (value) return value;
    const char *env = getenv("TOTK_FPS_CAP");
    if (!env || !*env) return 0;
    int hz = atoi(env);
    if (totk_frame_cap_unavailable(hz)) {
        static int reported;
        if (!reported) {
            reported = 1;
            fprintf(stderr, "[totk] TOTK_FPS_CAP=%d is unavailable. Pacing at %d Hz.\n", hz, TOTK_FRAME_CAP_DEFAULT);
        }
        return TOTK_FRAME_CAP_DEFAULT;
    }
    return totk_frame_cap_normalize(hz);
}

/* Strong override of the graphics default. 30 disables frame extrapolation.
   An unset cap keeps the historical 60 default used by graphics fixtures. */
int dm_fps_setting(void) {
    int applied = totk_frame_cap_applied_now();
    return applied == 30 ? 30 : 60;
}
