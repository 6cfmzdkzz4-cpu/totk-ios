/* Deterministic tests for authoritative sustained-skydive wind policy. */
#include "../runtime/skydive_feedback.h"

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

static TOTKSkydiveFeedbackSample full_sample(uint64_t now_ns) {
    TOTKSkydiveFeedbackSample sample = {
        .timestamp_ns = now_ns,
        .started_ns = now_ns - UINT64_C(2000000000),
        .downward_speed_mps = 40.0f,
        .descent_m = 18.0f,
        .valid = 1,
        .paused = 0,
        .skydiving = 1,
    };
    return sample;
}

static void expect_base_only(TOTKSkydiveFeedbackSample sample,
                             uint64_t now_ns,
                             float gain,
                             int active,
                             unsigned handle,
                             const char *message) {
    TOTKSkydiveFeedbackOutput output = totk_skydive_feedback_compose(
        0.25f, 0.35f, sample, now_ns, gain, active, handle);
    expect(output.intensity == 0.25f && output.sharpness == 0.35f &&
               !output.wind_active,
           message);
}

static void test_authoritative_thresholds(void) {
    const uint64_t now = UINT64_C(10000000000);
    TOTKSkydiveFeedbackSample sample = full_sample(now);

    sample.skydiving = 0;
    expect_base_only(sample, now, 1.0f, 1, 0,
                     "an ordinary fall or jump cannot activate without exact skydive state");

    sample = full_sample(now);
    sample.started_ns = now - TOTK_SKYDIVE_MIN_DURATION_NS;
    expect_base_only(sample, now, 1.0f, 1, 0,
                     "the exact 800 ms duration boundary remains quiet");
    sample.started_ns++;
    expect_base_only(sample, now, 1.0f, 1, 0,
                     "less than 800 ms of exact skydive time remains quiet");

    sample = full_sample(now);
    sample.descent_m = 10.0f;
    expect_base_only(sample, now, 1.0f, 1, 0,
                     "the exact ten-metre descent boundary remains quiet");
    sample.descent_m = 9.999f;
    expect_base_only(sample, now, 1.0f, 1, 0,
                     "a shallow skydive remains quiet");

    sample = full_sample(now);
    sample.downward_speed_mps = 8.0f;
    expect_base_only(sample, now, 1.0f, 1, 0,
                     "the exact downward-speed boundary remains quiet");
    sample.downward_speed_mps = 7.999f;
    expect_base_only(sample, now, 1.0f, 1, 0,
                     "slow or hovering skydive state remains quiet");
}

static void test_memoryless_smooth_ramp(void) {
    const uint64_t now = UINT64_C(12000000000);
    TOTKSkydiveFeedbackSample crossing = full_sample(now);
    crossing.descent_m = 10.01f;
    TOTKSkydiveFeedbackOutput first = totk_skydive_feedback_compose(
        0.0f, 0.0f, crossing, now, 1.0f, 1, 0);
    expect(first.wind_active && first.intensity > 0.0f && first.intensity < 0.00001f,
           "crossing ten metres fades in smoothly even after a long skydive");

    TOTKSkydiveFeedbackSample partial = full_sample(now);
    partial.started_ns = now - UINT64_C(1100000000);
    partial.descent_m = 14.0f;
    partial.downward_speed_mps = 24.0f;
    TOTKSkydiveFeedbackOutput middle = totk_skydive_feedback_compose(
        0.0f, 0.0f, partial, now, 1.0f, 1, 0);
    TOTKSkydiveFeedbackOutput full = totk_skydive_feedback_compose(
        0.0f, 0.0f, full_sample(now), now, 1.0f, 1, 0);
    expect(middle.wind_active && full.wind_active && middle.intensity > first.intensity &&
               full.intensity > middle.intensity,
           "the minimum of time, descent, and speed ramps grows monotonically at one grain");
    expect(full.intensity >= 0.052f && full.intensity <= 0.065f &&
               full.sharpness >= 0.68f && full.sharpness <= 0.86f,
           "fully developed wind stays within restrained intensity and fine-edge bounds");
}

static void test_engineering_speed_guard(void) {
    const uint64_t now = UINT64_C(13000000000);
    TOTKSkydiveFeedbackSample sample = full_sample(now);
    const TOTKSkydiveFeedbackOutput saturated = totk_skydive_feedback_compose(
        0.0f, 0.0f, sample, now, 1.0f, 1, 0);
    const float bounded_speeds[] = {81.0f, 150.0f,
                                    TOTK_SKYDIVE_MAX_DOWN_SPEED_MPS};
    for (size_t i = 0; i < sizeof bounded_speeds / sizeof *bounded_speeds; ++i) {
        sample = full_sample(now);
        sample.downward_speed_mps = bounded_speeds[i];
        const TOTKSkydiveFeedbackOutput output = totk_skydive_feedback_compose(
            0.0f, 0.0f, sample, now, 1.0f, 1, 0);
        expect(output.wind_active &&
                   nearf(output.intensity, saturated.intensity, 0.0000001f) &&
                   nearf(output.sharpness, saturated.sharpness, 0.0000001f) &&
                   output.intensity >= 0.052f && output.intensity <= 0.065f &&
                   output.sharpness >= 0.68f && output.sharpness <= 0.86f,
               "bounded high-speed observations retain the saturated restrained texture");
    }

    sample = full_sample(now);
    sample.downward_speed_mps = TOTK_SKYDIVE_MAX_DOWN_SPEED_MPS + 0.001f;
    expect_base_only(sample, now, 1.0f, 1, 0,
                     "speed above the engineering guard fails quiet");
}

static void test_freshness_lifecycle_and_invalids(void) {
    const uint64_t now = UINT64_C(14000000000);
    TOTKSkydiveFeedbackSample sample = full_sample(now);

    sample.timestamp_ns = 0;
    expect_base_only(sample, now, 1.0f, 1, 0, "a zero timestamp fails quiet");
    sample = full_sample(now);
    sample.timestamp_ns = now + 1;
    expect_base_only(sample, now, 1.0f, 1, 0, "a future sample fails quiet");
    sample = full_sample(now);
    sample.timestamp_ns = now - TOTK_SKYDIVE_CONTEXT_LEASE_NS;
    expect_base_only(sample, now, 1.0f, 1, 0,
                     "the 200 ms freshness boundary fails quiet");
    sample = full_sample(now);
    sample.timestamp_ns = now - TOTK_SKYDIVE_CONTEXT_LEASE_NS + 1;
    expect(totk_skydive_feedback_compose(0.0f, 0.0f, sample, now, 1.0f, 1, 0)
               .wind_active,
           "a sample just inside the freshness lease remains eligible");

    sample = full_sample(now);
    sample.started_ns = 0;
    expect_base_only(sample, now, 1.0f, 1, 0, "a missing authoritative start fails quiet");
    sample = full_sample(now);
    sample.started_ns = sample.timestamp_ns + 1;
    expect_base_only(sample, now, 1.0f, 1, 0, "a start after the sample fails quiet");

    sample = full_sample(now);
    sample.paused = 1;
    expect_base_only(sample, now, 1.0f, 1, 0, "pause immediately silences wind");
    sample = full_sample(now);
    sample.valid = 0;
    expect_base_only(sample, now, 1.0f, 1, 0, "invalid context immediately silences wind");
    sample = full_sample(now);
    sample.valid = 2;
    expect_base_only(sample, now, 1.0f, 1, 0, "malformed boolean state fails quiet");
    sample = full_sample(now);
    sample.paused = -1;
    expect_base_only(sample, now, 1.0f, 1, 0, "malformed pause state fails quiet");
    sample = full_sample(now);
    sample.skydiving = 2;
    expect_base_only(sample, now, 1.0f, 1, 0, "malformed skydive state fails quiet");

    const float invalid_speeds[] = {
        -1.0f, TOTK_SKYDIVE_MAX_DOWN_SPEED_MPS + 0.001f, NAN, INFINITY};
    for (size_t i = 0; i < sizeof invalid_speeds / sizeof *invalid_speeds; ++i) {
        sample = full_sample(now);
        sample.downward_speed_mps = invalid_speeds[i];
        expect_base_only(sample, now, 1.0f, 1, 0,
                         "invalid downward speed fails quiet");
    }
    const float invalid_descents[] = {-1.0f, 5000.001f, NAN, INFINITY};
    for (size_t i = 0; i < sizeof invalid_descents / sizeof *invalid_descents; ++i) {
        sample = full_sample(now);
        sample.descent_m = invalid_descents[i];
        expect_base_only(sample, now, 1.0f, 1, 0,
                         "invalid continuous descent fails quiet");
    }

    sample = full_sample(now);
    expect_base_only(sample, now, 1.0f, 0, 0, "inactive output fails quiet");
    expect_base_only(sample, now, 1.0f, 2, 0, "malformed active state fails quiet");
    expect_base_only(sample, now, 1.0f, 1, 2, "invalid handle index fails quiet");
    expect_base_only(sample, now, 0.0f, 1, 0, "zero gain mutes wind independently");
    expect_base_only(sample, now, -1.0f, 1, 0, "negative gain fails quiet");
    expect_base_only(sample, now, NAN, 1, 0, "nonfinite gain fails quiet");
    expect_base_only(sample, now, INFINITY, 1, 0, "infinite gain fails quiet");
}

static void test_grain_gain_and_max_composition(void) {
    const uint64_t now = UINT64_C(16008000000);
    TOTKSkydiveFeedbackSample sample = full_sample(now);
    TOTKSkydiveFeedbackOutput left = totk_skydive_feedback_compose(
        0.0f, 0.0f, sample, now, 1.0f, 1, 0);
    TOTKSkydiveFeedbackOutput right = totk_skydive_feedback_compose(
        0.0f, 0.0f, sample, now, 1.0f, 1, 1);
    expect(left.wind_active && right.wind_active &&
               (!nearf(left.intensity, right.intensity, 0.0000001f) ||
                !nearf(left.sharpness, right.sharpness, 0.0000001f)),
           "left and right handle grain is decorrelated");

    const uint64_t later = now + TOTK_SKYDIVE_GRAIN_NS;
    sample.timestamp_ns = later;
    TOTKSkydiveFeedbackOutput next = totk_skydive_feedback_compose(
        0.0f, 0.0f, sample, later, 1.0f, 1, 0);
    expect(next.wind_active &&
               (!nearf(left.intensity, next.intensity, 0.0000001f) ||
                !nearf(left.sharpness, next.sharpness, 0.0000001f)),
           "fine grain varies deterministically across 24 ms ticks");

    sample = full_sample(now);
    TOTKSkydiveFeedbackOutput doubled = totk_skydive_feedback_compose(
        0.0f, 0.0f, sample, now, 99.0f, 1, 0);
    expect(nearf(doubled.intensity, left.intensity * 2.0f, 0.000001f) &&
               doubled.sharpness == left.sharpness,
           "finite gain clamps to two without changing wind texture");

    TOTKSkydiveFeedbackOutput native_wins = totk_skydive_feedback_compose(
        0.8f, 0.2f, sample, now, 1.0f, 1, 0);
    expect(native_wins.wind_active && native_wins.intensity == 0.8f &&
               native_wins.sharpness == 0.2f,
           "a stronger native or semantic base wins without cancellation");
    TOTKSkydiveFeedbackOutput tie = totk_skydive_feedback_compose(
        left.intensity, 0.17f, sample, now, 1.0f, 1, 0);
    expect(tie.wind_active && tie.intensity == left.intensity && tie.sharpness == 0.17f,
           "an equal base preserves base intensity and sharpness");

    TOTKSkydiveFeedbackOutput sanitized = totk_skydive_feedback_compose(
        NAN, INFINITY, sample, now, 1.0f, 1, 0);
    expect(sanitized.wind_active && sanitized.intensity == left.intensity &&
               sanitized.sharpness == left.sharpness,
           "nonfinite base values sanitize without suppressing valid wind");
}

int main(void) {
    test_authoritative_thresholds();
    test_memoryless_smooth_ramp();
    test_engineering_speed_guard();
    test_freshness_lifecycle_and_invalids();
    test_grain_gain_and_max_composition();
    if (failures) {
        fprintf(stderr, "%d skydive feedback test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    puts("skydive feedback policy tests passed");
    return EXIT_SUCCESS;
}
