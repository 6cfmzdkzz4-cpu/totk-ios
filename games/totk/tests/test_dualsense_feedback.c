/* Deterministic tests for the pure-C DualSense feedback and stick policy. */
#include "../runtime/dualsense_feedback.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int failures;

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static int nearf(float actual, float expected, float tolerance) {
    return fabsf(actual - expected) <= tolerance;
}

static int silent(TOTKFeedbackOutput output) {
    return output.intensity == 0.0f && output.sharpness == 0.0f &&
           output.trigger_amplitude == 0.0f && output.trigger_frequency == 0.0f;
}

static TOTKFeedbackSample sample(float low_amplitude, float low_frequency,
                                 float high_amplitude, float high_frequency,
                                 uint64_t timestamp_ns) {
    TOTKFeedbackSample value = {
        low_amplitude, low_frequency, high_amplitude, high_frequency, timestamp_ns};
    return value;
}

static void test_feedback_freshness(void) {
    const uint64_t now = UINT64_C(900000000);
    const TOTKFeedbackSample value = sample(0.3f, 160.0f, 0.4f, 320.0f, now);

    TOTKFeedbackOutput inactive = totk_feedback_output(value, now, 1.0f, 1.0f, 0);
    expect(!inactive.fresh && silent(inactive), "inactive feedback returns a zero command");
    TOTKFeedbackSample no_timestamp = value;
    no_timestamp.timestamp_ns = 0;
    expect(!totk_feedback_output(no_timestamp, now, 1.0f, 1.0f, 1).fresh,
           "a zero timestamp is not fresh");
    TOTKFeedbackSample future = value;
    future.timestamp_ns = now + 1;
    TOTKFeedbackOutput future_output = totk_feedback_output(future, now, 1.0f, 1.0f, 1);
    expect(!future_output.fresh && silent(future_output),
           "a future timestamp returns zero without unsigned underflow");
    TOTKFeedbackSample boundary = value;
    boundary.timestamp_ns = now - UINT64_C(250000000);
    TOTKFeedbackOutput stale = totk_feedback_output(boundary, now, 1.0f, 1.0f, 1);
    expect(!stale.fresh && silent(stale), "the stale 250 ms boundary returns zero");
    boundary.timestamp_ns++;
    TOTKFeedbackOutput output = totk_feedback_output(boundary, now, 1.0f, 1.0f, 1);
    expect(output.fresh && output.intensity > 0.0f,
           "a sample one nanosecond inside the freshness window is accepted");
}

static void test_feedback_invalid_values(void) {
    const uint64_t now = UINT64_C(1000000000);
    TOTKFeedbackSample values[] = {
        sample(NAN, 160.0f, 0.2f, 320.0f, now),
        sample(0.2f, NAN, 0.2f, 320.0f, now),
        sample(0.2f, 160.0f, INFINITY, 320.0f, now),
        sample(0.2f, 160.0f, 0.2f, -INFINITY, now),
        sample(0.2f, 0.0f, 0.2f, 320.0f, now),
        sample(0.2f, 160.0f, 0.2f, -1.0f, now),
    };
    for (size_t i = 0; i < sizeof values / sizeof *values; i++) {
        TOTKFeedbackOutput output = totk_feedback_output(values[i], now, 1.0f, 1.0f, 1);
        expect(!output.fresh && silent(output), "malformed native feedback fails silent");
    }
    TOTKFeedbackSample valid = sample(0.2f, 160.0f, 0.2f, 320.0f, now);
    TOTKFeedbackOutput bad_gain = totk_feedback_output(valid, now, NAN, 1.0f, 1);
    TOTKFeedbackOutput bad_trigger_gain = totk_feedback_output(valid, now, 1.0f, INFINITY, 1);
    expect(!bad_gain.fresh && silent(bad_gain), "a nonfinite haptic gain fails silent");
    expect(!bad_trigger_gain.fresh && silent(bad_trigger_gain),
           "a nonfinite trigger gain fails silent");
}

static void test_feedback_mix_and_bounds(void) {
    const uint64_t now = UINT64_C(1000000000);
    TOTKFeedbackOutput low = totk_feedback_output(
        sample(0.5f, 40.0f, 0.0f, 1250.0f, now), now, 1.0f, 1.0f, 1);
    TOTKFeedbackOutput high = totk_feedback_output(
        sample(0.0f, 40.0f, 0.5f, 1250.0f, now), now, 1.0f, 1.0f, 1);
    expect(low.fresh && nearf(low.intensity, 0.5f, 0.00001f) && low.sharpness == 0.0f,
           "a low-only native band maps to low sharpness");
    expect(high.fresh && nearf(high.intensity, 0.5f, 0.00001f) && high.sharpness == 1.0f,
           "a high-only native band maps to high sharpness");

    const float middle_frequency = sqrtf(40.0f * 1250.0f);
    TOTKFeedbackOutput middle = totk_feedback_output(
        sample(0.5f, middle_frequency, 0.0f, 320.0f, now), now, 1.0f, 1.0f, 1);
    expect(nearf(middle.sharpness, 0.5f, 0.00001f),
           "log-frequency midpoint maps to half sharpness");

    TOTKFeedbackOutput both = totk_feedback_output(
        sample(1.0f, 40.0f, 1.0f, 1250.0f, now), now, 1.0f, 2.0f, 1);
    const float weighted_sharpness = logf(645.0f / 40.0f) / logf(1250.0f / 40.0f);
    expect(both.fresh && both.intensity == 1.0f && both.trigger_amplitude == 1.0f,
           "maximum native bands and trigger gain stay bounded");
    expect(nearf(both.sharpness, weighted_sharpness, 0.00001f) &&
               nearf(both.trigger_frequency, both.sharpness, 0.000001f),
           "amplitude-weighted native frequency drives sharpness and trigger frequency");

    TOTKFeedbackOutput clamped = totk_feedback_output(
        sample(-4.0f, 10.0f, 4.0f, 5000.0f, now), now, 99.0f, 99.0f, 1);
    expect(clamped.fresh && clamped.intensity == 1.0f && clamped.sharpness == 1.0f &&
               clamped.trigger_amplitude == 1.0f,
           "native amplitudes, frequencies, and finite gains are bounded");
    TOTKFeedbackOutput gain_capped = totk_feedback_output(
        sample(0.1f, 160.0f, 0.0f, 320.0f, now), now, 99.0f, 99.0f, 1);
    expect(nearf(gain_capped.intensity, 0.2f, 0.00001f) &&
               nearf(gain_capped.trigger_amplitude, 0.2f, 0.00001f),
           "finite haptic and trigger gains clamp to two");

    TOTKFeedbackSample side_sample = sample(0.4f, 120.0f, 0.7f, 900.0f, now);
    TOTKFeedbackOutput left_side = totk_feedback_output(side_sample, now, 0.8f, 0.6f, 1);
    TOTKFeedbackOutput right_side = totk_feedback_output(side_sample, now, 0.8f, 0.6f, 1);
    expect(left_side.fresh == right_side.fresh &&
               nearf(left_side.intensity, right_side.intensity, 0.000001f) &&
               nearf(left_side.sharpness, right_side.sharpness, 0.000001f) &&
               nearf(left_side.trigger_amplitude, right_side.trigger_amplitude, 0.000001f) &&
               nearf(left_side.trigger_frequency, right_side.trigger_frequency, 0.000001f),
           "the same native sample has side-independent output");
}

static void test_feedback_silence(void) {
    const uint64_t now = UINT64_C(1000000000);
    TOTKFeedbackOutput zero = totk_feedback_output(
        sample(0.0f, 160.0f, 0.0f, 320.0f, now), now, 1.0f, 1.0f, 1);
    TOTKFeedbackOutput small = totk_feedback_output(
        sample(0.019f, 160.0f, 0.0f, 320.0f, now), now, 1.0f, 2.0f, 1);
    TOTKFeedbackOutput threshold = totk_feedback_output(
        sample(0.02f, 160.0f, 0.0f, 320.0f, now), now, 1.0f, 1.0f, 1);
    TOTKFeedbackOutput haptics_muted = totk_feedback_output(
        sample(0.3f, 160.0f, 0.4f, 320.0f, now), now, -1.0f, 0.5f, 1);
    TOTKFeedbackOutput trigger_muted = totk_feedback_output(
        sample(0.3f, 160.0f, 0.4f, 320.0f, now), now, 0.5f, -1.0f, 1);
    TOTKFeedbackOutput both_muted = totk_feedback_output(
        sample(0.3f, 160.0f, 0.4f, 320.0f, now), now, 0.0f, 0.0f, 1);
    expect(zero.fresh && silent(zero), "zero native amplitude is a fresh silent command");
    expect(small.fresh && silent(small), "native amplitude below 0.02 is silent");
    expect(threshold.fresh && threshold.intensity > 0.0f,
           "the exact 0.02 native amplitude threshold remains audible");
    expect(haptics_muted.fresh && haptics_muted.intensity == 0.0f &&
               nearf(haptics_muted.trigger_amplitude, 0.25f, 0.00001f) &&
               haptics_muted.sharpness > 0.0f &&
               nearf(haptics_muted.trigger_frequency, haptics_muted.sharpness, 0.000001f),
           "zero haptic gain leaves independently scaled trigger vibration");
    expect(trigger_muted.fresh && nearf(trigger_muted.intensity, 0.25f, 0.00001f) &&
               trigger_muted.trigger_amplitude == 0.0f,
           "zero trigger gain leaves independently scaled handle haptics");
    expect(both_muted.fresh && both_muted.intensity == 0.0f &&
               both_muted.trigger_amplitude == 0.0f,
           "zero gains independently mute both output amplitudes");
}

static int trigger_off(TOTKAdaptiveTriggerOutput trigger) {
    return trigger.mode == TOTK_TRIGGER_OFF && trigger.start_position == 0.0f &&
           trigger.end_position == 0.0f && trigger.start_strength == 0.0f &&
           trigger.end_strength == 0.0f && trigger.amplitude == 0.0f &&
           trigger.frequency == 0.0f;
}

static int semantic_quiet(TOTKSemanticFeedbackOutput output) {
    return output.handle_intensity == 0.0f && output.handle_sharpness == 0.0f &&
           trigger_off(output.left_trigger) && trigger_off(output.right_trigger);
}

static TOTKSemanticContext semantic_context(uint64_t timestamp_ns) {
    TOTKSemanticContext context = {0};
    context.timestamp_ns = timestamp_ns;
    context.valid = 1;
    return context;
}

static void test_semantic_native_handles_only(void) {
    const uint64_t now = UINT64_C(1000000000);
    TOTKSemanticFeedbackState state = {0};
    const TOTKFeedbackSample native = sample(0.3f, 160.0f, 0.4f, 320.0f, now);
    const TOTKSemanticContext no_context = {0};
    const TOTKSemanticEvents no_events = {0};
    TOTKSemanticFeedbackOutput output = totk_semantic_feedback_output(
        native, no_context, no_events, now, 1.0f, 2.0f, 1, &state);
    expect(nearf(output.handle_intensity, 0.5f, 0.00001f) &&
               output.handle_sharpness > 0.0f,
           "native HD rumble still contributes to handle haptics");
    expect(trigger_off(output.left_trigger) && trigger_off(output.right_trigger),
           "native HD rumble never creates generic semantic trigger buzz");

    output = totk_semantic_feedback_output(
        native, no_context, no_events, now, 0.0f, 2.0f, 1, &state);
    expect(output.handle_intensity == 0.0f && output.handle_sharpness == 0.0f &&
               trigger_off(output.left_trigger),
           "handle gain independently mutes native rumble without inventing a trigger effect");
}

static void test_semantic_shield_posture(void) {
    const uint64_t now = UINT64_C(2000000000);
    TOTKSemanticFeedbackState state = {0};
    const TOTKFeedbackSample native = sample(0.3f, 160.0f, 0.4f, 320.0f, now);
    const TOTKSemanticEvents no_events = {0};
    TOTKSemanticContext context = semantic_context(now);
    context.shield_ready = 1;

    TOTKSemanticFeedbackOutput ready = totk_semantic_feedback_output(
        native, context, no_events, now, 1.0f, 1.0f, 1, &state);
    expect(ready.context_fresh && ready.left_trigger.mode == TOTK_TRIGGER_SLOPE &&
               nearf(ready.left_trigger.start_position, 0.24f, 0.000001f) &&
               nearf(ready.left_trigger.end_position, 0.82f, 0.000001f) &&
               ready.left_trigger.start_strength > 0.0f &&
               ready.left_trigger.end_strength > ready.left_trigger.start_strength,
           "authoritative shield-ready posture uses a gentle left-trigger slope");
    expect(trigger_off(ready.right_trigger), "shield posture never activates the right trigger");

    context.shield_raised = 1;
    TOTKSemanticFeedbackOutput raised = totk_semantic_feedback_output(
        native, context, no_events, now, 1.0f, 1.0f, 1, &state);
    expect(raised.left_trigger.mode == TOTK_TRIGGER_FEEDBACK &&
               nearf(raised.left_trigger.start_position, 0.18f, 0.000001f) &&
               nearf(raised.left_trigger.start_strength, 0.12f, 0.000001f),
           "authoritative raised-shield posture uses light left-trigger resistance");

    TOTKSemanticFeedbackOutput trigger_muted = totk_semantic_feedback_output(
        native, context, no_events, now, 1.0f, 0.0f, 1, &state);
    expect(trigger_muted.handle_intensity > 0.0f && trigger_off(trigger_muted.left_trigger),
           "trigger gain independently mutes shield resistance but preserves handles");
    TOTKSemanticFeedbackOutput handles_muted = totk_semantic_feedback_output(
        native, context, no_events, now, 0.0f, 1.0f, 1, &state);
    expect(handles_muted.handle_intensity == 0.0f &&
               handles_muted.left_trigger.mode == TOTK_TRIGGER_FEEDBACK,
           "handle gain independently mutes handles but preserves shield resistance");

    TOTKSemanticFeedbackOutput stale = totk_semantic_feedback_output(
        native, context, no_events, now + TOTK_SEMANTIC_CONTEXT_LEASE_NS,
        1.0f, 1.0f, 1, &state);
    expect(!stale.context_fresh && trigger_off(stale.left_trigger),
           "the exact shield-context lease boundary is quiet");
    context.timestamp_ns = now + 1;
    TOTKSemanticFeedbackOutput future = totk_semantic_feedback_output(
        native, context, no_events, now, 1.0f, 1.0f, 1, &state);
    expect(!future.context_fresh && trigger_off(future.left_trigger),
           "future shield context is rejected");
    context.timestamp_ns = now;
    context.shield_ready = 2;
    TOTKSemanticFeedbackOutput malformed = totk_semantic_feedback_output(
        native, context, no_events, now, 1.0f, 1.0f, 1, &state);
    expect(!malformed.context_fresh && trigger_off(malformed.left_trigger),
           "non-boolean observer context fails closed");
    context.shield_ready = 1;
    context.paused = 1;
    TOTKSemanticFeedbackOutput paused = totk_semantic_feedback_output(
        native, context, no_events, now, 1.0f, 1.0f, 1, &state);
    expect(!paused.context_fresh && trigger_off(paused.left_trigger),
           "paused observer context is quiet");
    context.paused = 0;
    context.valid = 0;
    TOTKSemanticFeedbackOutput invalid = totk_semantic_feedback_output(
        native, context, no_events, now, 1.0f, 1.0f, 1, &state);
    expect(!invalid.context_fresh && trigger_off(invalid.left_trigger),
           "observer context explicitly marked invalid is quiet");
}

static void test_semantic_bow_trigger(void) {
    const uint64_t now = UINT64_C(2500000000);
    const TOTKFeedbackSample native = sample(0.3f, 160.0f, 0.4f, 320.0f, now);
    const TOTKSemanticEvents no_events = {0};
    TOTKSemanticFeedbackState state = {0};
    TOTKSemanticContext context = semantic_context(now);
    TOTKSemanticFeedbackOutput baseline = totk_semantic_feedback_output(
        native, context, no_events, now, 1.0f, 0.55f, 1, &state);

    context.bow_ready = 1;
    TOTKSemanticFeedbackOutput bow = totk_semantic_feedback_output(
        native, context, no_events, now, 1.0f, 0.55f, 1, &state);
    expect(trigger_off(bow.left_trigger) &&
               bow.right_trigger.mode == TOTK_TRIGGER_SLOPE &&
               nearf(bow.right_trigger.start_position, 0.0f, 0.000001f) &&
               nearf(bow.right_trigger.end_position, 1.0f, 0.000001f) &&
               nearf(bow.right_trigger.start_strength, 0.01375f, 0.000001f) &&
               nearf(bow.right_trigger.end_strength, 0.121f, 0.000001f),
           "verified ready bow arms a light progressive right-trigger curve before drawing");
    expect(nearf(bow.handle_intensity, baseline.handle_intensity, 0.000001f) &&
               nearf(bow.handle_sharpness, baseline.handle_sharpness, 0.000001f),
           "bow resistance does not alter native handle feedback");

    context.bow_drawn = 1;
    TOTKSemanticFeedbackOutput active_draw = totk_semantic_feedback_output(
        native, context, no_events, now, 1.0f, 0.55f, 1, &state);
    context.bow_drawn = 0;
    TOTKSemanticFeedbackOutput released = totk_semantic_feedback_output(
        native, context, no_events, now, 1.0f, 0.55f, 1, &state);
    expect(active_draw.right_trigger.mode == TOTK_TRIGGER_SLOPE &&
               released.right_trigger.mode == TOTK_TRIGGER_SLOPE &&
               active_draw.right_trigger.start_position ==
                   released.right_trigger.start_position &&
               active_draw.right_trigger.end_position ==
                   released.right_trigger.end_position &&
               active_draw.right_trigger.start_strength ==
                   released.right_trigger.start_strength &&
               active_draw.right_trigger.end_strength ==
                   released.right_trigger.end_strength,
           "active draw and release retain one continuous physical resistance slope");

    TOTKSemanticFeedbackOutput unity = totk_semantic_feedback_output(
        native, context, no_events, now, 1.0f, 1.0f, 1, &state);
    TOTKSemanticFeedbackOutput doubled = totk_semantic_feedback_output(
        native, context, no_events, now, 1.0f, 2.0f, 1, &state);
    TOTKSemanticFeedbackOutput muted = totk_semantic_feedback_output(
        native, context, no_events, now, 1.0f, 0.0f, 1, &state);
    expect(nearf(unity.right_trigger.start_strength, 0.025f, 0.000001f) &&
               nearf(unity.right_trigger.end_strength, 0.22f, 0.000001f) &&
               nearf(doubled.right_trigger.start_strength, 0.05f, 0.000001f) &&
               nearf(doubled.right_trigger.end_strength, 0.44f, 0.000001f) &&
               trigger_off(muted.right_trigger) &&
               nearf(muted.handle_intensity, baseline.handle_intensity, 0.000001f),
           "bow slope follows trigger gain and trigger mute without changing handles");

    context.shield_ready = 1;
    TOTKSemanticFeedbackOutput ready = totk_semantic_feedback_output(
        native, context, no_events, now, 1.0f, 1.0f, 1, &state);
    expect(ready.left_trigger.mode == TOTK_TRIGGER_SLOPE &&
               nearf(ready.left_trigger.start_position, 0.24f, 0.000001f) &&
               ready.right_trigger.mode == TOTK_TRIGGER_SLOPE &&
               nearf(ready.right_trigger.start_position, 0.0f, 0.000001f) &&
               nearf(ready.handle_intensity, baseline.handle_intensity, 0.000001f),
           "shield-ready L2 and bow-ready R2 slopes compose independently");

    context.shield_raised = 1;
    TOTKSemanticFeedbackOutput raised = totk_semantic_feedback_output(
        native, context, no_events, now, 1.0f, 1.0f, 1, &state);
    expect(raised.left_trigger.mode == TOTK_TRIGGER_FEEDBACK &&
               nearf(raised.left_trigger.start_strength, 0.12f, 0.000001f) &&
               raised.right_trigger.mode == TOTK_TRIGGER_SLOPE &&
               nearf(raised.handle_intensity, baseline.handle_intensity, 0.000001f),
           "raised-shield L2 resistance leaves bow-ready R2 resistance independent");

    TOTKSemanticFeedbackState block_state = {0};
    const TOTKSemanticEvents block = {0, 0, now, 0, TOTK_MELEE_UNKNOWN};
    TOTKSemanticFeedbackOutput blocked = totk_semantic_feedback_output(
        native, context, block, now, 1.0f, 1.0f, 1, &block_state);
    expect(blocked.left_trigger.mode == TOTK_TRIGGER_VIBRATION &&
               nearf(blocked.left_trigger.amplitude, 0.30f, 0.000001f) &&
               nearf(blocked.left_trigger.frequency, 0.20f, 0.000001f) &&
               blocked.right_trigger.mode == TOTK_TRIGGER_SLOPE &&
               nearf(blocked.handle_intensity, baseline.handle_intensity, 0.000001f),
           "left blocking thump does not replace bow-ready R2 resistance or alter handles");

    context = semantic_context(now);
    context.bow_ready = 1;
    context.bow_drawn = 1;
    TOTKSemanticFeedbackOutput stale = totk_semantic_feedback_output(
        native, context, no_events, now + TOTK_SEMANTIC_CONTEXT_LEASE_NS,
        1.0f, 1.0f, 1, &state);
    context.timestamp_ns = now + 1;
    TOTKSemanticFeedbackOutput future = totk_semantic_feedback_output(
        native, context, no_events, now, 1.0f, 1.0f, 1, &state);
    context.timestamp_ns = now;
    context.paused = 1;
    TOTKSemanticFeedbackOutput paused = totk_semantic_feedback_output(
        native, context, no_events, now, 1.0f, 1.0f, 1, &state);
    context.paused = 0;
    context.valid = 0;
    TOTKSemanticFeedbackOutput invalid = totk_semantic_feedback_output(
        native, context, no_events, now, 1.0f, 1.0f, 1, &state);
    context.valid = 1;
    TOTKSemanticFeedbackOutput inactive = totk_semantic_feedback_output(
        native, context, no_events, now, 1.0f, 1.0f, 0, &state);
    context.bow_ready = 0;
    TOTKSemanticFeedbackOutput drawn_without_ready = totk_semantic_feedback_output(
        native, context, no_events, now, 1.0f, 1.0f, 1, &state);
    context.bow_drawn = 0;
    TOTKSemanticFeedbackOutput no_bow = totk_semantic_feedback_output(
        native, context, no_events, now, 1.0f, 1.0f, 1, &state);
    context.bow_ready = 2;
    TOTKSemanticFeedbackOutput malformed_ready = totk_semantic_feedback_output(
        native, context, no_events, now, 1.0f, 1.0f, 1, &state);
    context.bow_ready = 1;
    context.bow_drawn = 2;
    TOTKSemanticFeedbackOutput malformed_drawn = totk_semantic_feedback_output(
        native, context, no_events, now, 1.0f, 1.0f, 1, &state);
    expect(trigger_off(stale.right_trigger) && trigger_off(future.right_trigger) &&
               trigger_off(paused.right_trigger) && trigger_off(invalid.right_trigger) &&
               trigger_off(inactive.right_trigger) &&
               trigger_off(drawn_without_ready.right_trigger) &&
               trigger_off(no_bow.right_trigger) &&
               trigger_off(malformed_ready.right_trigger) &&
               trigger_off(malformed_drawn.right_trigger),
           "bow resistance fails quiet without readiness and across stale, future, paused, invalid, inactive, absent, and malformed context");
}

static void test_semantic_block_envelope(void) {
    const uint64_t start = UINT64_C(3000000000);
    TOTKSemanticFeedbackState state = {0};
    const TOTKFeedbackSample neutral = sample(0.0f, 160.0f, 0.0f, 320.0f, start);
    TOTKSemanticContext raised = semantic_context(start);
    raised.shield_raised = 1;
    TOTKSemanticEvents events = {0, 0, start, 0, TOTK_MELEE_UNKNOWN};

    TOTKSemanticFeedbackOutput onset = totk_semantic_feedback_output(
        neutral, raised, events, start, 1.0f, 1.0f, 1, &state);
    expect(onset.left_trigger.mode == TOTK_TRIGGER_VIBRATION &&
               nearf(onset.left_trigger.amplitude, 0.30f, 0.000001f) &&
               nearf(onset.left_trigger.frequency, 0.20f, 0.000001f),
           "an authoritative block event starts a short left-trigger thump");

    const uint64_t halfway = start + TOTK_SEMANTIC_BLOCK_DURATION_NS / 2;
    TOTKSemanticFeedbackOutput middle = totk_semantic_feedback_output(
        neutral, raised, events, halfway, 1.0f, 1.0f, 1, &state);
    expect(middle.left_trigger.mode == TOTK_TRIGGER_VIBRATION &&
               nearf(middle.left_trigger.amplitude, 0.075f, 0.00001f),
           "the block thump follows a short quadratic decay envelope");

    events.shield_block_timestamp_ns = start + UINT64_C(50000000);
    (void)totk_semantic_feedback_output(
        neutral, raised, events, start + UINT64_C(50000000), 1.0f, 1.0f, 1, &state);
    TOTKSemanticFeedbackOutput expired = totk_semantic_feedback_output(
        neutral, raised, events, start + TOTK_SEMANTIC_BLOCK_DURATION_NS,
        1.0f, 1.0f, 1, &state);
    expect(expired.left_trigger.mode == TOTK_TRIGGER_FEEDBACK &&
               expired.left_trigger.start_strength < onset.left_trigger.amplitude / 2.0f,
           "a rate-limited block does not restart the thump and light posture resistance resumes");

    events.shield_block_timestamp_ns = start + UINT64_C(130000000);
    raised.timestamp_ns = events.shield_block_timestamp_ns;
    TOTKSemanticFeedbackOutput later = totk_semantic_feedback_output(
        neutral, raised, events, events.shield_block_timestamp_ns,
        1.0f, 1.0f, 1, &state);
    expect(later.left_trigger.mode == TOTK_TRIGGER_VIBRATION &&
               nearf(later.left_trigger.amplitude, 0.30f, 0.000001f),
           "a later authoritative block after cooldown starts a new thump");
    expect(trigger_off(later.right_trigger), "block feedback never activates the right trigger");

    TOTKSemanticFeedbackState default_gain_state = {0};
    const TOTKSemanticEvents no_events = {0};
    TOTKSemanticFeedbackOutput default_hold = totk_semantic_feedback_output(
        neutral, raised, no_events, raised.timestamp_ns,
        0.9f, 0.55f, 1, &default_gain_state);
    TOTKSemanticFeedbackOutput default_hit = totk_semantic_feedback_output(
        neutral, raised, events, raised.timestamp_ns,
        0.9f, 0.55f, 1, &default_gain_state);
    expect(default_hold.left_trigger.start_strength > 0.0f &&
               default_hold.left_trigger.start_strength < 0.07f &&
               default_hit.left_trigger.mode == TOTK_TRIGGER_VIBRATION &&
               default_hit.left_trigger.amplitude > default_hold.left_trigger.start_strength * 2.0f,
           "default trigger gain keeps shield holding subtle with a distinct impact kick");

    TOTKSemanticFeedbackState future_state = {0};
    TOTKSemanticEvents future_event = {0, 0, start + 1, 0, TOTK_MELEE_UNKNOWN};
    TOTKSemanticContext frame = semantic_context(start);
    TOTKSemanticFeedbackOutput rejected = totk_semantic_feedback_output(
        neutral, frame, future_event, start, 1.0f, 1.0f, 1, &future_state);
    frame.timestamp_ns = start + 1;
    TOTKSemanticFeedbackOutput no_replay = totk_semantic_feedback_output(
        neutral, frame, future_event, start + 1, 1.0f, 1.0f, 1, &future_state);
    expect(trigger_off(rejected.left_trigger) && trigger_off(no_replay.left_trigger),
           "a future block event is discarded rather than replayed later");
}

static void test_semantic_handle_envelopes_and_rate_limits(void) {
    const uint64_t start = UINT64_C(4000000000);
    const TOTKFeedbackSample neutral = sample(0.0f, 160.0f, 0.0f, 320.0f, start);
    TOTKSemanticContext frame = semantic_context(start);
    frame.npc_interaction_allowed = 1;
    TOTKSemanticFeedbackState state = {0};
    TOTKSemanticEvents events = {start, 0, 0, 0, TOTK_MELEE_UNKNOWN};

    TOTKSemanticFeedbackOutput npc_onset = totk_semantic_feedback_output(
        neutral, frame, events, start, 1.0f, 1.0f, 1, &state);
    expect(nearf(npc_onset.handle_intensity, 0.09f, 0.000001f) &&
               nearf(npc_onset.handle_sharpness, 0.45f, 0.000001f),
           "NPC interaction emits a subtle handle pulse");
    frame.timestamp_ns = start + TOTK_SEMANTIC_NPC_DURATION_NS / 2;
    TOTKSemanticFeedbackOutput npc_middle = totk_semantic_feedback_output(
        neutral, frame, events, frame.timestamp_ns,
        1.0f, 1.0f, 1, &state);
    expect(nearf(npc_middle.handle_intensity, 0.0225f, 0.00001f),
           "NPC pulse follows its quadratic decay envelope");

    events.npc_interaction_timestamp_ns = start + UINT64_C(50000000);
    frame.timestamp_ns = events.npc_interaction_timestamp_ns;
    (void)totk_semantic_feedback_output(
        neutral, frame, events, frame.timestamp_ns, 1.0f, 1.0f, 1, &state);
    frame.timestamp_ns = start + UINT64_C(100000000);
    TOTKSemanticFeedbackOutput npc_expired = totk_semantic_feedback_output(
        neutral, frame, events, frame.timestamp_ns, 1.0f, 1.0f, 1, &state);
    expect(npc_expired.handle_intensity == 0.0f,
           "rapid repeated NPC events do not restart the pulse");
    events.npc_interaction_timestamp_ns = start + UINT64_C(300000000);
    frame.timestamp_ns = events.npc_interaction_timestamp_ns;
    TOTKSemanticFeedbackOutput npc_later = totk_semantic_feedback_output(
        neutral, frame, events, events.npc_interaction_timestamp_ns,
        1.0f, 1.0f, 1, &state);
    expect(nearf(npc_later.handle_intensity, 0.09f, 0.000001f),
           "NPC event after cooldown starts a new pulse");

    totk_semantic_feedback_state_reset(&state);
    events = (TOTKSemanticEvents){0, start, 0, 0, TOTK_MELEE_UNKNOWN};
    frame.timestamp_ns = start;
    TOTKSemanticFeedbackOutput landing_onset = totk_semantic_feedback_output(
        neutral, frame, events, start, 1.0f, 1.0f, 1, &state);
    frame.timestamp_ns = start + TOTK_SEMANTIC_LANDING_DURATION_NS / 2;
    TOTKSemanticFeedbackOutput landing_middle = totk_semantic_feedback_output(
        neutral, frame, events, frame.timestamp_ns,
        1.0f, 1.0f, 1, &state);
    frame.timestamp_ns = start + TOTK_SEMANTIC_LANDING_DURATION_NS;
    TOTKSemanticFeedbackOutput landing_expired = totk_semantic_feedback_output(
        neutral, frame, events, frame.timestamp_ns,
        1.0f, 1.0f, 1, &state);
    expect(nearf(landing_onset.handle_intensity, 0.13f, 0.000001f) &&
               nearf(landing_onset.handle_sharpness, 0.20f, 0.000001f),
           "landing emits a restrained low-sharpness handle pulse");
    expect(nearf(landing_middle.handle_intensity, 0.0325f, 0.00001f) &&
               landing_expired.handle_intensity == 0.0f,
           "landing pulse decays and expires at its bounded duration");

    totk_semantic_feedback_state_reset(&state);
    events = (TOTKSemanticEvents){0, start, 0, 0, TOTK_MELEE_UNKNOWN};
    frame.timestamp_ns = start;
    (void)totk_semantic_feedback_output(
        neutral, frame, events, start, 1.0f, 1.0f, 1, &state);
    events.landing_timestamp_ns = start + UINT64_C(50000000);
    frame.timestamp_ns = events.landing_timestamp_ns;
    (void)totk_semantic_feedback_output(
        neutral, frame, events, frame.timestamp_ns, 1.0f, 1.0f, 1, &state);
    frame.timestamp_ns = start + UINT64_C(130000000);
    TOTKSemanticFeedbackOutput landing_not_restarted = totk_semantic_feedback_output(
        neutral, frame, events, frame.timestamp_ns, 1.0f, 1.0f, 1, &state);
    expect(landing_not_restarted.handle_intensity == 0.0f,
           "rapid repeated landing events do not restart the pulse");
    events.landing_timestamp_ns = start + UINT64_C(200000000);
    frame.timestamp_ns = events.landing_timestamp_ns;
    TOTKSemanticFeedbackOutput landing_later = totk_semantic_feedback_output(
        neutral, frame, events, frame.timestamp_ns, 1.0f, 1.0f, 1, &state);
    expect(nearf(landing_later.handle_intensity, 0.13f, 0.000001f),
           "landing event after cooldown starts a new pulse");

    totk_semantic_feedback_state_reset(&state);
    events = (TOTKSemanticEvents){start, start, 0, 0, TOTK_MELEE_UNKNOWN};
    frame.timestamp_ns = start;
    TOTKSemanticFeedbackOutput combined = totk_semantic_feedback_output(
        neutral, frame, events, start, 1.0f, 1.0f, 1, &state);
    expect(nearf(combined.handle_intensity, hypotf(0.09f, 0.13f), 0.00001f) &&
               nearf(combined.handle_sharpness,
                     (0.09f * 0.45f + 0.13f * 0.20f) / 0.22f, 0.00001f),
           "simultaneous semantic pulses combine with bounded RMS intensity");
}

static void test_semantic_npc_allpause_exception(void) {
    const uint64_t start = UINT64_C(4500000000);
    const TOTKFeedbackSample native = sample(0.3f, 160.0f, 0.4f, 320.0f, start);
    TOTKSemanticFeedbackState state = {0};
    TOTKSemanticContext frame = semantic_context(start);
    frame.paused = 1;
    frame.npc_interaction_allowed = 1;
    frame.shield_raised = 1;
    frame.shield_surfing = 1;
    frame.speed_mps = 6.0f;
    const TOTKSemanticEvents all_events = {
        start, start, start, 0, TOTK_MELEE_UNKNOWN};

    TOTKSemanticFeedbackOutput allowed = totk_semantic_feedback_output(
        native, frame, all_events, start, 1.0f, 1.0f, 1, &state);
    expect(!allowed.context_fresh &&
               nearf(allowed.handle_intensity, 0.09f, 0.000001f) &&
               nearf(allowed.handle_sharpness, 0.45f, 0.000001f) &&
               trigger_off(allowed.left_trigger) && trigger_off(allowed.right_trigger),
           "an explicitly allowed NPCTalk entry survives AllPause while native, landing, surf, and block effects stay quiet");

    totk_semantic_feedback_state_reset(&state);
    frame.timestamp_ns = start + UINT64_C(1000000000);
    frame.npc_interaction_allowed = 0;
    frame.shield_raised = 0;
    frame.shield_surfing = 0;
    frame.speed_mps = 0.0f;
    TOTKSemanticEvents npc = {
        frame.timestamp_ns, 0, 0, 0, TOTK_MELEE_UNKNOWN};
    const TOTKFeedbackSample fresh_native =
        sample(0.3f, 160.0f, 0.4f, 320.0f, frame.timestamp_ns);
    TOTKSemanticFeedbackOutput menu_disallowed = totk_semantic_feedback_output(
        fresh_native, frame, npc, frame.timestamp_ns, 1.0f, 1.0f, 1, &state);
    frame.paused = 0;
    frame.npc_interaction_allowed = 1;
    frame.timestamp_ns++;
    TOTKSemanticFeedbackOutput no_menu_replay = totk_semantic_feedback_output(
        fresh_native, frame, npc, frame.timestamp_ns, 1.0f, 1.0f, 1, &state);
    expect(semantic_quiet(menu_disallowed) && no_menu_replay.handle_intensity > 0.0f &&
               nearf(no_menu_replay.handle_intensity, 0.5f, 0.00001f),
           "a menu-disallowed NPC event is dropped immediately and never replays after resume");

    totk_semantic_feedback_state_reset(&state);
    frame = semantic_context(start + UINT64_C(2000000000));
    frame.valid = 0;
    frame.npc_interaction_allowed = 1;
    npc.npc_interaction_timestamp_ns = frame.timestamp_ns;
    TOTKSemanticFeedbackOutput loading = totk_semantic_feedback_output(
        (TOTKFeedbackSample){0}, frame, npc, frame.timestamp_ns, 1.0f, 1.0f, 1, &state);
    frame.valid = 1;
    frame.timestamp_ns++;
    TOTKSemanticFeedbackOutput no_loading_replay = totk_semantic_feedback_output(
        (TOTKFeedbackSample){0}, frame, npc, frame.timestamp_ns, 1.0f, 1.0f, 1, &state);
    expect(semantic_quiet(loading) && semantic_quiet(no_loading_replay),
           "an NPC event observed during loading is dropped instead of delayed until valid context");

    totk_semantic_feedback_state_reset(&state);
    frame = semantic_context(start);
    frame.npc_interaction_allowed = 2;
    TOTKSemanticFeedbackOutput malformed = totk_semantic_feedback_output(
        (TOTKFeedbackSample){0}, frame,
        (TOTKSemanticEvents){start, 0, 0, 0, TOTK_MELEE_UNKNOWN},
        start, 1.0f, 1.0f, 1, &state);
    expect(!malformed.context_fresh && semantic_quiet(malformed),
           "a non-boolean NPC authorization flag fails closed");
}

static void test_semantic_melee_native_shaping(void) {
    const uint64_t now = UINT64_C(4800000000);
    const TOTKFeedbackSample native = sample(0.3f, 160.0f, 0.4f, 320.0f, now);
    const TOTKFeedbackOutput native_policy =
        totk_feedback_output(native, now, 1.0f, 0.0f, 1);
    TOTKSemanticFeedbackState state = {0};
    TOTKSemanticContext frame = semantic_context(now);
    TOTKSemanticEvents events = {0};

    TOTKSemanticFeedbackOutput baseline = totk_semantic_feedback_output(
        native, frame, events, now, 1.0f, 1.0f, 1, &state);
    events.melee_swing_timestamp_ns = now - UINT64_C(10000000);
    events.melee_kind = TOTK_MELEE_BLUNT;
    TOTKSemanticFeedbackOutput blunt = totk_semantic_feedback_output(
        native, frame, events, now, 1.0f, 1.0f, 1, &state);
    events.melee_kind = TOTK_MELEE_SHARP;
    TOTKSemanticFeedbackOutput sharp = totk_semantic_feedback_output(
        native, frame, events, now, 1.0f, 1.0f, 1, &state);
    const float expected_sharpness =
        native_policy.sharpness + 0.18f * (1.0f - native_policy.sharpness);
    expect(nearf(baseline.handle_intensity, 0.5f, 0.00001f) &&
               nearf(blunt.handle_intensity / baseline.handle_intensity, 1.15f, 0.00001f) &&
               nearf(sharp.handle_intensity / baseline.handle_intensity, 1.15f, 0.00001f),
           "verified blunt and sharp swings strengthen actual native rumble by fifteen percent");
    expect(nearf(blunt.handle_sharpness, native_policy.sharpness, 0.000001f) &&
               nearf(sharp.handle_sharpness, expected_sharpness, 0.000001f) &&
               sharp.handle_sharpness > blunt.handle_sharpness,
           "blunt swings retain native frequency while sharp swings bias its sharpness upward");
    expect(trigger_off(blunt.left_trigger) && trigger_off(blunt.right_trigger) &&
               trigger_off(sharp.left_trigger) && trigger_off(sharp.right_trigger),
           "melee shaping never synthesizes adaptive-trigger output");

    TOTKSemanticFeedbackOutput half_gain = totk_semantic_feedback_output(
        native, frame, events, now, 0.5f, 0.0f, 1, &state);
    TOTKSemanticFeedbackOutput trigger_gain = totk_semantic_feedback_output(
        native, frame, events, now, 1.0f, 2.0f, 1, &state);
    expect(nearf(half_gain.handle_intensity, sharp.handle_intensity * 0.5f, 0.000001f) &&
               nearf(trigger_gain.handle_intensity, sharp.handle_intensity, 0.000001f) &&
               trigger_off(half_gain.left_trigger) && trigger_off(trigger_gain.left_trigger),
           "melee shaping follows handle gain and remains independent of trigger gain");

    const TOTKFeedbackSample maximum = sample(1.0f, 160.0f, 1.0f, 320.0f, now);
    TOTKSemanticFeedbackOutput capped = totk_semantic_feedback_output(
        maximum, frame, events, now, 2.0f, 1.0f, 1, &state);
    expect(capped.handle_intensity == 1.0f,
           "melee strengthening and handle gain remain capped at one");

    events.melee_swing_timestamp_ns = now - TOTK_SEMANTIC_MELEE_LEASE_NS;
    TOTKSemanticFeedbackOutput stale = totk_semantic_feedback_output(
        native, frame, events, now, 1.0f, 1.0f, 1, &state);
    events.melee_swing_timestamp_ns = now + 1;
    TOTKSemanticFeedbackOutput future = totk_semantic_feedback_output(
        native, frame, events, now, 1.0f, 1.0f, 1, &state);
    events.melee_swing_timestamp_ns = now;
    events.melee_kind = 3;
    TOTKSemanticFeedbackOutput invalid_kind = totk_semantic_feedback_output(
        native, frame, events, now, 1.0f, 1.0f, 1, &state);
    expect(nearf(stale.handle_intensity, baseline.handle_intensity, 0.000001f) &&
               nearf(future.handle_intensity, baseline.handle_intensity, 0.000001f) &&
               nearf(invalid_kind.handle_intensity, baseline.handle_intensity, 0.000001f),
           "stale, future, and invalid swing metadata leaves native rumble unchanged");

    events.melee_kind = TOTK_MELEE_SHARP;
    events.melee_swing_timestamp_ns = now;
    TOTKFeedbackSample pre_swing_native = native;
    pre_swing_native.timestamp_ns = now - 1;
    TOTKSemanticFeedbackOutput pre_swing = totk_semantic_feedback_output(
        pre_swing_native, frame, events, now, 1.0f, 1.0f, 1, &state);
    expect(nearf(pre_swing.handle_intensity, baseline.handle_intensity, 0.000001f),
           "native rumble sampled before the verified swing is never shaped retroactively");

    frame.paused = 1;
    TOTKSemanticFeedbackOutput paused = totk_semantic_feedback_output(
        native, frame, events, now, 1.0f, 1.0f, 1, &state);
    frame.paused = 0;
    frame.timestamp_ns = now - TOTK_SEMANTIC_CONTEXT_LEASE_NS;
    TOTKSemanticFeedbackOutput stale_context = totk_semantic_feedback_output(
        native, frame, events, now, 1.0f, 1.0f, 1, &state);
    frame.timestamp_ns = now;
    frame.valid = 0;
    TOTKSemanticFeedbackOutput invalid_context = totk_semantic_feedback_output(
        native, frame, events, now, 1.0f, 1.0f, 1, &state);
    TOTKSemanticFeedbackOutput inactive = totk_semantic_feedback_output(
        native, frame, events, now, 1.0f, 1.0f, 0, &state);
    expect(semantic_quiet(paused) &&
               nearf(stale_context.handle_intensity, baseline.handle_intensity, 0.000001f) &&
               nearf(invalid_context.handle_intensity, baseline.handle_intensity, 0.000001f) &&
               semantic_quiet(inactive),
           "paused, stale, invalid, and inactive lifecycle states never apply a melee boost");

    frame = semantic_context(now);
    TOTKSemanticFeedbackOutput no_native = totk_semantic_feedback_output(
        sample(0.0f, 160.0f, 0.0f, 320.0f, now), frame, events,
        now, 1.0f, 1.0f, 1, &state);
    TOTKSemanticFeedbackOutput new_epoch = totk_semantic_feedback_output(
        native, frame, (TOTKSemanticEvents){0}, now, 1.0f, 1.0f, 1, &state);
    expect(semantic_quiet(no_native) &&
               nearf(new_epoch.handle_intensity, baseline.handle_intensity, 0.000001f),
           "melee metadata generates no synthetic pulse and cleared epoch events cannot boost native rumble");
}

static void test_semantic_surf_texture(void) {
    const uint64_t start = UINT64_C(5000000000);
    TOTKSemanticFeedbackState state = {0};
    const TOTKFeedbackSample neutral = sample(0.0f, 160.0f, 0.0f, 320.0f, start);
    const TOTKSemanticEvents no_events = {0};
    TOTKSemanticContext surfing = semantic_context(start);
    surfing.shield_surfing = 1;
    surfing.speed_mps = 6.0f;

    TOTKSemanticFeedbackOutput first = totk_semantic_feedback_output(
        neutral, surfing, no_events, start, 1.0f, 1.0f, 1, &state);
    surfing.timestamp_ns = start + TOTK_SEMANTIC_SURF_TICK_NS;
    TOTKSemanticFeedbackOutput second = totk_semantic_feedback_output(
        neutral, surfing, no_events, surfing.timestamp_ns, 1.0f, 1.0f, 1, &state);
    expect(first.handle_intensity > 0.02f && first.handle_intensity <= 0.12f &&
               first.handle_sharpness >= 0.55f && first.handle_sharpness <= 0.85f &&
               second.handle_intensity > 0.02f && second.handle_intensity <= 0.12f,
           "moderate-speed shield surf emits perceptible bounded granular handle texture");
    expect(!nearf(first.handle_intensity, second.handle_intensity, 0.000001f) ||
               !nearf(first.handle_sharpness, second.handle_sharpness, 0.000001f),
           "surf texture varies over time instead of producing a solid buzz");
    expect(trigger_off(first.left_trigger) && trigger_off(first.right_trigger),
           "shield surfing alone does not activate either trigger");

    surfing.timestamp_ns = start + UINT64_C(24000000);
    surfing.speed_mps = 0.1f;
    TOTKSemanticFeedbackOutput stationary = totk_semantic_feedback_output(
        neutral, surfing, no_events, surfing.timestamp_ns, 1.0f, 1.0f, 1, &state);
    expect(semantic_quiet(stationary),
           "near-stationary shield-surf posture does not buzz");
    surfing.timestamp_ns += TOTK_SEMANTIC_SURF_TICK_NS;
    surfing.speed_mps = 100.0f;
    TOTKSemanticFeedbackOutput high_speed = totk_semantic_feedback_output(
        neutral, surfing, no_events, surfing.timestamp_ns, 1.0f, 1.0f, 1, &state);
    expect(high_speed.handle_intensity >= 0.05f && high_speed.handle_intensity <= 0.12f,
           "shield-surf texture remains bounded beyond high movement speed");

    surfing.timestamp_ns += TOTK_SEMANTIC_SURF_TICK_NS;
    surfing.speed_mps = NAN;
    TOTKSemanticFeedbackOutput bad_speed = totk_semantic_feedback_output(
        neutral, surfing, no_events, surfing.timestamp_ns, 1.0f, 1.0f, 1, &state);
    expect(!bad_speed.context_fresh && semantic_quiet(bad_speed),
           "nonfinite shield-surf speed fails closed");
    surfing.timestamp_ns += TOTK_SEMANTIC_SURF_TICK_NS;
    surfing.speed_mps = -1.0f;
    TOTKSemanticFeedbackOutput negative_speed = totk_semantic_feedback_output(
        neutral, surfing, no_events, surfing.timestamp_ns, 1.0f, 1.0f, 1, &state);
    expect(!negative_speed.context_fresh && semantic_quiet(negative_speed),
           "negative shield-surf speed fails closed");

    surfing.timestamp_ns = start;
    surfing.speed_mps = 6.0f;
    TOTKSemanticFeedbackOutput stale = totk_semantic_feedback_output(
        neutral, surfing, no_events, start + TOTK_SEMANTIC_CONTEXT_LEASE_NS,
        1.0f, 1.0f, 1, &state);
    expect(!stale.context_fresh && semantic_quiet(stale),
           "stale shield-surf context stops texture at the lease boundary");
}

static void test_semantic_invalid_and_lifecycle(void) {
    const uint64_t start = UINT64_C(6000000000);
    TOTKSemanticFeedbackState state = {0};
    const TOTKFeedbackSample neutral = sample(0.0f, 160.0f, 0.0f, 320.0f, start);
    const TOTKFeedbackSample native = sample(0.3f, 160.0f, 0.4f, 320.0f, start);
    TOTKSemanticContext frame = semantic_context(start);
    frame.npc_interaction_allowed = 1;
    const TOTKSemanticEvents npc = {start, 0, 0, 0, TOTK_MELEE_UNKNOWN};

    TOTKSemanticFeedbackOutput no_state = totk_semantic_feedback_output(
        native, frame, npc, start, 1.0f, 1.0f, 1, NULL);
    TOTKSemanticFeedbackOutput bad_haptic_gain = totk_semantic_feedback_output(
        native, frame, npc, start, NAN, 1.0f, 1, &state);
    TOTKSemanticFeedbackOutput bad_trigger_gain = totk_semantic_feedback_output(
        native, frame, npc, start, 1.0f, INFINITY, 1, &state);
    expect(semantic_quiet(no_state) && semantic_quiet(bad_haptic_gain) &&
               semantic_quiet(bad_trigger_gain),
           "missing state and nonfinite semantic gains fail closed");

    totk_semantic_feedback_state_reset(&state);
    TOTKSemanticFeedbackOutput inactive = totk_semantic_feedback_output(
        native, frame, npc, start, 1.0f, 1.0f, 0, &state);
    frame.timestamp_ns = start + 1;
    TOTKSemanticFeedbackOutput no_replay = totk_semantic_feedback_output(
        neutral, frame, npc, start + 1, 1.0f, 1.0f, 1, &state);
    expect(semantic_quiet(inactive) && semantic_quiet(no_replay),
           "events observed while inactive remain quiet and do not replay on activation");

    totk_semantic_feedback_state_reset(&state);
    frame = semantic_context(start);
    frame.paused = 1;
    TOTKSemanticFeedbackOutput paused = totk_semantic_feedback_output(
        neutral, frame, npc, start, 1.0f, 1.0f, 1, &state);
    frame.paused = 0;
    frame.timestamp_ns = start + 1;
    TOTKSemanticFeedbackOutput no_paused_replay = totk_semantic_feedback_output(
        neutral, frame, npc, start + 1, 1.0f, 1.0f, 1, &state);
    expect(semantic_quiet(paused) && semantic_quiet(no_paused_replay),
           "events observed while paused remain quiet and do not replay after resume");

    totk_semantic_feedback_state_reset(&state);
    frame = semantic_context(start);
    frame.npc_interaction_allowed = 1;
    TOTKSemanticFeedbackOutput active_pulse = totk_semantic_feedback_output(
        neutral, frame, npc, start, 1.0f, 1.0f, 1, &state);
    frame.timestamp_ns = start + UINT64_C(10000000);
    frame.paused = 1;
    frame.npc_interaction_allowed = 0;
    TOTKSemanticFeedbackOutput interrupted = totk_semantic_feedback_output(
        neutral, frame, npc, frame.timestamp_ns, 1.0f, 1.0f, 1, &state);
    frame.timestamp_ns++;
    frame.paused = 0;
    TOTKSemanticFeedbackOutput stays_stopped = totk_semantic_feedback_output(
        neutral, frame, npc, frame.timestamp_ns, 1.0f, 1.0f, 1, &state);
    expect(active_pulse.handle_intensity > 0.0f && semantic_quiet(interrupted) &&
               semantic_quiet(stays_stopped),
           "revoking NPC authorization immediately stops an active pulse and resume does not replay it");

    totk_semantic_feedback_state_reset(&state);
    frame = semantic_context(start);
    frame.npc_interaction_allowed = 1;
    frame.shield_raised = 1;
    TOTKSemanticFeedbackOutput handles_off = totk_semantic_feedback_output(
        neutral, frame, npc, start,
        0.0f, 1.0f, 1, &state);
    expect(handles_off.handle_intensity == 0.0f &&
               handles_off.left_trigger.mode == TOTK_TRIGGER_FEEDBACK,
           "zero handle gain preserves authoritative trigger semantics");
    totk_semantic_feedback_state_reset(&state);
    TOTKSemanticFeedbackOutput trigger_off_output = totk_semantic_feedback_output(
        neutral, frame, npc, start,
        1.0f, 0.0f, 1, &state);
    expect(trigger_off_output.handle_intensity > 0.0f &&
               trigger_off(trigger_off_output.left_trigger),
           "zero trigger gain preserves semantic handle pulses");
}

static void axes(float x, float y, float deadzone, float curve, int32_t *out_x, int32_t *out_y) {
    *out_x = INT32_MAX;
    *out_y = INT32_MAX;
    totk_stick_axes(x, y, deadzone, curve, out_x, out_y);
}

static void test_stick_invalid_and_deadzone(void) {
    int32_t x, y;
    axes(NAN, 0.0f, 0.1f, 1.0f, &x, &y);
    expect(x == 0 && y == 0, "NaN stick input is zero");
    axes(0.0f, INFINITY, 0.1f, 1.0f, &x, &y);
    expect(x == 0 && y == 0, "infinite stick input is zero");
    axes(0.5f, 0.0f, NAN, 1.0f, &x, &y);
    expect(x == 0 && y == 0, "nonfinite dead zone is zero");
    axes(0.5f, 0.0f, 0.1f, INFINITY, &x, &y);
    expect(x == 0 && y == 0, "nonfinite curve is zero");
    axes(0.5f, 0.0f, -0.1f, 1.0f, &x, &y);
    expect(x == 0 && y == 0, "negative dead zone is invalid");
    axes(0.5f, 0.0f, 1.0f, 1.0f, &x, &y);
    expect(x == 0 && y == 0, "a full dead zone is invalid");
    axes(0.5f, 0.0f, 0.1f, 0.0f, &x, &y);
    expect(x == 0 && y == 0, "a nonpositive response curve is invalid");
    axes(0.06f, 0.08f, 0.10f, 1.0f, &x, &y);
    expect(x == 0 && y == 0, "the radial dead-zone boundary is zero");
    axes(0.03f, 0.04f, 0.10f, 1.0f, &x, &y);
    expect(x == 0 && y == 0, "a diagonal inside the radial dead zone is zero");
}

static void test_stick_geometry_and_curve(void) {
    int32_t x, y;
    axes(1.0f, 0.0f, 0.10f, 1.0f, &x, &y);
    expect(x == 32767 && y == 0, "positive axis reaches the full signed range");
    axes(-1.0f, 0.0f, 0.10f, 1.0f, &x, &y);
    expect(x == -32767 && y == 0, "negative axis reaches the symmetric full range");
    axes(0.55f, 0.0f, 0.10f, 1.0f, &x, &y);
    expect(x == 16384 && y == 0, "dead-zone removal renormalizes the remaining axis range");

    axes(1.0f, 1.0f, 0.0f, 1.0f, &x, &y);
    expect(x == y && abs(x - 23170) <= 1 &&
               llabs((long long)x * x + (long long)y * y - (long long)32767 * 32767) < 100000,
           "a full diagonal preserves direction and radial magnitude");
    axes(0.6f, 0.8f, 0.0f, 1.0f, &x, &y);
    expect(abs(x - 19660) <= 1 && abs(y - 26214) <= 1,
           "an arbitrary unit direction preserves its component ratio");
    axes(FLT_MAX, FLT_MAX, 0.0f, 1.0f, &x, &y);
    expect(x == y && x > 0 && x <= 32767,
           "large finite stick input remains bounded and direction-preserving");

    int32_t low, middle, high, unused;
    axes(0.25f, 0.0f, 0.0f, 1.1f, &low, &unused);
    axes(0.50f, 0.0f, 0.0f, 1.1f, &middle, &unused);
    axes(0.75f, 0.0f, 0.0f, 1.1f, &high, &unused);
    expect(0 < low && low < middle && middle < high && high < 32767,
           "the power curve is monotonic");
    axes(0.50f, 0.0f, 0.0f, 1.0f, &x, &y);
    expect(middle < x, "a curve above one softens partial stick travel");
}

int main(void) {
    test_feedback_freshness();
    test_feedback_invalid_values();
    test_feedback_mix_and_bounds();
    test_feedback_silence();
    test_semantic_native_handles_only();
    test_semantic_shield_posture();
    test_semantic_bow_trigger();
    test_semantic_block_envelope();
    test_semantic_handle_envelopes_and_rate_limits();
    test_semantic_npc_allpause_exception();
    test_semantic_melee_native_shaping();
    test_semantic_surf_texture();
    test_semantic_invalid_and_lifecycle();
    test_stick_invalid_and_deadzone();
    test_stick_geometry_and_curve();
    if (failures)
        return 1;
    puts("PASS: DualSense native, semantic, and radial stick policy");
    return 0;
}
