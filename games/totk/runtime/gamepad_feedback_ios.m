/* iOS controller feedback: native HD-rumble bands, localized haptics, reactive DualSense triggers, and motion aiming. */
#include <TargetConditionals.h>
#if TARGET_OS_IOS
#import <UIKit/UIKit.h>
#import <GameController/GameController.h>
#import <CoreHaptics/CoreHaptics.h>
#include <pthread.h>
#include <stdatomic.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "runtime.h"
#include "dualsense_feedback.h"
#include "gameplay_feedback.h"
#include "gameplay_haptic_patterns.h"
#include "skydive_feedback.h"
#include "device_haptics_ios.h"

static dispatch_queue_t feedbackQueue;
static dispatch_source_t feedbackTimer;
static char queueKey;
static GCController *feedbackPad;
static BOOL foreground, splitHandles, handlesEnabled, triggersEnabled;
static float hapticGain, triggerGain;
static CHHapticEngine *engines[2];
static id<CHHapticPatternPlayer> players[2];
static CHHapticPattern *pattern;
static float sentIntensity[2], sentSharpness[2], sentTriggerAmplitude[2], sentTriggerFrequency[2];
static uint64_t playerSince[2], hapticLastActive[2], retryAt[2], generation[2], hapticUpdates[2], hapticStarts[2], triggerUpdates[2];
static pthread_mutex_t sampleLock = PTHREAD_MUTEX_INITIALIZER;
static TOTKFeedbackSample wanted[2];
static uint64_t received[2], nonzero[2];
static uint64_t lastReport;
static dispatch_queue_t diagnosticsQueue;
static pthread_mutex_t reportLock = PTHREAD_MUTEX_INITIALIZER;
static NSDictionary *pendingReport;
static GCController *pendingReportPad;
static BOOL reportWorkerRunning;
static TOTKSemanticFeedbackState semanticState[2];
static TOTKAdaptiveTriggerOutput submittedTrigger[2];
static uint64_t semanticActiveSince;
static TOTKHapticCueState cueState[2];
static unsigned cueCategory[2];
static uint64_t cueAdmissions[2];
static int skydiveWindActive[2];
static uint64_t skydiveWindTicks[2];
static uint64_t bowTriggerPolicyTicks;
static uint64_t bowPrearmPolicyTicks;

static float setting_gain(const char *name, float fallback) {
    const char *text=getenv(name); if(!text)return fallback;
    char *end=NULL;float value=strtof(text,&end);
    return end!=text && !*end && isfinite(value) ? fmaxf(0,fminf(1,value)) : fallback;
}
static BOOL setting_on(const char *name) {
    const char *text=getenv(name);return !text || strcmp(text,"0")!=0;
}
static void on_feedback_queue(dispatch_block_t work) {
    if(dispatch_get_specific(&queueKey))work();else dispatch_sync(feedbackQueue,work);
}
static GCDualSenseGamepad *dual_sense(void) {
    GCExtendedGamepad *pad=feedbackPad.extendedGamepad;
    return [pad isKindOfClass:GCDualSenseGamepad.class]?(GCDualSenseGamepad *)pad:nil;
}
static void triggers_off(void) {
    GCDualSenseGamepad *pad=dual_sense();
    [pad.leftTrigger setModeOff];[pad.rightTrigger setModeOff];
    for(int side=0;side<2;side++){
        sentTriggerAmplitude[side]=0;sentTriggerFrequency[side]=-1;
        submittedTrigger[side]=(TOTKAdaptiveTriggerOutput){0};
    }
}
static void release_engine(int side) {
    generation[side]++;
    [players[side] stopAtTime:CHHapticTimeImmediate error:nil];
    players[side]=nil;
    CHHapticEngine *engine=engines[side];engines[side]=nil;
    engine.resetHandler=^{};engine.stoppedHandler=^(CHHapticEngineStoppedReason reason){(void)reason;};
    [engine stopWithCompletionHandler:nil];
    playerSince[side]=hapticLastActive[side]=0;sentIntensity[side]=sentSharpness[side]=-1;
}
static void stop_output(void) {
    runtime_device_haptics_quiet(runtime_now_ns());
    triggers_off();
    for(int side=0;side<2;side++)release_engine(side);
    pthread_mutex_lock(&sampleLock);
    memset(wanted,0,sizeof wanted);
    pthread_mutex_unlock(&sampleLock);
    memset(semanticState,0,sizeof semanticState);
    memset(cueState,0,sizeof cueState);
    memset(cueCategory,0,sizeof cueCategory);
    memset(skydiveWindActive,0,sizeof skydiveWindActive);
}
static void write_reports(void) {
    for(;;){@autoreleasepool{
        pthread_mutex_lock(&reportLock);
        NSMutableDictionary *info=[pendingReport mutableCopy];
        GCController *controller=pendingReportPad;
        pendingReport=nil;pendingReportPad=nil;
        if(!info){reportWorkerRunning=NO;pthread_mutex_unlock(&reportLock);return;}
        pthread_mutex_unlock(&reportLock);
        GCExtendedGamepad *profile=controller.extendedGamepad;
        GCDualSenseGamepad *pad=[profile isKindOfClass:GCDualSenseGamepad.class]?(GCDualSenseGamepad *)profile:nil;
        info[@"controller"]=controller.vendorName?:@"none";
        info[@"profile"]=controller.productCategory?:@"none";
        info[@"dualSenseSupported"]=@(pad!=nil);
        info[@"hapticLocalities"]=controller.haptics.supportedLocalities.allObjects?:@[];
        info[@"motionAvailable"]=@(controller.motion.hasRotationRate);
        // The SDK reports controller state asynchronously; these values are observations, not command acknowledgements.
        info[@"hardwareTriggerModesLastReported"]=@[@(pad.leftTrigger.mode),@(pad.rightTrigger.mode)];
        info[@"hardwareTriggerStatusLastReported"]=@[@(pad.leftTrigger.status),@(pad.rightTrigger.status)];
        info[@"triggerStatusIsAsynchronous"]=@YES;
        NSString *docs=NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,NSUserDomainMask,YES).firstObject;
        [[NSJSONSerialization dataWithJSONObject:info options:NSJSONWritingPrettyPrinted error:nil]
            writeToFile:[docs stringByAppendingPathComponent:@"controller-feedback.json"] options:NSDataWritingAtomic error:nil];
    }}
}
static void report(void) {
    uint64_t values[2],positive[2];
    TOTKSemanticContext context;TOTKSemanticEvents events;RuntimeGameplayFeedbackEvidence evidence;
    runtime_gameplay_feedback_read(&context,&events,&evidence);
    pthread_mutex_lock(&sampleLock);
    memcpy(values,received,sizeof values);memcpy(positive,nonzero,sizeof positive);
    pthread_mutex_unlock(&sampleLock);
    NSDictionary *info=@{
        @"connected":@(feedbackPad!=nil),@"foreground":@(foreground),
        @"deviceHaptics":runtime_device_haptics_diagnostics(),
        @"stereoHapticsSupported":@(splitHandles),
        @"hapticsEnabled":@(handlesEnabled),@"adaptiveTriggersEnabled":@(triggersEnabled),
        @"motionSamples":@(runtime_device_motion_sample_count()),
        @"nativeValues":@[@(values[0]),@(values[1])],
        @"nativeNonzeroValues":@[@(positive[0]),@(positive[1])],
        @"hapticParameterCallsSucceeded":@[@(hapticUpdates[0]),@(hapticUpdates[1])],
        @"hapticStartCallsSucceeded":@[@(hapticStarts[0]),@(hapticStarts[1])],
        @"buildVersion":NSBundle.mainBundle.infoDictionary[@"CFBundleVersion"]?:@"unknown",
        @"extraCueAdmissions":@[@(cueAdmissions[0]),@(cueAdmissions[1])],
        @"extraCueActiveIds":@[@(cueState[0].active_cue_id),@(cueState[1].active_cue_id)],
        @"skydiveWindActive":@[@(skydiveWindActive[0]),@(skydiveWindActive[1])],
        @"skydiveWindPolicyTicks":@[@(skydiveWindTicks[0]),@(skydiveWindTicks[1])],
        @"triggerCommandsSubmitted":@[@(triggerUpdates[0]),@(triggerUpdates[1])],
        @"lastSubmittedTriggerAmplitude":@[@(sentTriggerAmplitude[0]),@(sentTriggerAmplitude[1])],
        @"lastSubmittedTriggerPolicyMode":@[@(submittedTrigger[0].mode),@(submittedTrigger[1].mode)],
        @"lastSubmittedTriggerStartStrength":@[@(submittedTrigger[0].start_strength),@(submittedTrigger[1].start_strength)],
        @"lastSubmittedTriggerEndStrength":@[@(submittedTrigger[0].end_strength),@(submittedTrigger[1].end_strength)],
        @"lastSubmittedTriggerStartPosition":@[@(submittedTrigger[0].start_position),@(submittedTrigger[1].start_position)],
        @"lastSubmittedTriggerEndPosition":@[@(submittedTrigger[0].end_position),@(submittedTrigger[1].end_position)],
        @"bowTriggerPolicyTicks":@(bowTriggerPolicyTicks),
        @"bowPrearmPolicyTicks":@(bowPrearmPolicyTicks),
        @"gameplayContext":@{@"valid":@(context.valid),@"paused":@(context.paused),
            @"bowDrawn":@(context.bow_drawn),@"bowReady":@(context.bow_ready),
            @"shieldReady":@(context.shield_ready),@"shieldRaised":@(context.shield_raised),
            @"shieldSurfing":@(context.shield_surfing),@"speedMps":@(context.speed_mps),
            @"action":@(evidence.action),@"detail":@(evidence.detail),
            @"frames":@(evidence.frames),@"playerFrames":@(evidence.player_frames),
            @"readFailures":@(evidence.read_failures),@"timestampNs":@(context.timestamp_ns),
            @"grounded":@(evidence.grounded),@"shieldAttached":@(evidence.shield_attached),
            @"controllerPaused":@(evidence.controller_paused),@"menuPaused":@(evidence.menu_paused),
            @"rumblePaused":@(evidence.rumble_paused),@"guardCueFingerprints":@(evidence.guard_cue_fingerprints),
            @"meleeCueFingerprints":@(evidence.melee_cue_fingerprints),@"meleeDrawn":@(evidence.melee_drawn),@"meleeEvents":@(evidence.melee_events),
            @"swimming":@(evidence.swimming),@"climbing":@(evidence.climbing),@"charging":@(evidence.charging),
            @"climbDashing":@(evidence.climb_dashing),@"meleeAttacking":@(evidence.melee_attacking),
            @"skydiving":@(evidence.skydiving),@"skydiveStartedNs":@(evidence.skydive_started_ns),
            @"skydiveDownwardSpeedMps":@(evidence.skydive_downward_speed_mps),
            @"skydiveDescentM":@(evidence.skydive_descent_m),
            @"actionVerb":@(evidence.action_verb),@"subActionVerb":@(evidence.sub_action_verb),
            @"lastCueId":@(evidence.last_cue_id),@"lastCuePitch":@(evidence.last_cue_pitch),
            @"cueQueueDrops":@(evidence.cue_queue_drops),
            @"cueEvents":@{ @"start":@(evidence.cue_events[RUNTIME_HAPTIC_CUE_START]),
                @"dialogue":@(evidence.cue_events[RUNTIME_HAPTIC_CUE_DIALOGUE]),
                @"waterDash":@(evidence.cue_events[RUNTIME_HAPTIC_CUE_WATER_DASH]),
                @"climb":@(evidence.cue_events[RUNTIME_HAPTIC_CUE_CLIMB]),
                @"charge":@(evidence.cue_events[RUNTIME_HAPTIC_CUE_CHARGE]),
                @"swing":@(evidence.cue_events[RUNTIME_HAPTIC_CUE_SWING]),
                @"spearSwing":@(evidence.cue_events[RUNTIME_HAPTIC_CUE_THRUST]),
                @"climbDash":@(evidence.cue_events[RUNTIME_HAPTIC_CUE_CLIMB_DASH])},
            @"cueMatches":@[@(evidence.cue_matches[0]),@(evidence.cue_matches[1]),@(evidence.cue_matches[2]),
                @(evidence.cue_matches[3]),@(evidence.cue_matches[4]),@(evidence.cue_matches[5]),
                @(evidence.cue_matches[6]),@(evidence.cue_matches[7]),@(evidence.cue_matches[8])],
            @"cueStateRejects":@[@(evidence.cue_state_rejects[0]),@(evidence.cue_state_rejects[1]),@(evidence.cue_state_rejects[2]),
                @(evidence.cue_state_rejects[3]),@(evidence.cue_state_rejects[4]),@(evidence.cue_state_rejects[5]),
                @(evidence.cue_state_rejects[6]),@(evidence.cue_state_rejects[7]),@(evidence.cue_state_rejects[8])],
            @"lastRejectedCueId":@(evidence.last_rejected_cue_id),@"lastRejectedActionVerb":@(evidence.last_rejected_action_verb),
            @"npcEventIds":@(evidence.npc_event_ids),@"eventActive":@(evidence.event_active),
            @"eventName":@(evidence.event_name),@"hardPaused":@(evidence.hard_paused),
            @"npcInteractionAllowed":@(context.npc_interaction_allowed),
            @"npcEvents":@(evidence.npc_events),@"landingEvents":@(evidence.landing_events),@"blockEvents":@(evidence.block_events)},
        @"semanticEvents":@{@"npcInteractionNs":@(events.npc_interaction_timestamp_ns),
            @"landingNs":@(events.landing_timestamp_ns),@"shieldBlockNs":@(events.shield_block_timestamp_ns),
            @"meleeSwingNs":@(events.melee_swing_timestamp_ns),@"meleeKind":@(events.melee_kind)},
        @"updated":NSDate.date.description
    };
    pthread_mutex_lock(&reportLock);
    pendingReport=info;pendingReportPad=feedbackPad;
    BOOL schedule=!reportWorkerRunning;reportWorkerRunning=YES;
    pthread_mutex_unlock(&reportLock);
    if(schedule)dispatch_async(diagnosticsQueue,^{write_reports();});
}
static void engine_failed(int side,NSString *operation,NSError *error) {
    fprintf(stderr,"[controller-feedback] %s handle %s: %s; retrying\n",side?"right":"left",
        operation.UTF8String,error.localizedDescription.UTF8String?:"unavailable");
    release_engine(side);retryAt[side]=runtime_now_ns()+2000000000ull;
}
static BOOL ensure_engine(int side,uint64_t now) {
    if(engines[side])return YES;
    if(!feedbackPad.haptics || now<retryAt[side])return NO;
    NSError *error=nil;
    if(!pattern){
        NSArray *parameters=@[
            [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticIntensity value:1],
            [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticSharpness value:0.5f]];
        // Short leases bound output if the game stalls; renew without relying on controller advanced-player looping.
        CHHapticEvent *event=[[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticContinuous
            parameters:parameters relativeTime:0 duration:2];
        pattern=[[CHHapticPattern alloc] initWithEvents:@[event] parameters:@[] error:&error];
        if(!pattern){engine_failed(side,@"pattern",error);return NO;}
    }
    NSSet *localities=feedbackPad.haptics.supportedLocalities;
    GCHapticsLocality locality=splitHandles?(side?GCHapticsLocalityRightHandle:GCHapticsLocalityLeftHandle):
        ([localities containsObject:GCHapticsLocalityDefault]?GCHapticsLocalityDefault:localities.anyObject);
    if(!locality)return NO;
    CHHapticEngine *engine=[feedbackPad.haptics createEngineWithLocality:locality];
    if(!engine){engine_failed(side,@"engine creation",nil);return NO;}
    engine.playsHapticsOnly=YES;
    __weak CHHapticEngine *weakEngine=engine;
    uint64_t token=++generation[side];
    engine.resetHandler=^{
        CHHapticEngine *resetEngine=weakEngine;
        dispatch_async(feedbackQueue,^{
            if(generation[side]==token && engines[side]==resetEngine){
                release_engine(side);retryAt[side]=runtime_now_ns()+2000000000ull;
            }
        });
    };
    engine.stoppedHandler=^(CHHapticEngineStoppedReason reason){
        (void)reason;CHHapticEngine *stoppedEngine=weakEngine;
        dispatch_async(feedbackQueue,^{
            if(generation[side]==token && engines[side]==stoppedEngine){
                release_engine(side);retryAt[side]=runtime_now_ns()+2000000000ull;
            }
        });
    };
    if(![engine startAndReturnError:&error]){engine_failed(side,@"engine start",error);return NO;}
    engines[side]=engine;
    fprintf(stderr,"[controller-feedback] %s haptic engine ready (%s)\n",side?"right":"left",locality.UTF8String);
    return YES;
}
static BOOL send_parameters(id<CHHapticPatternPlayer> player,int side,float intensity,float sharpness) {
    NSError *error=nil;
    NSArray *parameters=@[
        [[CHHapticDynamicParameter alloc] initWithParameterID:CHHapticDynamicParameterIDHapticIntensityControl
            value:intensity relativeTime:0],
        [[CHHapticDynamicParameter alloc] initWithParameterID:CHHapticDynamicParameterIDHapticSharpnessControl
            value:sharpness-0.5f relativeTime:0]];
    if(![player sendParameters:parameters atTime:CHHapticTimeImmediate error:&error]){
        engine_failed(side,@"parameter update",error);return NO;
    }
    sentIntensity[side]=intensity;sentSharpness[side]=sharpness;hapticUpdates[side]++;
    return YES;
}
static void handle_output(int side,TOTKFeedbackOutput value,uint64_t now) {
    if(!handlesEnabled || value.intensity<0.02f){
        if(players[side]){
            [players[side] stopAtTime:CHHapticTimeImmediate error:nil];players[side]=nil;
            sentIntensity[side]=0;playerSince[side]=0;
        }
        // Keep a short warm lease between impacts, then release the idle controller engine.
        if(engines[side] && now-hapticLastActive[side]>=2000000000ull)release_engine(side);
        return;
    }
    if(!ensure_engine(side,now))return;
    hapticLastActive[side]=now;
    if(!players[side] || now-playerSince[side]>=1500000000ull){
        NSError *error=nil;
        id<CHHapticPatternPlayer> player=[engines[side] createPlayerWithPattern:pattern error:&error];
        if(!player){engine_failed(side,@"player creation",error);return;}
        if(!send_parameters(player,side,value.intensity,value.sharpness))return;
        if(![player startAtTime:CHHapticTimeImmediate error:&error]){engine_failed(side,@"player start",error);return;}
        [players[side] stopAtTime:CHHapticTimeImmediate error:nil];
        players[side]=player;playerSince[side]=now;hapticStarts[side]++;
    }else if(fabsf(value.intensity-sentIntensity[side])>=0.008f || fabsf(value.sharpness-sentSharpness[side])>=0.015f)
        send_parameters(players[side],side,value.intensity,value.sharpness);
}
static void trigger_output(int side,TOTKAdaptiveTriggerOutput value) {
    GCDualSenseGamepad *pad=dual_sense();
    if(!pad)return;
    GCDualSenseAdaptiveTrigger *trigger=side?pad.rightTrigger:pad.leftTrigger;
    if(!triggersEnabled)value=(TOTKAdaptiveTriggerOutput){0};
    TOTKAdaptiveTriggerOutput prior=submittedTrigger[side];
    if(value.mode==TOTK_TRIGGER_OFF){
        if(prior.mode!=TOTK_TRIGGER_OFF){[trigger setModeOff];triggerUpdates[side]++;}
        submittedTrigger[side]=value;sentTriggerAmplitude[side]=0;
        return;
    }
    if(value.mode==prior.mode && fabsf(value.start_position-prior.start_position)<0.01f &&
       fabsf(value.end_position-prior.end_position)<0.01f && fabsf(value.start_strength-prior.start_strength)<0.015f &&
       fabsf(value.end_strength-prior.end_strength)<0.015f && fabsf(value.amplitude-prior.amplitude)<0.015f &&
       fabsf(value.frequency-prior.frequency)<0.025f)return;
    switch(value.mode){
    case TOTK_TRIGGER_FEEDBACK:
        [trigger setModeFeedbackWithStartPosition:value.start_position resistiveStrength:value.start_strength];break;
    case TOTK_TRIGGER_VIBRATION:
        [trigger setModeVibrationWithStartPosition:value.start_position amplitude:value.amplitude frequency:value.frequency];break;
    case TOTK_TRIGGER_SLOPE:
        [trigger setModeSlopeFeedbackWithStartPosition:value.start_position endPosition:value.end_position
            startStrength:value.start_strength endStrength:value.end_strength];break;
    default:[trigger setModeOff];value=(TOTKAdaptiveTriggerOutput){0};break;
    }
    submittedTrigger[side]=value;sentTriggerAmplitude[side]=value.amplitude;
    sentTriggerFrequency[side]=value.frequency;triggerUpdates[side]++;
}
static void feedback_tick(void) {
    TOTKFeedbackSample samples[2];
    pthread_mutex_lock(&sampleLock);memcpy(samples,wanted,sizeof samples);pthread_mutex_unlock(&sampleLock);
    TOTKSemanticContext context;TOTKSemanticEvents events;RuntimeGameplayFeedbackEvidence live;
    runtime_gameplay_feedback_read(&context,&events,&live);
    BOOL compositionActive=foreground && (feedbackPad!=nil || runtime_device_haptics_wanted(foreground,feedbackPad!=nil));
    if(events.npc_interaction_timestamp_ns<semanticActiveSince)events.npc_interaction_timestamp_ns=0;
    if(events.landing_timestamp_ns<semanticActiveSince)events.landing_timestamp_ns=0;
    if(events.shield_block_timestamp_ns<semanticActiveSince)events.shield_block_timestamp_ns=0;
    if(events.melee_swing_timestamp_ns<semanticActiveSince){events.melee_swing_timestamp_ns=0;events.melee_kind=0;}
    TOTKFeedbackOutput values[2];
    RuntimeHapticCueObservation pending[16], latest[2]={{0}};
    unsigned epoch=0,count=runtime_gameplay_feedback_cues(pending,16,&epoch);
    for(unsigned i=0;i<count;i++){
        if(pending[i].event.timestamp_ns<semanticActiveSince)continue;
        unsigned category=pending[i].category;
        if(live.charging && (category==RUNTIME_HAPTIC_CUE_SWING || category==RUNTIME_HAPTIC_CUE_THRUST))continue;
        for(unsigned side=0;side<2;side++)if(pending[i].locality&(1u<<side)){
            if(latest[side].category==RUNTIME_HAPTIC_CUE_CHARGE && live.charging && category!=RUNTIME_HAPTIC_CUE_CHARGE)continue;
            latest[side]=pending[i];
        }
    }
    /* Sample time after draining: an audio producer may enqueue during this
       tick, and a valid new cue must not be consumed as a future event. */
    uint64_t now=runtime_now_ns();
    for(int side=0;side<2;side++){
        TOTKSemanticFeedbackOutput composed=totk_semantic_feedback_output(samples[side],context,events,now,
            hapticGain,triggerGain,compositionActive,&semanticState[side]);
        values[side]=(TOTKFeedbackOutput){.intensity=composed.handle_intensity,.sharpness=composed.handle_sharpness};
        TOTKHapticCueDescriptor descriptor={0};
        TOTKHapticCuePoint scaled[TOTK_HAPTIC_CUE_MAX_POINTS];
        RuntimeHapticCueObservation cue=latest[side];
        if(cue.event.cue_id && cue.event.cue_id<=EXTRA_CUE_DESCRIPTOR_COUNT){
            cueCategory[side]=cue.category;
            /* Verified Player charge notes use pitches 1, 1.05 and 1.12.
               This shared, tested transform follows actual audio playback. */
            totk_haptic_cue_shape_sound(&descriptor,&extra_cue_descriptors[cue.event.cue_id-1],
                cue.pitch,cue.category==RUNTIME_HAPTIC_CUE_CHARGE,scaled,TOTK_HAPTIC_CUE_MAX_POINTS);
        }
        TOTKHapticCueGate gate={.epoch=epoch,.timestamp_ns=context.timestamp_ns,
            .valid=context.valid,.paused=context.paused};
        /* Exact dialogue page cues have the same narrow NPC pause allowance as
           the approved entry pulse; title sound authority is in its descriptor. */
        if(cueCategory[side]==RUNTIME_HAPTIC_CUE_DIALOGUE)gate.paused=!context.npc_interaction_allowed;
        if(cueCategory[side]==RUNTIME_HAPTIC_CUE_WATER_DASH)gate.valid&=live.swimming;
        /* Climb contacts are short one-shots admitted by exact Climb/DashUp
           state. Let them finish after the action changes; pause still cancels. */
        if(cueCategory[side]==RUNTIME_HAPTIC_CUE_CHARGE)gate.valid&=live.charging;
        if(cueCategory[side]==RUNTIME_HAPTIC_CUE_SWING || cueCategory[side]==RUNTIME_HAPTIC_CUE_THRUST)
            gate.valid&=!live.charging;
        uint64_t admitted=cueState[side].active_sequence;
        TOTKHapticCueOutput extra=totk_haptic_cue_compose(values[side].intensity,values[side].sharpness,
            descriptor.cue_id?&descriptor:NULL,cue.event,gate,epoch,now,hapticGain,
            compositionActive,&cueState[side]);
        if(cueState[side].active_sequence && cueState[side].active_sequence!=admitted)cueAdmissions[side]++;
        values[side].intensity=extra.intensity;values[side].sharpness=extra.sharpness;
        TOTKSkydiveFeedbackSample windSample={.timestamp_ns=context.timestamp_ns,
            .started_ns=live.skydive_started_ns,.downward_speed_mps=live.skydive_downward_speed_mps,
            .descent_m=live.skydive_descent_m,.valid=context.valid,
            .paused=context.paused || live.controller_paused,.skydiving=live.skydiving};
        TOTKSkydiveFeedbackOutput wind=totk_skydive_feedback_compose(values[side].intensity,
            values[side].sharpness,windSample,now,hapticGain,compositionActive,(unsigned)side);
        values[side].intensity=wind.intensity;values[side].sharpness=wind.sharpness;
        skydiveWindActive[side]=wind.wind_active;
        if(wind.wind_active)skydiveWindTicks[side]++;
        trigger_output(side,side?composed.right_trigger:composed.left_trigger);
        if(side && triggersEnabled && dual_sense() && composed.context_fresh && context.bow_drawn &&
           submittedTrigger[1].mode==TOTK_TRIGGER_SLOPE)bowTriggerPolicyTicks++;
        if(side && triggersEnabled && dual_sense() && composed.context_fresh && context.bow_ready &&
           !context.bow_drawn && submittedTrigger[1].mode==TOTK_TRIGGER_SLOPE)bowPrearmPolicyTicks++;
    }
    /* One authoritative composition fans out to independent sinks. The local
       sink is silent with a controller unless Always was explicitly selected. */
    runtime_device_haptics_submit(values[0],values[1],context.timestamp_ns,now,
        foreground,feedbackPad!=nil,context.paused && !context.npc_interaction_allowed);
    if(splitHandles){handle_output(0,values[0],now);handle_output(1,values[1],now);}
    else{
        // A controller with one actuator receives the stronger channel once, rather than two competing default engines.
        TOTKFeedbackOutput mixed=values[0].intensity>=values[1].intensity?values[0]:values[1];
        handle_output(0,mixed,now);
    }
    if(!lastReport || now-lastReport>=30000000000ull){lastReport=now;report();}
}
static void feedback_start(void) {
    static dispatch_once_t once;
    dispatch_once(&once,^{
        handlesEnabled=setting_on("TOTK_CONTROLLER_HAPTICS");
        triggersEnabled=setting_on("TOTK_ADAPTIVE_TRIGGERS");
        hapticGain=setting_gain("TOTK_RUMBLE_GAIN",0.9f);
        triggerGain=setting_gain("TOTK_TRIGGER_GAIN",0.55f);
        foreground=UIApplication.sharedApplication.applicationState==UIApplicationStateActive;
        feedbackQueue=dispatch_queue_create("org.macnativegames.totk.controller-feedback",
            dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL,QOS_CLASS_USER_INTERACTIVE,0));
        diagnosticsQueue=dispatch_queue_create("org.macnativegames.totk.controller-diagnostics",
            dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL,QOS_CLASS_UTILITY,0));
        runtime_device_haptics_initialize(feedbackQueue);
        dispatch_queue_set_specific(feedbackQueue,&queueKey,&queueKey,NULL);
        feedbackTimer=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,feedbackQueue);
        dispatch_source_set_timer(feedbackTimer,DISPATCH_TIME_FOREVER,8*NSEC_PER_MSEC,NSEC_PER_MSEC);
        dispatch_source_set_event_handler(feedbackTimer,^{@autoreleasepool{feedback_tick();}});
        dispatch_resume(feedbackTimer);
        for(int side=0;side<2;side++){sentIntensity[side]=sentSharpness[side]=-1;sentTriggerFrequency[side]=-1;}
    });
}
static void refresh_feedback_activity(void) {
    BOOL local=runtime_device_haptics_wanted(foreground,feedbackPad!=nil);
    runtime_gameplay_feedback_set_active(foreground &&
        ((feedbackPad!=nil && (handlesEnabled || triggersEnabled)) || local));
    BOOL ticking=foreground && (feedbackPad!=nil || local);
    dispatch_source_set_timer(feedbackTimer,ticking?dispatch_time(DISPATCH_TIME_NOW,0):DISPATCH_TIME_FOREVER,
        8*NSEC_PER_MSEC,NSEC_PER_MSEC);
}
void runtime_feedback_attach(GCController *controller) {
    feedback_start();
    on_feedback_queue(^{
        if(feedbackPad==controller)return;
        stop_output();feedbackPad=controller;triggers_off();
        semanticActiveSince=runtime_now_ns();
        memset(hapticUpdates,0,sizeof hapticUpdates);memset(hapticStarts,0,sizeof hapticStarts);
        memset(triggerUpdates,0,sizeof triggerUpdates);
        memset(cueAdmissions,0,sizeof cueAdmissions);
        memset(skydiveWindTicks,0,sizeof skydiveWindTicks);
        bowTriggerPolicyTicks=0;
        bowPrearmPolicyTicks=0;
        pthread_mutex_lock(&sampleLock);
        memset(received,0,sizeof received);memset(nonzero,0,sizeof nonzero);
        pthread_mutex_unlock(&sampleLock);
        NSSet *where=controller.haptics.supportedLocalities;
        splitHandles=[where containsObject:GCHapticsLocalityLeftHandle] && [where containsObject:GCHapticsLocalityRightHandle];
        for(int side=0;side<2;side++)retryAt[side]=0;
        refresh_feedback_activity();
        fprintf(stderr,"[controller-feedback] attached %s (%s): stereo-supported=%d adaptive-supported=%d adaptive-enabled=%d motion-supported=%d\n",
            controller.vendorName.UTF8String?:"controller",controller.productCategory.UTF8String?:"unknown",
            splitHandles,dual_sense()!=nil,triggersEnabled,controller.motion.hasRotationRate);
        report();
    });
}
void runtime_feedback_detach(GCController *controller) {
    if(!feedbackQueue)return;
    on_feedback_queue(^{
        if(feedbackPad!=controller)return;
        stop_output();feedbackPad=nil;splitHandles=NO;
        semanticActiveSince=runtime_now_ns();
        refresh_feedback_activity();
        report();
    });
}
void runtime_feedback_quiet(void) {
    pthread_mutex_lock(&sampleLock);memset(wanted,0,sizeof wanted);pthread_mutex_unlock(&sampleLock);
    if(feedbackQueue)on_feedback_queue(^{stop_output();});
}
void runtime_feedback_focus(int active) {
    feedback_start();
    on_feedback_queue(^{
        foreground=active!=0;
        stop_output();
        runtime_device_haptics_preferences();
        semanticActiveSince=runtime_now_ns();
        refresh_feedback_activity();
        report();
    });
}
void runtime_rumble_set(unsigned side,float lowAmplitude,float lowFrequency,float highAmplitude,float highFrequency) {
    if(side>1)return;
    TOTKFeedbackSample sample={lowAmplitude,lowFrequency,highAmplitude,highFrequency,runtime_now_ns()};
    pthread_mutex_lock(&sampleLock);
    wanted[side]=sample;received[side]++;
    if(isfinite(lowAmplitude)&&isfinite(highAmplitude)&&(lowAmplitude>0.02f||highAmplitude>0.02f))nonzero[side]++;
    pthread_mutex_unlock(&sampleLock);
}
#endif
