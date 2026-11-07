/* Persisted present cap. Only 30 and 60 pace this build.
   Shipping present interval 2 is 30 Hz. The existing base-1.0.0 patch can lower
   that floor to interval 1, which is 60 Hz, and the controller still raises it
   when a frame does not fit. Interval is a whole number of 60 Hz ticks and is
   floored at 1, so 90 and 120 are not intervals the game can request.
   Remapping that tick onto a 90 or 120 Hz clock would change the game's own
   frame time and the frame-counted interface logic measured against a 60 Hz
   cap. The real-time step's 1/120 second floor only stops the game running
   fast; it is not a 90 or 120 mode. Those rates are unavailable. */
#pragma once
#include <stdint.h>

#define TOTK_FRAME_CAP_DEFAULT 30

static inline int totk_frame_cap_normalize(int hz) {
    return hz == 30 || hz == 60 ? hz : 0;
}

static inline int totk_frame_cap_unavailable(int hz) {
    return hz == 90 || hz == 120;
}

/* 30 or 60. Any other request, including 90 and 120, is not a pace rate. */
static inline int totk_frame_cap_applied(int requested) {
    return totk_frame_cap_normalize(requested);
}

static inline uint64_t totk_frame_cap_period_ns(int applied) {
    if (applied != 30 && applied != 60) return 0;
    return 1000000000ull / (uint64_t)applied;
}

/* A cap is a ceiling. A slower game interval stays slower. An uncapped or
   faster period is raised to 30 or 60 so the real-time frame step samples the
   spacing that actually ran. */
static inline uint64_t totk_frame_cap_pace(uint64_t period_ns, int applied) {
    uint64_t floor = totk_frame_cap_period_ns(applied);
    if (!floor) return period_ns;
    if (!period_ns || period_ns < floor) return floor;
    return period_ns;
}

int totk_frame_cap_get(void);    /* 0 when no cap is selected; never 90 or 120 */
void totk_frame_cap_set(int hz); /* accepts 30 or 60; 90 and 120 are refused */
static inline int totk_frame_cap_applied_now(void) {
    return totk_frame_cap_applied(totk_frame_cap_get());
}
