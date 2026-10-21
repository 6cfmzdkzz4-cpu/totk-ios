/* Pure-C policy for bounded, exact-cue handle-haptic envelopes.
   The caller supplies only descriptors selected from its trusted cue table.
   In particular, allow_paused may be set only for an exact UI cue whose
   identity is sufficient authority to play while gameplay context is absent
   or paused.  Ordinary gameplay cues require a fresh, valid, unpaused gate.

   Keep one state object per handle.  Events are one-shot and identified by a
   monotonically increasing sequence within a foreground epoch.  Envelopes are
   anchored to the event timestamp, copied into bounded state on admission,
   and max-composed with an already composed handle value.  This policy does
   not create adaptive-trigger output or infer game state. */
#ifndef TOTK_HAPTIC_CUES_H
#define TOTK_HAPTIC_CUES_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>

enum {
    TOTK_HAPTIC_CUE_MAX_POINTS = 512,
    TOTK_HAPTIC_CUE_MAX_DURATION_MS = 10000,
    TOTK_HAPTIC_CUE_EVENT_FRESH_NS = 100000000,
    TOTK_HAPTIC_CUE_GAMEPLAY_GATE_LEASE_NS = 200000000
};

typedef enum {
    TOTK_HAPTIC_CUE_GAMEPLAY = 0,
    TOTK_HAPTIC_CUE_TRUSTED_UI_ALLOW_PAUSED = 1
} TOTKHapticCuePausePolicy;

typedef struct {
    uint32_t offset_ms;
    float intensity;
    float sharpness;
} TOTKHapticCuePoint;

/* points are linearly interpolated.  The first offset must be zero, all later
   offsets must increase, and the final offset is the finite envelope length.
   Finite intensity and sharpness values are clamped to [0, 1] when admitted. */
typedef struct {
    uint32_t cue_id;
    const TOTKHapticCuePoint *points;
    size_t point_count;
    int allow_paused; /* TOTKHapticCuePausePolicy */
} TOTKHapticCueDescriptor;

typedef struct {
    uint32_t cue_id;
    uint32_t epoch;
    uint64_t sequence;
    uint64_t timestamp_ns;
} TOTKHapticCueEvent;

/* A gameplay gate is authoritative observer state.  An allow_paused descriptor
   deliberately does not require this gate: exact title/start UI cues can occur
   before a Player exists.  Foreground, epoch, and event freshness still apply. */
typedef struct {
    uint32_t epoch;
    uint64_t timestamp_ns;
    int valid;
    int paused;
} TOTKHapticCueGate;

typedef struct {
    float intensity;
    float sharpness;
    int cue_active;
} TOTKHapticCueOutput;

typedef struct {
    uint32_t epoch;
    uint32_t seen_cue_id;
    uint64_t seen_sequence;
    uint64_t seen_timestamp_ns;
    int has_seen;

    uint32_t active_cue_id;
    uint64_t active_sequence;
    uint64_t active_timestamp_ns;
    size_t active_point_count;
    int active_allow_paused;
    int active;
    TOTKHapticCuePoint active_points[TOTK_HAPTIC_CUE_MAX_POINTS];
} TOTKHapticCueState;

static inline float totk_haptic_cue_clampf(float value, float low, float high) {
    return value < low ? low : value > high ? high : value;
}

static inline void totk_haptic_cue_state_reset(TOTKHapticCueState *state) {
    if (state)
        *state = (TOTKHapticCueState){0};
}

static inline void totk_haptic_cue_clear_active(TOTKHapticCueState *state) {
    state->active_cue_id = 0;
    state->active_sequence = 0;
    state->active_timestamp_ns = 0;
    state->active_point_count = 0;
    state->active_allow_paused = 0;
    state->active = 0;
}

static inline int totk_haptic_cue_gameplay_gate_current(TOTKHapticCueGate gate,
                                                         uint32_t active_epoch,
                                                         uint64_t now_ns) {
    return gate.valid == 1 && gate.paused == 0 && gate.epoch == active_epoch &&
           gate.timestamp_ns > 0 && gate.timestamp_ns <= now_ns &&
           now_ns - gate.timestamp_ns < TOTK_HAPTIC_CUE_GAMEPLAY_GATE_LEASE_NS;
}

static inline int totk_haptic_cue_descriptor_valid(
    const TOTKHapticCueDescriptor *descriptor, uint32_t cue_id) {
    if (!descriptor || descriptor->cue_id == 0 || descriptor->cue_id != cue_id ||
        !descriptor->points || descriptor->point_count < 2 ||
        descriptor->point_count > TOTK_HAPTIC_CUE_MAX_POINTS ||
        (descriptor->allow_paused != 0 && descriptor->allow_paused != 1))
        return 0;

    if (descriptor->points[0].offset_ms != 0)
        return 0;
    uint32_t previous = 0;
    for (size_t i = 0; i < descriptor->point_count; ++i) {
        const TOTKHapticCuePoint point = descriptor->points[i];
        if (!isfinite(point.intensity) || !isfinite(point.sharpness))
            return 0;
        if (i > 0 && point.offset_ms <= previous)
            return 0;
        previous = point.offset_ms;
    }
    return previous > 0 && previous <= TOTK_HAPTIC_CUE_MAX_DURATION_MS;
}

/* Copy and shape an exact sound envelope for its actual playback pitch.
   Ordinary cues change timing only.  A verified charge cue additionally uses
   the bounded three-stage accent derived from its authored 1/1.05/1.12 pitch.
   The source remains untouched.  On any malformed input, rounded timestamp
   collision, or duration overflow, return zero with a zero descriptor. */
static inline int totk_haptic_cue_shape_sound(
    TOTKHapticCueDescriptor *out,
    const TOTKHapticCueDescriptor *source,
    float pitch,
    int charge,
    TOTKHapticCuePoint *scratch,
    size_t capacity) {
    if (!out)
        return 0;

    const TOTKHapticCueDescriptor input =
        source ? *source : (TOTKHapticCueDescriptor){0};
    *out = (TOTKHapticCueDescriptor){0};
    if (!source || !totk_haptic_cue_descriptor_valid(&input, input.cue_id) ||
        !isfinite(pitch) || pitch < 0.75f || pitch > 4.0f ||
        (charge != 0 && charge != 1) || !scratch || capacity < input.point_count)
        return 0;

    const float rise =
        charge ? totk_haptic_cue_clampf(pitch - 1.0f, 0.0f, 0.2f) : 0.0f;
    for (size_t i = 0; i < input.point_count; ++i) {
        scratch[i] = input.points[i];
        scratch[i].offset_ms =
            (uint32_t)lroundf((float)input.points[i].offset_ms / pitch);
        if (charge) {
            scratch[i].intensity =
                fminf(1.0f, input.points[i].intensity * (1.0f + 3.5f * rise));
            scratch[i].sharpness =
                fminf(1.0f, input.points[i].sharpness + 0.05f + 1.8f * rise);
        }
    }

    TOTKHapticCueDescriptor shaped = input;
    shaped.points = scratch;
    if (!totk_haptic_cue_descriptor_valid(&shaped, shaped.cue_id))
        return 0;
    *out = shaped;
    return 1;
}

static inline void totk_haptic_cue_admit(const TOTKHapticCueDescriptor *descriptor,
                                          TOTKHapticCueEvent event,
                                          TOTKHapticCueState *state) {
    state->active_cue_id = descriptor->cue_id;
    state->active_sequence = event.sequence;
    state->active_timestamp_ns = event.timestamp_ns;
    state->active_point_count = descriptor->point_count;
    state->active_allow_paused = descriptor->allow_paused;
    state->active = 1;
    for (size_t i = 0; i < descriptor->point_count; ++i) {
        state->active_points[i].offset_ms = descriptor->points[i].offset_ms;
        state->active_points[i].intensity =
            totk_haptic_cue_clampf(descriptor->points[i].intensity, 0.0f, 1.0f);
        state->active_points[i].sharpness =
            totk_haptic_cue_clampf(descriptor->points[i].sharpness, 0.0f, 1.0f);
    }
}

static inline TOTKHapticCueOutput totk_haptic_cue_base(float intensity,
                                                        float sharpness) {
    TOTKHapticCueOutput output = {0};
    if (isfinite(intensity))
        output.intensity = totk_haptic_cue_clampf(intensity, 0.0f, 1.0f);
    if (isfinite(sharpness))
        output.sharpness = totk_haptic_cue_clampf(sharpness, 0.0f, 1.0f);
    return output;
}

static inline TOTKHapticCuePoint totk_haptic_cue_sample(
    const TOTKHapticCueState *state, uint64_t age_ns) {
    TOTKHapticCuePoint sampled = {0};
    if (!state->active || state->active_point_count < 2)
        return sampled;

    const TOTKHapticCuePoint *points = state->active_points;
    for (size_t i = 1; i < state->active_point_count; ++i) {
        const uint64_t right_ns = (uint64_t)points[i].offset_ms * UINT64_C(1000000);
        if (age_ns <= right_ns) {
            const uint64_t left_ns =
                (uint64_t)points[i - 1].offset_ms * UINT64_C(1000000);
            const double phase = (double)(age_ns - left_ns) /
                                 (double)(right_ns - left_ns);
            sampled.offset_ms = (uint32_t)(age_ns / UINT64_C(1000000));
            sampled.intensity =
                points[i - 1].intensity +
                (float)(phase * (double)(points[i].intensity - points[i - 1].intensity));
            sampled.sharpness =
                points[i - 1].sharpness +
                (float)(phase * (double)(points[i].sharpness - points[i - 1].sharpness));
            return sampled;
        }
    }
    return points[state->active_point_count - 1];
}

/* Compose one handle.  Call this on the serial haptic queue with one state per
   handle.  A newly observed sequence is recorded before any admission check,
   so a future, stale, gated, or malformed event cannot replay later.  Once an
   event is admitted, descriptor may be NULL on subsequent calls because the
   sanitized envelope is held in state.  A cue replaces the prior handle value
   only when its intensity is strictly greater; an equal value preserves both
   the prior intensity and sharpness. */
static inline TOTKHapticCueOutput totk_haptic_cue_compose(
    float base_intensity,
    float base_sharpness,
    const TOTKHapticCueDescriptor *descriptor,
    TOTKHapticCueEvent event,
    TOTKHapticCueGate gate,
    uint32_t active_epoch,
    uint64_t now_ns,
    float gain,
    int foreground,
    TOTKHapticCueState *state) {
    TOTKHapticCueOutput output = totk_haptic_cue_base(base_intensity, base_sharpness);
    if (!state)
        return output;

    if (state->epoch != active_epoch) {
        totk_haptic_cue_state_reset(state);
        state->epoch = active_epoch;
    }

    const int candidate = active_epoch != 0 && event.epoch == active_epoch &&
                          event.sequence != 0;
    const int new_event = candidate &&
                          (!state->has_seen || event.sequence > state->seen_sequence);
    if (new_event) {
        state->has_seen = 1;
        state->seen_cue_id = event.cue_id;
        state->seen_sequence = event.sequence;
        state->seen_timestamp_ns = event.timestamp_ns;
        totk_haptic_cue_clear_active(state);
    }

    if (active_epoch == 0 || foreground != 1 || !isfinite(gain) || gain <= 0.0f) {
        totk_haptic_cue_clear_active(state);
        return output;
    }
    gain = totk_haptic_cue_clampf(gain, 0.0f, 2.0f);

    if (new_event) {
        if (event.cue_id == 0 || event.timestamp_ns == 0 || event.timestamp_ns > now_ns ||
            now_ns - event.timestamp_ns >= TOTK_HAPTIC_CUE_EVENT_FRESH_NS ||
            !totk_haptic_cue_descriptor_valid(descriptor, event.cue_id))
            return output;
        if (!descriptor->allow_paused &&
            !totk_haptic_cue_gameplay_gate_current(gate, active_epoch, now_ns))
            return output;
        totk_haptic_cue_admit(descriptor, event, state);
    }

    if (!state->active)
        return output;
    if (!state->active_allow_paused &&
        !totk_haptic_cue_gameplay_gate_current(gate, active_epoch, now_ns)) {
        totk_haptic_cue_clear_active(state);
        return output;
    }
    if (state->active_timestamp_ns == 0 || state->active_timestamp_ns > now_ns) {
        totk_haptic_cue_clear_active(state);
        return output;
    }

    const uint64_t age_ns = now_ns - state->active_timestamp_ns;
    const uint64_t duration_ns =
        (uint64_t)state->active_points[state->active_point_count - 1].offset_ms *
        UINT64_C(1000000);
    if (age_ns >= duration_ns) {
        totk_haptic_cue_clear_active(state);
        return output;
    }

    const TOTKHapticCuePoint cue = totk_haptic_cue_sample(state, age_ns);
    const float cue_intensity =
        totk_haptic_cue_clampf(cue.intensity * gain, 0.0f, 1.0f);
    output.cue_active = 1;
    if (cue_intensity > output.intensity) {
        output.intensity = cue_intensity;
        output.sharpness = cue.sharpness;
    }
    return output;
}

#endif
