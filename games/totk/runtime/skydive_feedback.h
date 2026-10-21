/* Pure-C handle-haptic policy for authoritative sustained skydive wind.
   The observer supplies fresh exact-state timing, continuous observed descent,
   and downward speed.  This policy never infers skydiving from airborne state,
   never emits adaptive-trigger output, and strictly max-composes with an
   already composed native/semantic handle value. */
#ifndef TOTK_SKYDIVE_FEEDBACK_H
#define TOTK_SKYDIVE_FEEDBACK_H

#include <math.h>
#include <stdint.h>

enum {
    TOTK_SKYDIVE_CONTEXT_LEASE_NS = 200000000,
    TOTK_SKYDIVE_MIN_DURATION_NS = 800000000,
    TOTK_SKYDIVE_FADE_DURATION_NS = 600000000,
    TOTK_SKYDIVE_GRAIN_NS = 24000000
};

/* Defensive engineering bound for corrupt or discontinuous observations, not
   a claim about the game's terminal dive speed.  Texture strength still
   saturates at 40 m/s through the unchanged speed-fade calculation below. */
#define TOTK_SKYDIVE_MAX_DOWN_SPEED_MPS 200.0f

typedef struct {
    uint64_t timestamp_ns;
    uint64_t started_ns;
    float downward_speed_mps;
    float descent_m;
    int valid;
    int paused;
    int skydiving;
} TOTKSkydiveFeedbackSample;

typedef struct {
    float intensity;
    float sharpness;
    int wind_active;
} TOTKSkydiveFeedbackOutput;

static inline float totk_skydive_clampf(float value, float low, float high) {
    return value < low ? low : value > high ? high : value;
}

static inline float totk_skydive_noise(uint64_t tick, uint64_t salt) {
    uint64_t value = tick + salt;
    value ^= value >> 30;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27;
    value *= UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31;
    return (float)(value >> 40) / 16777215.0f;
}

static inline float totk_skydive_smoothstep(float phase) {
    phase = totk_skydive_clampf(phase, 0.0f, 1.0f);
    return phase * phase * (3.0f - 2.0f * phase);
}

/* Compose one handle.  handle is zero for left and one for right, used only to
   decorrelate fine grain.  Base values are sanitized independently.  Invalid,
   stale, paused, brief, shallow, or slow samples preserve the base unchanged.
   A tie also preserves both base intensity and base sharpness. */
static inline TOTKSkydiveFeedbackOutput totk_skydive_feedback_compose(
    float base_intensity,
    float base_sharpness,
    TOTKSkydiveFeedbackSample sample,
    uint64_t now_ns,
    float gain,
    int active,
    unsigned handle) {
    TOTKSkydiveFeedbackOutput output = {0};
    if (isfinite(base_intensity))
        output.intensity = totk_skydive_clampf(base_intensity, 0.0f, 1.0f);
    if (isfinite(base_sharpness))
        output.sharpness = totk_skydive_clampf(base_sharpness, 0.0f, 1.0f);

    const int booleans_valid =
        (sample.valid == 0 || sample.valid == 1) &&
        (sample.paused == 0 || sample.paused == 1) &&
        (sample.skydiving == 0 || sample.skydiving == 1);
    if (active != 1 || handle > 1 || !isfinite(gain) || gain <= 0.0f ||
        !booleans_valid || sample.valid != 1 || sample.paused != 0 ||
        sample.skydiving != 1 || !isfinite(sample.downward_speed_mps) ||
        sample.downward_speed_mps < 0.0f ||
        sample.downward_speed_mps > TOTK_SKYDIVE_MAX_DOWN_SPEED_MPS ||
        !isfinite(sample.descent_m) || sample.descent_m < 0.0f ||
        sample.descent_m > 5000.0f || sample.timestamp_ns == 0 ||
        sample.timestamp_ns > now_ns ||
        now_ns - sample.timestamp_ns >= TOTK_SKYDIVE_CONTEXT_LEASE_NS ||
        sample.started_ns == 0 || sample.started_ns > sample.timestamp_ns)
        return output;

    const uint64_t age_ns = now_ns - sample.started_ns;
    if (age_ns < TOTK_SKYDIVE_MIN_DURATION_NS || sample.descent_m < 10.0f ||
        sample.downward_speed_mps < 8.0f)
        return output;

    const float time_fade =
        (float)(age_ns - TOTK_SKYDIVE_MIN_DURATION_NS) /
        (float)TOTK_SKYDIVE_FADE_DURATION_NS;
    const float drop_fade = (sample.descent_m - 10.0f) / 8.0f;
    const float speed_fade = (sample.downward_speed_mps - 8.0f) / 32.0f;
    const float limiting_fade =
        fminf(time_fade, fminf(drop_fade, speed_fade));
    const float onset = totk_skydive_smoothstep(limiting_fade);
    if (onset <= 0.0f)
        return output;

    gain = totk_skydive_clampf(gain, 0.0f, 2.0f);
    const uint64_t tick = now_ns / TOTK_SKYDIVE_GRAIN_NS;
    const uint64_t side_salt = handle == 0 ? UINT64_C(0x2c1b3c6d) :
                                             UINT64_C(0x9e3779b97f4a7c15);
    const float texture = totk_skydive_noise(tick, side_salt);
    const float edge =
        totk_skydive_noise(tick, side_salt ^ UINT64_C(0xd1b54a32d192ed03));
    const float speed_mix = totk_skydive_clampf(speed_fade, 0.0f, 1.0f);
    const float bed = 0.030f + 0.035f * sqrtf(speed_mix);
    const float wind_intensity = totk_skydive_clampf(
        onset * bed * (0.80f + 0.20f * texture) * gain, 0.0f, 1.0f);

    if (wind_intensity <= 0.0f)
        return output;
    output.wind_active = 1;
    if (wind_intensity > output.intensity) {
        output.intensity = wind_intensity;
        output.sharpness = 0.68f + 0.18f * edge;
    }
    return output;
}

#endif
