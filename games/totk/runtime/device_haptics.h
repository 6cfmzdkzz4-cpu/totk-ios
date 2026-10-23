/* Pure policy for subtle local-device feedback; controller tuning stays upstream. */
#ifndef TOTK_DEVICE_HAPTICS_H
#define TOTK_DEVICE_HAPTICS_H
#include "dualsense_feedback.h"

enum { TOTK_DEVICE_HAPTICS_OFF, TOTK_DEVICE_HAPTICS_TOUCH, TOTK_DEVICE_HAPTICS_ALWAYS };
#define TOTK_DEVICE_HAPTICS_LEASE_NS UINT64_C(100000000)
#define TOTK_DEVICE_HAPTICS_RENEW_NS UINT64_C(60000000)

static inline int totk_device_haptics_eligible(int supported, int mode,
                                              int foreground, int controller) {
    return supported && foreground &&
        (mode == TOTK_DEVICE_HAPTICS_ALWAYS ||
         (mode == TOTK_DEVICE_HAPTICS_TOUCH && !controller));
}

/* The observer timestamp is a real presentation heartbeat, including title
   frames with no player. Native sample freshness and exact semantic gates are
   already enforced by the shared composer. No new gameplay events are inferred. */
static inline TOTKFeedbackOutput totk_device_haptics_output(
    TOTKFeedbackOutput left, TOTKFeedbackOutput right, uint64_t heartbeat_ns,
    uint64_t active_since_ns, uint64_t now_ns, float gain, int active, int paused) {
    TOTKFeedbackOutput out = {0};
    if (!active || paused || !heartbeat_ns || heartbeat_ns < active_since_ns ||
        heartbeat_ns > now_ns || now_ns-heartbeat_ns >= UINT64_C(200000000) ||
        !isfinite(gain) || gain <= 0) return out;
    if (!isfinite(left.intensity) || !isfinite(left.sharpness)) left = out;
    if (!isfinite(right.intensity) || !isfinite(right.sharpness)) right = out;
    TOTKFeedbackOutput source = left.intensity >= right.intensity ? left : right;
    /* Mono local actuator receives the stronger side once, never a stereo sum.
       Even the explicit strongest setting cannot exceed 0.22 intensity. */
    out.intensity = fminf(.22f, fmaxf(0, source.intensity) * fminf(.4f, gain));
    out.sharpness = .15f + .65f * totk_feedback_clampf(source.sharpness, 0, 1);
    out.fresh = 1;
    return out;
}
#endif
