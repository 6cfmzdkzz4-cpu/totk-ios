/* Pure-C policy for bounded native two-band HD rumble, observer-backed semantic
   controller feedback, and radial stick shaping.  Semantic effects require
   explicit authoritative context or event timestamps; this layer infers none. */
#ifndef TOTK_DUALSENSE_FEEDBACK_H
#define TOTK_DUALSENSE_FEEDBACK_H

#include <math.h>
#include <stdint.h>

typedef struct {
    float low_amplitude;
    float low_frequency;
    float high_amplitude;
    float high_frequency;
    uint64_t timestamp_ns;
} TOTKFeedbackSample;

typedef struct {
    float intensity;
    float sharpness;
    float trigger_amplitude;
    float trigger_frequency;
    int fresh;
} TOTKFeedbackOutput;

static inline float totk_feedback_clampf(float value, float low, float high) {
    return value < low ? low : value > high ? high : value;
}

/* Convert a genuine native low/high actuator sample into bounded low-level
   values.  A stale or malformed sample is silent.  The semantic composer below
   uses only its handle values and never mirrors native rumble to a trigger. */
static inline TOTKFeedbackOutput totk_feedback_output(TOTKFeedbackSample sample,
                                                       uint64_t now_ns,
                                                       float gain,
                                                       float trigger_gain,
                                                       int active) {
    TOTKFeedbackOutput output = {0};
    const uint64_t freshness_ns = UINT64_C(250000000);

    if (!active || sample.timestamp_ns == 0 || sample.timestamp_ns > now_ns ||
        now_ns - sample.timestamp_ns >= freshness_ns)
        return output;
    if (!isfinite(sample.low_amplitude) || !isfinite(sample.low_frequency) ||
        !isfinite(sample.high_amplitude) || !isfinite(sample.high_frequency) ||
        !isfinite(gain) || !isfinite(trigger_gain) ||
        sample.low_frequency <= 0.0f || sample.high_frequency <= 0.0f)
        return output;

    const float low_amplitude = totk_feedback_clampf(sample.low_amplitude, 0.0f, 1.0f);
    const float high_amplitude = totk_feedback_clampf(sample.high_amplitude, 0.0f, 1.0f);
    const float amplitude = hypotf(low_amplitude, high_amplitude);
    output.fresh = 1;
    if (amplitude < 0.02f)
        return output;

    gain = totk_feedback_clampf(gain, 0.0f, 2.0f);
    trigger_gain = totk_feedback_clampf(trigger_gain, 0.0f, 2.0f);
    output.intensity = totk_feedback_clampf(amplitude * gain, 0.0f, 1.0f);

    const double weight = (double)low_amplitude + (double)high_amplitude;
    double frequency = ((double)low_amplitude * (double)sample.low_frequency +
                        (double)high_amplitude * (double)sample.high_frequency) / weight;
    if (frequency < 40.0)
        frequency = 40.0;
    else if (frequency > 1250.0)
        frequency = 1250.0;
    output.sharpness = (float)(log(frequency / 40.0) / log(1250.0 / 40.0));
    output.sharpness = totk_feedback_clampf(output.sharpness, 0.0f, 1.0f);
    output.trigger_amplitude = totk_feedback_clampf(amplitude * trigger_gain, 0.0f, 1.0f);
    output.trigger_frequency = output.sharpness;
    return output;
}

/* Authoritative semantic feedback supplied by a separate observer.  These
   values describe observed game state and events; controller buttons must not
   be used to synthesize them.  Context is leased for 200 ms.  speed_mps is the
   observer's finite, nonnegative movement speed, not a material classification.
   npc_interaction_allowed authorizes only an exact observer-verified NPCTalk
   entry and may remain true during its Controller/Rumble AllPause.  A melee
   swing timestamp and kind identify a verified player-authored cue and can
   only shape native rumble sampled at or after that cue.  bow_drawn reports
   the observer-verified active bow-use state.  bow_ready separately reports a
   live equipped bow that can be drawn, allowing physical trigger travel to
   supply progressive resistance before and throughout the action.  Neither is
   inferred from controller input or elapsed time.  Event timestamps are actual
   occurrence times and monotonic per event kind. */
typedef struct {
    uint64_t timestamp_ns;
    int valid;
    int paused;
    int npc_interaction_allowed;
    int shield_ready;
    int shield_raised;
    int shield_surfing;
    float speed_mps;
    int bow_drawn;
    int bow_ready;
} TOTKSemanticContext;

typedef struct {
    uint64_t npc_interaction_timestamp_ns;
    uint64_t landing_timestamp_ns;
    uint64_t shield_block_timestamp_ns;
    uint64_t melee_swing_timestamp_ns;
    int melee_kind;
} TOTKSemanticEvents;

typedef enum {
    TOTK_MELEE_UNKNOWN = 0,
    TOTK_MELEE_BLUNT = 1,
    TOTK_MELEE_SHARP = 2
} TOTKMeleeKind;

typedef enum {
    TOTK_TRIGGER_OFF = 0,
    TOTK_TRIGGER_FEEDBACK = 1,
    TOTK_TRIGGER_VIBRATION = 2,
    TOTK_TRIGGER_SLOPE = 3
} TOTKTriggerMode;

/* All values are normalized to [0, 1].  FEEDBACK uses start_position and
   start_strength.  VIBRATION uses start_position, amplitude, and frequency.
   SLOPE uses both positions and both strengths.  Unused fields are zero. */
typedef struct {
    TOTKTriggerMode mode;
    float start_position;
    float end_position;
    float start_strength;
    float end_strength;
    float amplitude;
    float frequency;
} TOTKAdaptiveTriggerOutput;

typedef struct {
    float handle_intensity;
    float handle_sharpness;
    TOTKAdaptiveTriggerOutput left_trigger;
    TOTKAdaptiveTriggerOutput right_trigger;
    int context_fresh;
} TOTKSemanticFeedbackOutput;

/* Caller-owned, zero-initialized state.  It prevents a timestamp repeated on
   multiple frames from retriggering and rate-limits noisy observer events. */
typedef struct {
    uint64_t seen_npc_interaction_ns;
    uint64_t active_npc_interaction_ns;
    uint64_t last_npc_interaction_ns;
    uint64_t seen_landing_ns;
    uint64_t active_landing_ns;
    uint64_t last_landing_ns;
    uint64_t seen_shield_block_ns;
    uint64_t active_shield_block_ns;
    uint64_t last_shield_block_ns;
} TOTKSemanticFeedbackState;

enum {
    TOTK_SEMANTIC_CONTEXT_LEASE_NS = 200000000,
    TOTK_SEMANTIC_NPC_DURATION_NS = 90000000,
    TOTK_SEMANTIC_NPC_COOLDOWN_NS = 250000000,
    TOTK_SEMANTIC_LANDING_DURATION_NS = 110000000,
    TOTK_SEMANTIC_LANDING_COOLDOWN_NS = 180000000,
    TOTK_SEMANTIC_BLOCK_DURATION_NS = 85000000,
    TOTK_SEMANTIC_BLOCK_COOLDOWN_NS = 120000000,
    TOTK_SEMANTIC_MELEE_LEASE_NS = 350000000,
    TOTK_SEMANTIC_SURF_TICK_NS = 12000000
};

static inline void totk_semantic_feedback_state_reset(TOTKSemanticFeedbackState *state) {
    if (state)
        *state = (TOTKSemanticFeedbackState){0};
}

static inline void totk_semantic_observe_event(uint64_t timestamp_ns,
                                                uint64_t now_ns,
                                                uint64_t duration_ns,
                                                uint64_t cooldown_ns,
                                                int accept,
                                                uint64_t *seen_ns,
                                                uint64_t *active_ns,
                                                uint64_t *last_ns) {
    if (timestamp_ns == 0 || timestamp_ns <= *seen_ns)
        return;
    *seen_ns = timestamp_ns;
    if (!accept || timestamp_ns > now_ns || now_ns - timestamp_ns >= duration_ns)
        return;
    if (*last_ns != 0 && (timestamp_ns <= *last_ns || timestamp_ns - *last_ns < cooldown_ns))
        return;
    *last_ns = timestamp_ns;
    *active_ns = timestamp_ns;
}

static inline float totk_semantic_decay(uint64_t event_ns, uint64_t now_ns,
                                        uint64_t duration_ns) {
    if (event_ns == 0 || event_ns > now_ns || now_ns - event_ns >= duration_ns)
        return 0.0f;
    const double phase = (double)(now_ns - event_ns) / (double)duration_ns;
    const double remaining = 1.0 - phase;
    return (float)(remaining * remaining);
}

static inline float totk_semantic_noise(uint64_t tick, uint64_t salt) {
    uint64_t value = tick + salt;
    value ^= value >> 30;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27;
    value *= UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31;
    return (float)(value >> 40) / 16777215.0f;
}

static inline void totk_semantic_add_handle(float amplitude, float sharpness,
                                             float *power, float *sharpness_sum,
                                             float *weight) {
    if (amplitude <= 0.0f)
        return;
    *power += amplitude * amplitude;
    *sharpness_sum += amplitude * totk_feedback_clampf(sharpness, 0.0f, 1.0f);
    *weight += amplitude;
}

/* Compose native handle rumble with observer-backed semantic effects.  Keep one
   state object per composed handle stream.  Native rumble never selects a
   trigger mode here.  haptic_gain controls handles only;
   trigger_gain controls adaptive triggers only.  A block event temporarily
   overrides the light left-trigger posture resistance with a short kick.  An
   authoritative bow-ready context independently adds a gentle progressive
   right-trigger slope.  active=0 or
   invalid gains return zero and clear active pulses.  Invalid, future, or stale
   context clears semantic effects.  Paused valid context silences native and
   gameplay effects except an explicitly allowed NPCTalk entry pulse. */
static inline TOTKSemanticFeedbackOutput totk_semantic_feedback_output(
    TOTKFeedbackSample native_sample,
    TOTKSemanticContext context,
    TOTKSemanticEvents events,
    uint64_t now_ns,
    float haptic_gain,
    float trigger_gain,
    int active,
    TOTKSemanticFeedbackState *state) {
    TOTKSemanticFeedbackOutput output = {0};
    if (!state)
        return output;

    const int gains_valid = isfinite(haptic_gain) && isfinite(trigger_gain);
    const int context_values_valid =
        (context.valid == 0 || context.valid == 1) &&
        (context.paused == 0 || context.paused == 1) &&
        (context.npc_interaction_allowed == 0 || context.npc_interaction_allowed == 1) &&
        (context.shield_ready == 0 || context.shield_ready == 1) &&
        (context.shield_raised == 0 || context.shield_raised == 1) &&
        (context.shield_surfing == 0 || context.shield_surfing == 1) &&
        (context.bow_drawn == 0 || context.bow_drawn == 1) &&
        (context.bow_ready == 0 || context.bow_ready == 1) &&
        isfinite(context.speed_mps) && context.speed_mps >= 0.0f;
    const int context_current = context_values_valid && context.valid &&
                                context.timestamp_ns > 0 && context.timestamp_ns <= now_ns &&
                                now_ns - context.timestamp_ns < TOTK_SEMANTIC_CONTEXT_LEASE_NS;
    const int context_fresh = context_current && !context.paused;
    const int accept_npc = active && gains_valid && context_current &&
                           context.npc_interaction_allowed;
    const int accept_gameplay = active && gains_valid && context_fresh;
    totk_semantic_observe_event(events.npc_interaction_timestamp_ns, now_ns,
                                TOTK_SEMANTIC_NPC_DURATION_NS,
                                TOTK_SEMANTIC_NPC_COOLDOWN_NS, accept_npc,
                                &state->seen_npc_interaction_ns,
                                &state->active_npc_interaction_ns,
                                &state->last_npc_interaction_ns);
    totk_semantic_observe_event(events.landing_timestamp_ns, now_ns,
                                TOTK_SEMANTIC_LANDING_DURATION_NS,
                                TOTK_SEMANTIC_LANDING_COOLDOWN_NS, accept_gameplay,
                                &state->seen_landing_ns,
                                &state->active_landing_ns,
                                &state->last_landing_ns);
    totk_semantic_observe_event(events.shield_block_timestamp_ns, now_ns,
                                TOTK_SEMANTIC_BLOCK_DURATION_NS,
                                TOTK_SEMANTIC_BLOCK_COOLDOWN_NS, accept_gameplay,
                                &state->seen_shield_block_ns,
                                &state->active_shield_block_ns,
                                &state->last_shield_block_ns);
    if (!active || !gains_valid) {
        state->active_npc_interaction_ns = 0;
        state->active_landing_ns = 0;
        state->active_shield_block_ns = 0;
        return output;
    }
    if (!context_current || !context.npc_interaction_allowed)
        state->active_npc_interaction_ns = 0;
    if (!context_fresh) {
        state->active_landing_ns = 0;
        state->active_shield_block_ns = 0;
    }

    haptic_gain = totk_feedback_clampf(haptic_gain, 0.0f, 2.0f);
    trigger_gain = totk_feedback_clampf(trigger_gain, 0.0f, 2.0f);
    output.context_fresh = context_fresh;

    const TOTKFeedbackOutput native =
        totk_feedback_output(native_sample, now_ns, 1.0f, 0.0f, 1);
    float handle_power = 0.0f, sharpness_sum = 0.0f, handle_weight = 0.0f;
    float native_intensity = native.intensity;
    float native_sharpness = native.sharpness;
    const int melee_kind_valid = events.melee_kind == TOTK_MELEE_BLUNT ||
                                 events.melee_kind == TOTK_MELEE_SHARP;
    const int melee_current = context_fresh && melee_kind_valid &&
                              events.melee_swing_timestamp_ns > 0 &&
                              events.melee_swing_timestamp_ns <= now_ns &&
                              now_ns - events.melee_swing_timestamp_ns <
                                  TOTK_SEMANTIC_MELEE_LEASE_NS &&
                              native.fresh && native_sample.timestamp_ns >=
                                  events.melee_swing_timestamp_ns;
    if (melee_current && native_intensity > 0.0f) {
        native_intensity = totk_feedback_clampf(native_intensity * 1.15f, 0.0f, 1.0f);
        if (events.melee_kind == TOTK_MELEE_SHARP)
            native_sharpness = totk_feedback_clampf(
                native_sharpness + 0.18f * (1.0f - native_sharpness), 0.0f, 1.0f);
    }
    if (!(context_current && context.paused))
        totk_semantic_add_handle(native_intensity, native_sharpness,
                                 &handle_power, &sharpness_sum, &handle_weight);

    const float npc_envelope =
        totk_semantic_decay(state->active_npc_interaction_ns, now_ns,
                            TOTK_SEMANTIC_NPC_DURATION_NS);
    const float landing_envelope =
        totk_semantic_decay(state->active_landing_ns, now_ns,
                            TOTK_SEMANTIC_LANDING_DURATION_NS);
    totk_semantic_add_handle(0.09f * npc_envelope, 0.45f,
                             &handle_power, &sharpness_sum, &handle_weight);
    totk_semantic_add_handle(0.13f * landing_envelope, 0.20f,
                             &handle_power, &sharpness_sum, &handle_weight);

    if (output.context_fresh && context.shield_surfing) {
        const float movement =
            totk_feedback_clampf((context.speed_mps - 0.35f) / 11.65f, 0.0f, 1.0f);
        if (movement > 0.0f) {
            const uint64_t tick = now_ns / TOTK_SEMANTIC_SURF_TICK_NS;
            const float texture = totk_semantic_noise(tick, UINT64_C(0x51ed2705));
            const float grain = totk_semantic_noise(tick, UINT64_C(0x9e3779b97f4a7c15));
            totk_semantic_add_handle(sqrtf(movement) * (0.05f + 0.07f * texture),
                                     0.55f + 0.30f * grain,
                                     &handle_power, &sharpness_sum, &handle_weight);
        }
    }

    if (handle_weight > 0.0f && haptic_gain > 0.0f) {
        output.handle_intensity =
            totk_feedback_clampf(sqrtf(handle_power) * haptic_gain, 0.0f, 1.0f);
        output.handle_sharpness =
            totk_feedback_clampf(sharpness_sum / handle_weight, 0.0f, 1.0f);
    }

    const float block_envelope =
        totk_semantic_decay(state->active_shield_block_ns, now_ns,
                            TOTK_SEMANTIC_BLOCK_DURATION_NS);
    if (trigger_gain > 0.0f && block_envelope > 0.0f) {
        output.left_trigger.mode = TOTK_TRIGGER_VIBRATION;
        output.left_trigger.start_position = 0.18f;
        output.left_trigger.amplitude =
            totk_feedback_clampf(0.30f * block_envelope * trigger_gain, 0.0f, 1.0f);
        output.left_trigger.frequency = 0.20f;
    } else if (trigger_gain > 0.0f && output.context_fresh && context.shield_raised) {
        output.left_trigger.mode = TOTK_TRIGGER_FEEDBACK;
        output.left_trigger.start_position = 0.18f;
        output.left_trigger.start_strength =
            totk_feedback_clampf(0.12f * trigger_gain, 0.0f, 1.0f);
    } else if (trigger_gain > 0.0f && output.context_fresh && context.shield_ready) {
        output.left_trigger.mode = TOTK_TRIGGER_SLOPE;
        output.left_trigger.start_position = 0.24f;
        output.left_trigger.end_position = 0.82f;
        output.left_trigger.start_strength =
            totk_feedback_clampf(0.025f * trigger_gain, 0.0f, 1.0f);
        output.left_trigger.end_strength =
            totk_feedback_clampf(0.08f * trigger_gain, 0.0f, 1.0f);
    }
    if (trigger_gain > 0.0f && output.context_fresh && context.bow_ready) {
        output.right_trigger.mode = TOTK_TRIGGER_SLOPE;
        output.right_trigger.start_position = 0.0f;
        output.right_trigger.end_position = 1.0f;
        output.right_trigger.start_strength =
            totk_feedback_clampf(0.025f * trigger_gain, 0.0f, 1.0f);
        output.right_trigger.end_strength =
            totk_feedback_clampf(0.22f * trigger_gain, 0.0f, 1.0f);
    }
    return output;
}

static inline int32_t totk_stick_component(float value) {
    value = totk_feedback_clampf(value, -32767.0f, 32767.0f);
    return (int32_t)(value < 0.0f ? value - 0.5f : value + 0.5f);
}

/* Apply a radial dead zone and optional power response while preserving the
   stick's direction.  The circular output range has radius 32767. */
static inline void totk_stick_axes(float x, float y, float deadzone, float curve,
                                   int32_t *out_x, int32_t *out_y) {
    if (out_x)
        *out_x = 0;
    if (out_y)
        *out_y = 0;
    if (!out_x || !out_y || !isfinite(x) || !isfinite(y) ||
        !isfinite(deadzone) || !isfinite(curve) ||
        deadzone < 0.0f || deadzone >= 1.0f || curve <= 0.0f)
        return;

    const double magnitude = hypot((double)x, (double)y);
    if (!isfinite(magnitude) || magnitude <= (double)deadzone)
        return;

    const double bounded_magnitude = magnitude > 1.0 ? 1.0 : magnitude;
    double response = (bounded_magnitude - (double)deadzone) / (1.0 - (double)deadzone);
    if (response <= 0.0)
        return;
    if (curve != 1.0f)
        response = pow(response, (double)curve);
    if (!isfinite(response) || response <= 0.0)
        return;

    const double scale = response * 32767.0 / magnitude;
    *out_x = totk_stick_component((float)((double)x * scale));
    *out_y = totk_stick_component((float)((double)y * scale));
}

#endif
