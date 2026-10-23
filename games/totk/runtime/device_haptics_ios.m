/* Capability-gated, device-only Core Haptics; never routes audio or controller effects. */
#include <TargetConditionals.h>
#if TARGET_OS_IOS
#import <CoreHaptics/CoreHaptics.h>
#include "runtime.h"
#include "device_haptics_ios.h"
#include "simple_settings_ios.h"

static dispatch_queue_t ownerQueue;
static BOOL supported, eligible;
static int outputMode = TOTK_DEVICE_HAPTICS_TOUCH;
static float localGain = .25f, sentIntensity = -1, sentSharpness = -1;
static CHHapticEngine *engine;
static id<CHHapticPatternPlayer> player;
static uint64_t generation, playerSince, lastActive, retryAt, activeSince;
static uint64_t starts, updates, failures, heartbeatRejects;

static void release_local_engine(void) {
    generation++;
    [player stopAtTime:CHHapticTimeImmediate error:nil]; player = nil;
    CHHapticEngine *old = engine; engine = nil;
    old.resetHandler = ^{};
    old.stoppedHandler = ^(CHHapticEngineStoppedReason reason){ (void)reason; };
    [old stopWithCompletionHandler:nil];
    playerSince = lastActive = 0;
    sentIntensity = sentSharpness = -1;
}

void runtime_device_haptics_quiet(uint64_t now_ns) {
    release_local_engine();
    eligible = NO;
    activeSince = now_ns;
}

void runtime_device_haptics_preferences(void) {
    NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
    int mode = totk_simple_device_haptic_mode(defaults);
    id setting = [defaults objectForKey:@"TOTKDeviceHapticIntensity"];
    float gain = [setting respondsToSelector:@selector(floatValue)] ? [setting floatValue] : .25f;
    if (!isfinite(gain)) gain = .25f;
    gain = fmaxf(0, fminf(.4f, gain));
    if (mode != outputMode || gain != localGain) runtime_device_haptics_quiet(runtime_now_ns());
    outputMode = mode; localGain = gain;
}

void runtime_device_haptics_initialize(dispatch_queue_t owner) {
    ownerQueue = owner;
    supported = CHHapticEngine.capabilitiesForHardware.supportsHaptics;
    runtime_device_haptics_preferences();
}

int runtime_device_haptics_wanted(int foreground, int controller) {
    return localGain > 0 && totk_device_haptics_eligible(supported, outputMode, foreground, controller);
}

static void failed(uint64_t now_ns) {
    failures++;
    release_local_engine();
    retryAt = now_ns + UINT64_C(2000000000);
    activeSince = now_ns; /* Recovery requires a new game heartbeat, not queued observations. */
}

static BOOL ensure_local_engine(uint64_t now_ns) {
    if (engine) return YES;
    if (!supported || now_ns < retryAt) return NO;
    NSError *error = nil;
    engine = [[CHHapticEngine alloc] initAndReturnError:&error];
    if (!engine || error) { failed(now_ns); return NO; }
    engine.playsHapticsOnly = YES;
    engine.autoShutdownEnabled = YES;
    const uint64_t token = ++generation;
    engine.resetHandler = ^{
        dispatch_async(ownerQueue, ^{
            if (token == generation) failed(runtime_now_ns());
        });
    };
    engine.stoppedHandler = ^(CHHapticEngineStoppedReason reason){
        (void)reason;
        dispatch_async(ownerQueue, ^{
            if (token == generation) failed(runtime_now_ns());
        });
    };
    if (![engine startAndReturnError:&error]) { failed(now_ns); return NO; }
    return YES;
}

void runtime_device_haptics_submit(TOTKFeedbackOutput left, TOTKFeedbackOutput right,
    uint64_t heartbeat_ns, uint64_t now_ns, int foreground, int controller, int paused) {
    BOOL wanted = runtime_device_haptics_wanted(foreground, controller);
    if (eligible != wanted) {
        runtime_device_haptics_quiet(now_ns);
        eligible = wanted;
    }
    TOTKFeedbackOutput value = totk_device_haptics_output(left, right, heartbeat_ns,
        activeSince, now_ns, localGain, wanted, paused);
    if (wanted && (!heartbeat_ns || heartbeat_ns > now_ns ||
        now_ns-heartbeat_ns >= UINT64_C(200000000))) heartbeatRejects++;
    if (value.intensity < .002f) {
        [player stopAtTime:CHHapticTimeImmediate error:nil]; player = nil; playerSince = 0;
        sentIntensity = sentSharpness = -1;
        if (engine && (!wanted || paused || !value.fresh ||
            (lastActive && now_ns-lastActive >= UINT64_C(1000000000)))) release_local_engine();
        return;
    }
    if (!ensure_local_engine(now_ns)) return;
    lastActive = now_ns;
    NSError *error = nil;
    BOOL start = !player || now_ns-playerSince >= TOTK_DEVICE_HAPTICS_RENEW_NS;
    if (start) {
        [player stopAtTime:CHHapticTimeImmediate error:nil]; player = nil;
        NSArray *parameters = @[
            [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticIntensity value:.22f],
            [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticSharpness value:0]];
        CHHapticEvent *event = [[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticContinuous
            parameters:parameters relativeTime:0 duration:(double)TOTK_DEVICE_HAPTICS_LEASE_NS/1e9];
        CHHapticPattern *pattern = [[CHHapticPattern alloc] initWithEvents:@[event] parameters:@[] error:&error];
        if (!pattern || error) { failed(now_ns); return; }
        player = [engine createPlayerWithPattern:pattern error:&error];
        if (!player || error) { failed(now_ns); return; }
    }
    if (!start && fabsf(value.intensity-sentIntensity) < .002f && fabsf(value.sharpness-sentSharpness) < .01f) return;
    /* Intensity control multiplies the capped base; sharpness control adds to
       a zero base. Submit both before starting, so no full-cap startup blip. */
    NSArray *parameters = @[
        [[CHHapticDynamicParameter alloc] initWithParameterID:CHHapticDynamicParameterIDHapticIntensityControl
            value:value.intensity/.22f relativeTime:0],
        [[CHHapticDynamicParameter alloc] initWithParameterID:CHHapticDynamicParameterIDHapticSharpnessControl
            value:value.sharpness relativeTime:0]];
    if (![player sendParameters:parameters atTime:CHHapticTimeImmediate error:&error] || error) { failed(now_ns); return; }
    if (start) {
        if (![player startAtTime:CHHapticTimeImmediate error:&error]) { failed(now_ns); return; }
        playerSince = now_ns; starts++;
    }
    sentIntensity = value.intensity; sentSharpness = value.sharpness; updates++;
}

NSDictionary *runtime_device_haptics_diagnostics(void) {
    return @{ @"supported":@(supported), @"status":supported?@"available":@"unsupported: no built-in haptic actuator",
        @"mode":outputMode==TOTK_DEVICE_HAPTICS_OFF?@"off":outputMode==TOTK_DEVICE_HAPTICS_ALWAYS?@"always":@"touch",
        @"gain":@(localGain), @"intensityCap":@.22, @"hardwareLeaseMs":@100,
        @"eligible":@(eligible), @"enginePresent":@(engine!=nil),
        @"startCallsSucceeded":@(starts), @"parameterCallsSucceeded":@(updates),
        @"engineFailures":@(failures), @"staleHeartbeatTicks":@(heartbeatRejects) };
}
#endif
