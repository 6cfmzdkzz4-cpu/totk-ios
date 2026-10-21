/* Contract tests for generated exact-audio haptic descriptors and mappings. */
#include "haptic_cues.h"
#include "gameplay_extra_audio.h"
#include "gameplay_haptic_patterns.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

enum {
    CUE_START = 1,
    CUE_DIALOGUE = 2,
    CUE_WATER_DASH = 3,
    CUE_CLIMB = 4,
    CUE_CHARGE = 5,
    CUE_SWING = 6,
    CUE_THRUST = 7,
    CUE_CLIMB_DASH = 8,
    CUE_CATEGORY_COUNT = 8
};

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

static float smooth_unit(float value) {
    return value * value * (3.0f - 2.0f * value);
}

static unsigned descriptor_category(unsigned cue_id) {
    unsigned category = 0;
    for (size_t i = 0; i < EXTRA_AUDIO_FINGERPRINT_COUNT; ++i) {
        if (extra_audio_fingerprints[i].cue_id != cue_id)
            continue;
        if (!category)
            category = extra_audio_fingerprints[i].category;
        else
            expect(category == extra_audio_fingerprints[i].category,
                   "all fingerprints for one descriptor retain one category");
    }
    return category;
}

static const TOTKHapticCueDescriptor *find_descriptor_category(unsigned category) {
    for (size_t i = 0; i < EXTRA_AUDIO_FINGERPRINT_COUNT; ++i) {
        if (extra_audio_fingerprints[i].category != category)
            continue;
        const unsigned cue_id = extra_audio_fingerprints[i].cue_id;
        if (cue_id > 0 && cue_id <= EXTRA_CUE_DESCRIPTOR_COUNT)
            return &extra_cue_descriptors[cue_id - 1];
    }
    return NULL;
}

static void test_generated_table_contract(void) {
    expect(EXTRA_CUE_DESCRIPTOR_COUNT > 0,
           "the generated descriptor table is nonempty");
    expect(EXTRA_AUDIO_FINGERPRINT_COUNT >= EXTRA_CUE_DESCRIPTOR_COUNT,
           "the generated table has at least one exact fingerprint per descriptor");

    unsigned allow_paused = 0;
    for (size_t i = 0; i < EXTRA_CUE_DESCRIPTOR_COUNT; ++i) {
        const TOTKHapticCueDescriptor *descriptor = &extra_cue_descriptors[i];
        expect(descriptor->cue_id == i + 1,
               "descriptor IDs remain contiguous for cue_id minus one lookup");
        expect(totk_haptic_cue_descriptor_valid(descriptor, (uint32_t)i + 1),
               "every generated descriptor satisfies the bounded policy contract");
        if (descriptor->allow_paused)
            allow_paused++;
    }
    expect(allow_paused == 1,
           "only the exact trusted title-start descriptor can bypass gameplay pause");

    unsigned category_counts[CUE_CATEGORY_COUNT + 1] = {0};
    unsigned category_by_cue[EXTRA_CUE_DESCRIPTOR_COUNT + 1] = {0};
    unsigned locality_by_cue[EXTRA_CUE_DESCRIPTOR_COUNT + 1] = {0};
    unsigned seen_cues[EXTRA_CUE_DESCRIPTOR_COUNT + 1] = {0};
    for (size_t i = 0; i < EXTRA_AUDIO_FINGERPRINT_COUNT; ++i) {
        const unsigned cue_id = extra_audio_fingerprints[i].cue_id;
        const unsigned category = extra_audio_fingerprints[i].category;
        const unsigned locality = extra_audio_fingerprints[i].locality;
        expect(cue_id > 0 && cue_id <= EXTRA_CUE_DESCRIPTOR_COUNT,
               "every exact fingerprint maps to a generated descriptor");
        expect(category >= CUE_START && category <= CUE_CATEGORY_COUNT,
               "every exact fingerprint has a bounded category");
        expect(locality >= 1 && locality <= 3,
               "every exact fingerprint has a bounded handle locality");
        if (!cue_id || cue_id > EXTRA_CUE_DESCRIPTOR_COUNT ||
            category > CUE_CATEGORY_COUNT)
            continue;
        category_counts[category]++;
        if (category_by_cue[cue_id]) {
            expect(category_by_cue[cue_id] == category,
                   "duplicate voices for one cue retain one category");
            expect(locality_by_cue[cue_id] == locality,
                   "duplicate voices for one cue retain one locality");
        } else {
            category_by_cue[cue_id] = category;
            locality_by_cue[cue_id] = locality;
        }
        seen_cues[cue_id] = 1;

        for (size_t j = 0; j < i; ++j)
            expect(memcmp(extra_audio_fingerprints[i].prefix,
                          extra_audio_fingerprints[j].prefix,
                          sizeof extra_audio_fingerprints[i].prefix) != 0,
                   "generated 128-byte exact fingerprints remain unique");
    }

    for (unsigned category = CUE_START; category <= CUE_CATEGORY_COUNT; ++category)
        expect(category_counts[category] > 0,
               "every reviewed haptic category has an exact generated fingerprint");
    for (unsigned cue_id = 1; cue_id <= EXTRA_CUE_DESCRIPTOR_COUNT; ++cue_id) {
        expect(seen_cues[cue_id], "every descriptor is reachable from an exact fingerprint");
        expect(descriptor_category(cue_id) == category_by_cue[cue_id],
               "descriptor category lookup follows the generated fingerprints");
        expect(extra_cue_descriptors[cue_id - 1].allow_paused ==
                   (category_by_cue[cue_id] == CUE_START),
               "trusted UI pause authority belongs only to the exact start cue");
    }
}

static void test_exact_start_descriptor(void) {
    const TOTKHapticCueDescriptor *start = find_descriptor_category(CUE_START);
    expect(start != NULL, "the exact start category has a generated descriptor");
    if (!start)
        return;
    expect(start->point_count == 343 &&
               start->points[start->point_count - 1].offset_ms == 6840 &&
               start->allow_paused == TOTK_HAPTIC_CUE_TRUSTED_UI_ALLOW_PAUSED,
           "the six-channel title cue maps to the reviewed 6.84-second envelope");

    const uint32_t epoch = 9;
    const uint64_t at = UINT64_C(1000000000);
    TOTKHapticCueState state = {0};
    const TOTKHapticCueEvent event = {start->cue_id, epoch, 1, at};
    TOTKHapticCueOutput value = totk_haptic_cue_compose(
        0.1f, 0.7f, start, event, (TOTKHapticCueGate){0}, epoch,
        at + UINT64_C(80000000), 1.0f, 1, &state);
    expect(value.cue_active && nearf(value.intensity, 0.24f, 0.000001f) &&
               nearf(value.sharpness, 0.129816f, 0.000001f),
           "trusted start plays at its event-anchored generated sample without Player context");

    value = totk_haptic_cue_compose(
        0.0f, 0.0f, NULL, event,
        (TOTKHapticCueGate){.epoch = epoch, .timestamp_ns = at, .valid = 0, .paused = 1},
        epoch, at + UINT64_C(1600000000), 1.0f, 1, &state);
    expect(value.cue_active,
           "an admitted exact start continues through absent and paused gameplay context");

    value = totk_haptic_cue_compose(
        0.12f, 0.4f, NULL, event, (TOTKHapticCueGate){0}, epoch,
        at + UINT64_C(6840000000), 1.0f, 1, &state);
    expect(!value.cue_active && !state.active && value.intensity == 0.12f &&
               value.sharpness == 0.4f,
           "the generated start envelope stops at its exact final offset and preserves base");

    TOTKHapticCueState background = {0};
    value = totk_haptic_cue_compose(
        0.0f, 0.0f, start, (TOTKHapticCueEvent){start->cue_id, epoch, 2, at},
        (TOTKHapticCueGate){0}, epoch, at, 1.0f, 0, &background);
    expect(!value.cue_active && background.has_seen && !background.active,
           "the exact start sequence is consumed without delayed replay outside foreground");
}

static void test_charge_pitch_transform(void) {
    const TOTKHapticCueDescriptor *source = find_descriptor_category(CUE_CHARGE);
    expect(source != NULL, "the exact charge category has a generated descriptor");
    if (!source)
        return;
    expect(source->point_count == 15 &&
               source->points[source->point_count - 1].offset_ms == 280 &&
               source->allow_paused == TOTK_HAPTIC_CUE_GAMEPLAY,
           "the exact charge cue retains its reviewed source envelope");

    const float pitches[] = {1.0f, 1.05f, 1.12f};
    const uint32_t durations[] = {280, 267, 250};
    const float peak_intensities[] = {0.18f, 0.2115f, 0.2556f};
    const float peak_sharpness[] = {0.376009f, 0.466009f, 0.592009f};
    for (size_t stage = 0; stage < sizeof pitches / sizeof *pitches; ++stage) {
        TOTKHapticCuePoint points[TOTK_HAPTIC_CUE_MAX_POINTS] = {{0}};
        TOTKHapticCueDescriptor transformed = {0};
        expect(totk_haptic_cue_shape_sound(
                   &transformed, source, pitches[stage], 1, points,
                   TOTK_HAPTIC_CUE_MAX_POINTS),
               "the policy shapes each source-proven charge pitch");
        expect(totk_haptic_cue_descriptor_valid(&transformed, source->cue_id),
               "each verified charge pitch keeps strictly increasing bounded offsets");
        expect(points[14].offset_ms == durations[stage],
               "charge duration follows the actual authored playback pitch");
        expect(nearf(points[12].intensity, peak_intensities[stage], 0.000002f) &&
                   nearf(points[12].sharpness, peak_sharpness[stage], 0.000002f),
               "charge intensity and sharpness follow the reviewed three-stage transform");
    }

    TOTKHapticCuePoint points[TOTK_HAPTIC_CUE_MAX_POINTS] = {{0}};
    TOTKHapticCueDescriptor high = {0};
    expect(totk_haptic_cue_shape_sound(
               &high, source, 1.12f, 1, points, TOTK_HAPTIC_CUE_MAX_POINTS),
           "the actual helper produces the high charge stage for admission");
    const uint32_t epoch = 12;
    const uint64_t at = UINT64_C(3000000000);
    TOTKHapticCueState state = {0};
    const TOTKHapticCueEvent event = {source->cue_id, epoch, 7, at};
    const TOTKHapticCueGate gate = {epoch, at, 1, 0};
    TOTKHapticCueOutput admitted = totk_haptic_cue_compose(
        0.0f, 0.0f, &high, event, gate, epoch, at, 1.0f, 1, &state);
    expect(admitted.cue_active,
           "the transformed charge descriptor is admitted only with fresh gameplay context");
    TOTKHapticCueOutput peak = totk_haptic_cue_compose(
        0.05f, 0.1f, NULL, event,
        (TOTKHapticCueGate){epoch, at + UINT64_C(210000000), 1, 0}, epoch,
        at + (uint64_t)points[12].offset_ms * UINT64_C(1000000), 1.0f, 1, &state);
    expect(peak.cue_active && nearf(peak.intensity, 0.2556f, 0.000002f) &&
               nearf(peak.sharpness, 0.592009f, 0.000002f),
           "the transformed charge envelope composes at its event-anchored peak");
}

static void test_water_dash_shapes(void) {
    unsigned descriptors = 0;
    for (size_t i = 0; i < EXTRA_CUE_DESCRIPTOR_COUNT; ++i) {
        const TOTKHapticCueDescriptor *cue = &extra_cue_descriptors[i];
        if (descriptor_category(cue->cue_id) != CUE_WATER_DASH)
            continue;
        descriptors++;
        expect(cue->point_count == 46 && cue->points[45].offset_ms == 360,
               "water dash uses 46 eight-millisecond points over 360 ms");
        if (cue->point_count != 46)
            continue;
        float peak = 0.0f;
        for (size_t point = 0; point < cue->point_count; ++point) {
            const TOTKHapticCuePoint value = cue->points[point];
            expect(value.offset_ms == point * 8,
                   "water dash points retain an exact eight-millisecond cadence");
            expect(value.intensity >= 0.0f && value.intensity <= 0.095002f,
                   "water dash intensity stays within its softened bound");
            expect(value.sharpness >= 0.70f && value.sharpness <= 0.85f,
                   "water dash grain stays within the fine-edge band");
            if (value.offset_ms < 32)
                expect(value.intensity <=
                           0.095002f * smooth_unit((float)value.offset_ms / 32.0f),
                       "water dash respects the smooth 32 ms attack window");
            if (value.offset_ms > 280)
                expect(value.intensity <=
                           0.095002f *
                               smooth_unit((float)(360 - value.offset_ms) / 80.0f),
                       "water dash respects the smooth 80 ms release window");
            if (value.intensity > peak)
                peak = value.intensity;
        }
        expect(cue->points[0].intensity == 0.0f &&
                   cue->points[45].intensity == 0.0f,
               "water dash starts and ends at zero intensity");
        expect(peak >= 0.09f && peak <= 0.095002f,
               "water dash uses the reviewed softened peak range");
    }
    expect(descriptors > 0, "the generated table contains water dash descriptors");
}

static void expect_category_bounds(unsigned category,
                                   float maximum_intensity,
                                   uint32_t maximum_duration_ms,
                                   const char *message) {
    unsigned descriptors = 0;
    for (size_t i = 0; i < EXTRA_CUE_DESCRIPTOR_COUNT; ++i) {
        const TOTKHapticCueDescriptor *cue = &extra_cue_descriptors[i];
        if (descriptor_category(cue->cue_id) != category)
            continue;
        descriptors++;
        float peak = 0.0f;
        for (size_t point = 0; point < cue->point_count; ++point)
            peak = fmaxf(peak, cue->points[point].intensity);
        expect(peak > 0.0f && peak <= maximum_intensity + 0.000002f &&
                   cue->points[cue->point_count - 1].offset_ms <=
                       maximum_duration_ms,
               message);
    }
    expect(descriptors > 0, "the reviewed category has generated descriptors");
}

static void test_dry_melee_and_climb_dash_bounds(void) {
    expect_category_bounds(CUE_SWING, 0.10f, 180,
                           "dry swing cues remain subtle and at most 180 ms");
    expect_category_bounds(CUE_THRUST, 0.10f, 180,
                           "dry thrust cues remain subtle and at most 180 ms");
    expect_category_bounds(CUE_CLIMB_DASH, 0.16f, 200,
                           "climb dash cues remain bounded and at most 200 ms");
}

static void expect_shape_failure(const TOTKHapticCueDescriptor *source,
                                 float pitch,
                                 int charge,
                                 TOTKHapticCuePoint *scratch,
                                 size_t capacity,
                                 const char *message) {
    TOTKHapticCueDescriptor output = {99, scratch, 99, 1};
    expect(!totk_haptic_cue_shape_sound(
               &output, source, pitch, charge, scratch, capacity) &&
               output.cue_id == 0 && output.points == NULL &&
               output.point_count == 0 && output.allow_paused == 0,
           message);
}

static void test_sound_shape_validation(void) {
    const TOTKHapticCueDescriptor *source = &extra_cue_descriptors[1];
    TOTKHapticCuePoint unchanged[TOTK_HAPTIC_CUE_MAX_POINTS] = {{0}};
    TOTKHapticCueDescriptor output = {0};
    expect(totk_haptic_cue_shape_sound(
               &output, source, 1.0f, 0, unchanged,
               TOTK_HAPTIC_CUE_MAX_POINTS),
           "an ordinary unity-pitch cue is copied by the real shaping helper");
    expect(output.points == unchanged && output.point_count == source->point_count &&
               memcmp(output.points, source->points,
                      source->point_count * sizeof *source->points) == 0,
           "ordinary unity pitch preserves every generated point exactly");

    TOTKHapticCuePoint scratch[TOTK_HAPTIC_CUE_MAX_POINTS] = {{0}};
    expect_shape_failure(source, 0.749f, 0, scratch,
                         TOTK_HAPTIC_CUE_MAX_POINTS,
                         "pitch below the supported range fails with zero output");
    expect_shape_failure(source, 4.001f, 0, scratch,
                         TOTK_HAPTIC_CUE_MAX_POINTS,
                         "pitch above the supported range fails with zero output");
    expect_shape_failure(source, NAN, 0, scratch,
                         TOTK_HAPTIC_CUE_MAX_POINTS,
                         "nonfinite pitch fails with zero output");
    expect_shape_failure(source, 1.0f, 2, scratch,
                         TOTK_HAPTIC_CUE_MAX_POINTS,
                         "an invalid charge flag fails with zero output");
    expect_shape_failure(source, 1.0f, 0, scratch, source->point_count - 1,
                         "insufficient scratch capacity fails with zero output");
    expect_shape_failure(source, 1.0f, 0, NULL,
                         TOTK_HAPTIC_CUE_MAX_POINTS,
                         "missing scratch storage fails with zero output");

    const TOTKHapticCuePoint close_points[] = {
        {0, 0.2f, 0.3f}, {1, 0.3f, 0.4f}, {2, 0.0f, 0.4f}};
    const TOTKHapticCueDescriptor close = {250, close_points, 3, 0};
    expect_shape_failure(&close, 4.0f, 0, scratch,
                         TOTK_HAPTIC_CUE_MAX_POINTS,
                         "rounded duplicate offsets fail with zero output");

    const TOTKHapticCuePoint long_points[] = {
        {0, 0.2f, 0.3f}, {10000, 0.0f, 0.3f}};
    const TOTKHapticCueDescriptor long_cue = {251, long_points, 2, 0};
    expect_shape_failure(&long_cue, 0.75f, 0, scratch,
                         TOTK_HAPTIC_CUE_MAX_POINTS,
                         "pitch-expanded duration beyond ten seconds fails with zero output");
}

int main(void) {
    test_generated_table_contract();
    test_exact_start_descriptor();
    test_charge_pitch_transform();
    test_water_dash_shapes();
    test_dry_melee_and_climb_dash_bounds();
    test_sound_shape_validation();
    if (failures) {
        fprintf(stderr, "%d generated haptic test(s) failed\n", failures);
        return 1;
    }
    puts("generated haptic descriptor tests passed");
    return 0;
}
