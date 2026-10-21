/* Deterministic tests for exact-cue handle-haptic envelope policy. */
#include "../runtime/haptic_cues.h"

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

static TOTKHapticCueEvent event(uint32_t cue_id, uint32_t epoch, uint64_t sequence,
                                uint64_t timestamp_ns) {
    TOTKHapticCueEvent value = {cue_id, epoch, sequence, timestamp_ns};
    return value;
}

static TOTKHapticCueGate gate(uint32_t epoch, uint64_t timestamp_ns, int paused) {
    TOTKHapticCueGate value = {epoch, timestamp_ns, 1, paused};
    return value;
}

static TOTKHapticCueDescriptor descriptor(uint32_t cue_id,
                                           const TOTKHapticCuePoint *points,
                                           size_t count,
                                           int allow_paused) {
    TOTKHapticCueDescriptor value = {cue_id, points, count, allow_paused};
    return value;
}

static void test_linear_envelope_and_timestamp_anchor(void) {
    const uint32_t epoch = 7;
    const uint64_t at = UINT64_C(1000000000);
    const TOTKHapticCuePoint points[] = {
        {0, 0.0f, 0.1f}, {50, 1.0f, 0.9f}, {100, 0.0f, 0.3f}};
    const TOTKHapticCueDescriptor cue =
        descriptor(11, points, 3, TOTK_HAPTIC_CUE_GAMEPLAY);
    TOTKHapticCueState state = {0};

    TOTKHapticCueOutput middle = totk_haptic_cue_compose(
        0.0f, 0.0f, &cue, event(11, epoch, 1, at), gate(epoch, at, 0), epoch,
        at + UINT64_C(25000000), 1.0f, 1, &state);
    expect(middle.cue_active && nearf(middle.intensity, 0.5f, 0.00001f) &&
               nearf(middle.sharpness, 0.5f, 0.00001f),
           "first evaluation uses elapsed event time instead of restarting the envelope");

    TOTKHapticCueOutput falling = totk_haptic_cue_compose(
        0.0f, 0.0f, NULL, event(11, epoch, 1, at),
        gate(epoch, at + UINT64_C(60000000), 0), epoch,
        at + UINT64_C(75000000), 1.0f, 1, &state);
    expect(falling.cue_active && nearf(falling.intensity, 0.5f, 0.00001f) &&
               nearf(falling.sharpness, 0.6f, 0.00001f),
           "accepted dynamic points are copied and linearly sampled without the descriptor");

    TOTKHapticCueOutput ended = totk_haptic_cue_compose(
        0.2f, 0.7f, NULL, event(11, epoch, 1, at),
        gate(epoch, at + UINT64_C(100000000), 0), epoch,
        at + UINT64_C(100000000), 1.0f, 1, &state);
    expect(!ended.cue_active && ended.intensity == 0.2f && ended.sharpness == 0.7f &&
               !state.active,
           "the final offset is the exclusive envelope end boundary");
}

static void test_max_composition_and_gain(void) {
    const uint32_t epoch = 3;
    const uint64_t at = UINT64_C(2000000000);
    const TOTKHapticCuePoint points[] = {{0, 0.4f, 0.9f}, {100, 0.4f, 0.9f}};
    const TOTKHapticCueDescriptor cue =
        descriptor(12, points, 2, TOTK_HAPTIC_CUE_GAMEPLAY);
    TOTKHapticCueState state = {0};

    TOTKHapticCueOutput equal = totk_haptic_cue_compose(
        0.4f, 0.2f, &cue, event(12, epoch, 1, at), gate(epoch, at, 0), epoch, at,
        1.0f, 1, &state);
    expect(equal.cue_active && equal.intensity == 0.4f && equal.sharpness == 0.2f,
           "equal cue intensity preserves the complete prior base value");

    TOTKHapticCueOutput native_wins = totk_haptic_cue_compose(
        0.7f, 0.35f, NULL, event(12, epoch, 1, at), gate(epoch, at, 0), epoch,
        at + UINT64_C(10000000), 1.0f, 1, &state);
    expect(native_wins.cue_active && native_wins.intensity == 0.7f &&
               native_wins.sharpness == 0.35f,
           "a later stronger native or semantic base wins without cancelling the cue");

    TOTKHapticCueOutput cue_wins_again = totk_haptic_cue_compose(
        0.1f, 0.35f, NULL, event(12, epoch, 1, at), gate(epoch, at, 0), epoch,
        at + UINT64_C(20000000), 1.0f, 1, &state);
    expect(cue_wins_again.intensity == 0.4f && cue_wins_again.sharpness == 0.9f,
           "an admitted cue remains time-based when the prior base later becomes quieter");

    TOTKHapticCueState gain_state = {0};
    TOTKHapticCueOutput gained = totk_haptic_cue_compose(
        0.0f, 0.0f, &cue, event(12, epoch, 2, at), gate(epoch, at, 0), epoch, at,
        99.0f, 1, &gain_state);
    expect(gained.intensity == 0.8f && gained.sharpness == 0.9f,
           "finite cue gain clamps to two before bounded output composition");

    TOTKHapticCueState muted_state = {0};
    TOTKHapticCueOutput muted = totk_haptic_cue_compose(
        0.3f, 0.4f, &cue, event(12, epoch, 3, at), gate(epoch, at, 0), epoch, at,
        0.0f, 1, &muted_state);
    expect(muted.intensity == 0.3f && muted.sharpness == 0.4f &&
               muted_state.has_seen && !muted_state.active,
           "zero gain consumes a new event and cannot replay it later");
}

static void test_point_sanitizing_and_bounds(void) {
    const uint32_t epoch = 4;
    const uint64_t at = UINT64_C(3000000000);
    const TOTKHapticCuePoint clamped_points[] = {
        {0, -3.0f, 4.0f}, {20, 2.0f, -2.0f}, {40, 0.0f, 0.5f}};
    const TOTKHapticCueDescriptor clamped = descriptor(20, clamped_points, 3, 0);
    TOTKHapticCueState state = {0};
    TOTKHapticCueOutput value = totk_haptic_cue_compose(
        0.0f, 0.0f, &clamped, event(20, epoch, 1, at), gate(epoch, at, 0), epoch,
        at + UINT64_C(20000000), 1.0f, 1, &state);
    expect(value.intensity == 1.0f && value.sharpness == 0.0f &&
               state.active_points[0].intensity == 0.0f &&
               state.active_points[0].sharpness == 1.0f,
           "finite envelope values are copied and clamped to normalized bounds");

    TOTKHapticCuePoint maximum[TOTK_HAPTIC_CUE_MAX_POINTS];
    for (size_t i = 0; i < TOTK_HAPTIC_CUE_MAX_POINTS; ++i) {
        maximum[i].offset_ms = (uint32_t)((uint64_t)i * 10000 /
                                          (TOTK_HAPTIC_CUE_MAX_POINTS - 1));
        maximum[i].intensity = (float)i / (float)TOTK_HAPTIC_CUE_MAX_POINTS;
        maximum[i].sharpness = 0.5f;
    }
    const TOTKHapticCueDescriptor max_cue = descriptor(
        21, maximum, TOTK_HAPTIC_CUE_MAX_POINTS,
        TOTK_HAPTIC_CUE_TRUSTED_UI_ALLOW_PAUSED);
    TOTKHapticCueState max_state = {0};
    TOTKHapticCueOutput max_value = totk_haptic_cue_compose(
        0.0f, 0.0f, &max_cue, event(21, epoch, 2, at), (TOTKHapticCueGate){0},
        epoch, at + UINT64_C(50000000), 1.0f, 1, &max_state);
    expect(max_value.cue_active && max_state.active_point_count == 512 &&
               max_state.active_points[511].offset_ms == 10000,
           "the 512-point ten-second trusted UI boundary is admitted and copied");

    TOTKHapticCuePoint too_many[TOTK_HAPTIC_CUE_MAX_POINTS + 1];
    for (size_t i = 0; i < TOTK_HAPTIC_CUE_MAX_POINTS + 1; ++i)
        too_many[i] = (TOTKHapticCuePoint){(uint32_t)i, 0.5f, 0.5f};
    const TOTKHapticCueDescriptor oversized =
        descriptor(22, too_many, TOTK_HAPTIC_CUE_MAX_POINTS + 1, 1);
    TOTKHapticCueState too_many_state = {0};
    TOTKHapticCueOutput too_many_value = totk_haptic_cue_compose(
        0.2f, 0.3f, &oversized, event(22, epoch, 3, at), (TOTKHapticCueGate){0},
        epoch, at, 1.0f, 1, &too_many_state);
    expect(!too_many_value.cue_active && too_many_state.has_seen && !too_many_state.active,
           "more than 512 points fails quiet after consuming the sequence");
}

static void expect_malformed(const TOTKHapticCueDescriptor *cue, uint64_t sequence,
                             const char *message) {
    const uint32_t epoch = 5;
    const uint64_t at = UINT64_C(4000000000);
    TOTKHapticCueState state = {0};
    TOTKHapticCueOutput value = totk_haptic_cue_compose(
        0.25f, 0.6f, cue, event(cue ? cue->cue_id : 30, epoch, sequence, at),
        gate(epoch, at, 0), epoch, at, 1.0f, 1, &state);
    expect(!value.cue_active && value.intensity == 0.25f && value.sharpness == 0.6f &&
               state.has_seen && !state.active,
           message);
}

static void test_malformed_descriptors(void) {
    const TOTKHapticCuePoint good[] = {{0, 0.5f, 0.5f}, {40, 0.0f, 0.5f}};
    const TOTKHapticCuePoint starts_late[] = {{1, 0.5f, 0.5f}, {40, 0.0f, 0.5f}};
    const TOTKHapticCuePoint duplicate[] = {
        {0, 0.5f, 0.5f}, {20, 0.3f, 0.5f}, {20, 0.0f, 0.5f}};
    const TOTKHapticCuePoint descending[] = {
        {0, 0.5f, 0.5f}, {30, 0.3f, 0.5f}, {20, 0.0f, 0.5f}};
    const TOTKHapticCuePoint too_long[] = {{0, 0.5f, 0.5f}, {10001, 0.0f, 0.5f}};
    const TOTKHapticCuePoint nan_point[] = {{0, NAN, 0.5f}, {40, 0.0f, 0.5f}};
    const TOTKHapticCuePoint inf_point[] = {{0, 0.5f, INFINITY}, {40, 0.0f, 0.5f}};
    TOTKHapticCueDescriptor values[] = {
        descriptor(31, NULL, 2, 0),
        descriptor(32, good, 1, 0),
        descriptor(33, starts_late, 2, 0),
        descriptor(34, duplicate, 3, 0),
        descriptor(35, descending, 3, 0),
        descriptor(36, too_long, 2, 0),
        descriptor(37, nan_point, 2, 0),
        descriptor(38, inf_point, 2, 0),
        descriptor(39, good, 2, 2),
        descriptor(0, good, 2, 0),
    };
    for (size_t i = 0; i < sizeof values / sizeof *values; ++i)
        expect_malformed(&values[i], i + 1, "malformed descriptor fails quiet and is consumed");

    TOTKHapticCueDescriptor mismatch = descriptor(40, good, 2, 0);
    TOTKHapticCueState state = {0};
    const uint32_t epoch = 5;
    const uint64_t at = UINT64_C(4000000000);
    TOTKHapticCueOutput value = totk_haptic_cue_compose(
        0.0f, 0.0f, &mismatch, event(41, epoch, 50, at), gate(epoch, at, 0),
        epoch, at, 1.0f, 1, &state);
    expect(!value.cue_active && state.has_seen && !state.active,
           "descriptor and exact event cue IDs must match");

    TOTKHapticCuePoint malformed_points[] = {
        {0, 0.5f, 0.5f}, {20, 0.2f, 0.5f}, {20, 0.0f, 0.5f}};
    TOTKHapticCueDescriptor malformed = descriptor(42, malformed_points, 3, 0);
    TOTKHapticCueDescriptor repaired = descriptor(42, good, 2, 0);
    TOTKHapticCueState repaired_state = {0};
    TOTKHapticCueOutput rejected = totk_haptic_cue_compose(
        0.0f, 0.0f, &malformed, event(42, epoch, 51, at), gate(epoch, at, 0),
        epoch, at, 1.0f, 1, &repaired_state);
    TOTKHapticCueOutput replay = totk_haptic_cue_compose(
        0.0f, 0.0f, &repaired, event(42, epoch, 51, at), gate(epoch, at, 0),
        epoch, at + 1, 1.0f, 1, &repaired_state);
    expect(!rejected.cue_active && !replay.cue_active && !repaired_state.active,
           "repairing a malformed descriptor cannot replay an already consumed sequence");
}

static void test_event_freshness_and_no_replay(void) {
    const uint32_t epoch = 8;
    const uint64_t now = UINT64_C(5000000000);
    const TOTKHapticCuePoint points[] = {{0, 0.6f, 0.4f}, {100, 0.0f, 0.4f}};
    const TOTKHapticCueDescriptor cue = descriptor(50, points, 2, 0);

    const TOTKHapticCueEvent invalid[] = {
        {50, epoch, 1, 0},
        {50, epoch, 2, now + 1},
        {50, epoch, 3, now - TOTK_HAPTIC_CUE_EVENT_FRESH_NS},
    };
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; ++i) {
        TOTKHapticCueState state = {0};
        TOTKHapticCueOutput first = totk_haptic_cue_compose(
            0.0f, 0.0f, &cue, invalid[i], gate(epoch, now, 0), epoch, now, 1.0f,
            1, &state);
        TOTKHapticCueOutput replay = totk_haptic_cue_compose(
            0.0f, 0.0f, &cue, invalid[i], gate(epoch, now, 0), epoch, now - 1,
            1.0f, 1, &state);
        expect(!first.cue_active && !replay.cue_active && state.has_seen && !state.active,
               "zero, future, and stale-boundary events are consumed without replay");
    }

    TOTKHapticCueState future_state = {0};
    const TOTKHapticCueEvent future = event(50, epoch, 5, now + 1);
    TOTKHapticCueOutput future_first = totk_haptic_cue_compose(
        0.0f, 0.0f, &cue, future, gate(epoch, now, 0), epoch, now, 1.0f, 1,
        &future_state);
    TOTKHapticCueOutput future_caught_up = totk_haptic_cue_compose(
        0.0f, 0.0f, &cue, future, gate(epoch, now + 2, 0), epoch, now + 2,
        1.0f, 1, &future_state);
    expect(!future_first.cue_active && !future_caught_up.cue_active &&
               future_state.seen_sequence == 5,
           "a rejected future cue does not start when wall time later catches up");

    TOTKHapticCueState inside_state = {0};
    TOTKHapticCueOutput inside = totk_haptic_cue_compose(
        0.0f, 0.0f, &cue,
        event(50, epoch, 4, now - TOTK_HAPTIC_CUE_EVENT_FRESH_NS + 1),
        gate(epoch, now, 0), epoch, now, 1.0f, 1, &inside_state);
    expect(inside.cue_active, "one nanosecond inside the event freshness window is accepted");
}

static void test_gameplay_gate_and_trusted_ui(void) {
    const uint32_t epoch = 9;
    const uint64_t now = UINT64_C(6000000000);
    const TOTKHapticCuePoint points[] = {{0, 0.5f, 0.6f}, {200, 0.0f, 0.6f}};
    const TOTKHapticCueDescriptor gameplay =
        descriptor(60, points, 2, TOTK_HAPTIC_CUE_GAMEPLAY);
    const TOTKHapticCueDescriptor trusted_ui = descriptor(
        61, points, 2, TOTK_HAPTIC_CUE_TRUSTED_UI_ALLOW_PAUSED);

    TOTKHapticCueState paused_state = {0};
    TOTKHapticCueOutput paused = totk_haptic_cue_compose(
        0.0f, 0.0f, &gameplay, event(60, epoch, 1, now), gate(epoch, now, 1),
        epoch, now, 1.0f, 1, &paused_state);
    TOTKHapticCueOutput after_unpause = totk_haptic_cue_compose(
        0.0f, 0.0f, &gameplay, event(60, epoch, 1, now), gate(epoch, now, 0),
        epoch, now + 1, 1.0f, 1, &paused_state);
    expect(!paused.cue_active && !after_unpause.cue_active && paused_state.has_seen,
           "a gameplay cue rejected while paused cannot replay after unpause");

    TOTKHapticCueState ui_state = {0};
    TOTKHapticCueOutput ui = totk_haptic_cue_compose(
        0.0f, 0.0f, &trusted_ui, event(61, epoch, 2, now),
        (TOTKHapticCueGate){epoch, 0, 0, 1}, epoch, now, 1.0f, 1, &ui_state);
    expect(ui.cue_active && ui.intensity == 0.5f,
           "an exact trusted UI cue may play while paused without a Player gate");
    TOTKHapticCueOutput ui_continues = totk_haptic_cue_compose(
        0.0f, 0.0f, NULL, event(61, epoch, 2, now),
        (TOTKHapticCueGate){epoch, 0, 0, 1}, epoch,
        now + UINT64_C(50000000), 1.0f, 1, &ui_state);
    expect(ui_continues.cue_active && nearf(ui_continues.intensity, 0.375f, 0.00001f),
           "trusted UI playback remains independent of later paused Player gate state");

    TOTKHapticCueState stale_gate_state = {0};
    TOTKHapticCueOutput stale_gate = totk_haptic_cue_compose(
        0.0f, 0.0f, &gameplay, event(60, epoch, 3, now),
        gate(epoch, now - TOTK_HAPTIC_CUE_GAMEPLAY_GATE_LEASE_NS, 0), epoch, now,
        1.0f, 1, &stale_gate_state);
    expect(!stale_gate.cue_active && stale_gate_state.has_seen,
           "the exact gameplay gate lease boundary rejects and consumes a cue");

    TOTKHapticCueState inside_gate_state = {0};
    TOTKHapticCueOutput inside_gate = totk_haptic_cue_compose(
        0.0f, 0.0f, &gameplay, event(60, epoch, 4, now),
        gate(epoch, now - TOTK_HAPTIC_CUE_GAMEPLAY_GATE_LEASE_NS + 1, 0), epoch,
        now, 1.0f, 1, &inside_gate_state);
    expect(inside_gate.cue_active,
           "one nanosecond inside the gameplay gate lease is accepted");

    TOTKHapticCueOutput gate_lost = totk_haptic_cue_compose(
        0.0f, 0.0f, NULL, event(60, epoch, 4, now), (TOTKHapticCueGate){0}, epoch,
        now + UINT64_C(1000000), 1.0f, 1, &inside_gate_state);
    expect(!gate_lost.cue_active && !inside_gate_state.active,
           "a gameplay envelope stops immediately when its authoritative gate is lost");
}

static void test_foreground_epoch_and_sequence_lifecycle(void) {
    const uint32_t epoch = 10;
    const uint64_t now = UINT64_C(7000000000);
    const TOTKHapticCuePoint points_a[] = {{0, 0.4f, 0.2f}, {300, 0.0f, 0.2f}};
    const TOTKHapticCuePoint points_b[] = {{0, 0.8f, 0.8f}, {300, 0.0f, 0.8f}};
    const TOTKHapticCueDescriptor cue_a = descriptor(70, points_a, 2, 1);
    const TOTKHapticCueDescriptor cue_b = descriptor(71, points_b, 2, 1);

    TOTKHapticCueState state = {0};
    TOTKHapticCueOutput background = totk_haptic_cue_compose(
        0.0f, 0.0f, &cue_a, event(70, epoch, 1, now), (TOTKHapticCueGate){0},
        epoch, now, 1.0f, 0, &state);
    TOTKHapticCueOutput same_after_foreground = totk_haptic_cue_compose(
        0.0f, 0.0f, &cue_a, event(70, epoch, 1, now), (TOTKHapticCueGate){0},
        epoch, now + 1, 1.0f, 1, &state);
    expect(!background.cue_active && !same_after_foreground.cue_active && state.has_seen,
           "a cue seen in the background cannot replay in the same epoch");

    TOTKHapticCueOutput fresh = totk_haptic_cue_compose(
        0.0f, 0.0f, &cue_a, event(70, epoch, 2, now + 2), (TOTKHapticCueGate){0},
        epoch, now + 2, 1.0f, 1, &state);
    expect(fresh.cue_active && state.active_cue_id == 70,
           "a newer foreground sequence starts one active cue slot");

    TOTKHapticCueOutput older = totk_haptic_cue_compose(
        0.0f, 0.0f, &cue_b, event(71, epoch, 1, now + 3), (TOTKHapticCueGate){0},
        epoch, now + 3, 1.0f, 1, &state);
    expect(older.cue_active && state.active_cue_id == 70,
           "an out-of-order sequence cannot replace the active cue");

    TOTKHapticCueOutput replaced = totk_haptic_cue_compose(
        0.0f, 0.0f, &cue_b, event(71, epoch, 3, now + 4), (TOTKHapticCueGate){0},
        epoch, now + 4, 1.0f, 1, &state);
    expect(replaced.cue_active && state.active_cue_id == 71 &&
               state.active_sequence == 3 && replaced.intensity == 0.8f,
           "a newer sequence replaces rather than stacks the per-handle cue");

    TOTKHapticCueOutput old_epoch = totk_haptic_cue_compose(
        0.0f, 0.0f, &cue_b, event(71, epoch, 4, now + 5), (TOTKHapticCueGate){0},
        epoch + 1, now + 5, 1.0f, 1, &state);
    expect(!old_epoch.cue_active && state.epoch == epoch + 1 && !state.active,
           "advancing the foreground epoch clears the active cue and rejects old events");

    TOTKHapticCueOutput new_epoch = totk_haptic_cue_compose(
        0.0f, 0.0f, &cue_b, event(71, epoch + 1, 1, now + 6),
        (TOTKHapticCueGate){0}, epoch + 1, now + 6, 1.0f, 1, &state);
    expect(new_epoch.cue_active && state.seen_sequence == 1,
           "a matching new epoch may restart its own sequence numbering");
}

static void test_invalid_runtime_values_and_independent_hands(void) {
    const uint32_t epoch = 12;
    const uint64_t now = UINT64_C(8000000000);
    const TOTKHapticCuePoint points[] = {{0, 0.5f, 0.7f}, {100, 0.0f, 0.7f}};
    const TOTKHapticCueDescriptor cue = descriptor(80, points, 2, 1);

    TOTKHapticCueState bad_gain_state = {0};
    TOTKHapticCueOutput bad_gain = totk_haptic_cue_compose(
        NAN, INFINITY, &cue, event(80, epoch, 1, now), (TOTKHapticCueGate){0},
        epoch, now, NAN, 1, &bad_gain_state);
    expect(bad_gain.intensity == 0.0f && bad_gain.sharpness == 0.0f &&
               bad_gain_state.has_seen && !bad_gain_state.active,
           "nonfinite base and gain values are bounded and fail quiet");

    TOTKHapticCueState bad_foreground_state = {0};
    TOTKHapticCueOutput bad_foreground = totk_haptic_cue_compose(
        2.0f, -1.0f, &cue, event(80, epoch, 2, now), (TOTKHapticCueGate){0},
        epoch, now, 1.0f, 2, &bad_foreground_state);
    expect(bad_foreground.intensity == 1.0f && bad_foreground.sharpness == 0.0f &&
               !bad_foreground.cue_active && bad_foreground_state.has_seen,
           "base is clamped and malformed foreground state consumes the cue");

    TOTKHapticCueState left = {0}, right = {0};
    TOTKHapticCueOutput left_value = totk_haptic_cue_compose(
        0.0f, 0.0f, &cue, event(80, epoch, 3, now), (TOTKHapticCueGate){0},
        epoch, now, 1.0f, 1, &left);
    TOTKHapticCueOutput right_value = totk_haptic_cue_compose(
        0.6f, 0.1f, &cue, event(80, epoch, 3, now), (TOTKHapticCueGate){0},
        epoch, now, 1.0f, 1, &right);
    expect(left_value.cue_active && right_value.cue_active &&
               left_value.intensity == 0.5f && right_value.intensity == 0.6f &&
               left.seen_sequence == right.seen_sequence,
           "separate per-handle states admit the same exact event independently");

    totk_haptic_cue_state_reset(&left);
    expect(left.epoch == 0 && !left.has_seen && !left.active,
           "explicit state reset clears lifecycle and copied-envelope state");
}

int main(void) {
    test_linear_envelope_and_timestamp_anchor();
    test_max_composition_and_gain();
    test_point_sanitizing_and_bounds();
    test_malformed_descriptors();
    test_event_freshness_and_no_replay();
    test_gameplay_gate_and_trusted_ui();
    test_foreground_epoch_and_sequence_lifecycle();
    test_invalid_runtime_values_and_independent_hands();

    if (failures) {
        fprintf(stderr, "%d haptic cue test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    puts("haptic cue policy tests passed");
    return EXIT_SUCCESS;
}
