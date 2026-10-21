/* Read-only, version-checked gameplay observations for contextual controller feedback. */
#pragma once
#include "runtime.h"
#include "dualsense_feedback.h"
#include "haptic_cues.h"

typedef enum {
    RUNTIME_HAPTIC_CUE_START = 1,
    RUNTIME_HAPTIC_CUE_DIALOGUE = 2,
    RUNTIME_HAPTIC_CUE_WATER_DASH = 3,
    RUNTIME_HAPTIC_CUE_CLIMB = 4,
    RUNTIME_HAPTIC_CUE_CHARGE = 5,
    RUNTIME_HAPTIC_CUE_SWING = 6,
    RUNTIME_HAPTIC_CUE_THRUST = 7,
    RUNTIME_HAPTIC_CUE_CLIMB_DASH = 8,
    RUNTIME_HAPTIC_CUE_CATEGORY_COUNT = 9
} RuntimeHapticCueCategory;

enum {
    RUNTIME_HAPTIC_CUE_LEFT = 1,
    RUNTIME_HAPTIC_CUE_RIGHT = 2,
    RUNTIME_HAPTIC_CUE_BOTH = 3
};

typedef struct {
    TOTKHapticCueEvent event;
    float pitch;
    unsigned category;
    unsigned locality;
} RuntimeHapticCueObservation;

typedef struct {
    uint64_t frames, player_frames, read_failures;
    uint64_t npc_events, landing_events, block_events, melee_events;
    int grounded, shield_attached, controller_paused, menu_paused, rumble_paused;
    unsigned guard_cue_fingerprints;
    unsigned melee_cue_fingerprints;
    int melee_drawn, bow_drawn, bow_ready;
    int swimming, climbing, climb_dashing, charging, melee_attacking;
    int skydiving;
    unsigned action_verb, sub_action_verb;
    uint64_t skydive_started_ns;
    float skydive_downward_speed_mps, skydive_descent_m;
    uint64_t cue_events[RUNTIME_HAPTIC_CUE_CATEGORY_COUNT];
    uint64_t cue_matches[RUNTIME_HAPTIC_CUE_CATEGORY_COUNT];
    uint64_t cue_state_rejects[RUNTIME_HAPTIC_CUE_CATEGORY_COUNT];
    uint64_t cue_queue_drops;
    unsigned last_cue_id;
    float last_cue_pitch;
    unsigned last_rejected_cue_id, last_rejected_action_verb;
    unsigned npc_event_ids;
    int event_active, hard_paused;
    char event_name[128];
    char action[64];
    char detail[96];
} RuntimeGameplayFeedbackEvidence;

void runtime_gameplay_feedback_init(const Module *module);
void runtime_gameplay_feedback_set_active(int active);
void runtime_gameplay_feedback_tick(uint64_t now_ns);
void runtime_gameplay_feedback_audio(uint64_t address,uint64_t size,uint64_t now_ns);
void runtime_gameplay_feedback_audio_cue(uint64_t address,uint64_t size,uint64_t now_ns,float pitch);
unsigned runtime_gameplay_feedback_cues(RuntimeHapticCueObservation *out,unsigned capacity,unsigned *epoch);
void runtime_gameplay_feedback_read(TOTKSemanticContext *context, TOTKSemanticEvents *events,
                                   RuntimeGameplayFeedbackEvidence *evidence);
