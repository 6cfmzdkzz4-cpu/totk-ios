/* Phone calls and route changes stop RemoteIO. Bring the same session back without
   tearing down the emulated audren renderer. */
#import <AVFAudio/AVFAudio.h>
#import <UIKit/UIKit.h>
#include <stdatomic.h>
#include <unistd.h>
#include "audio_host.h"

static _Atomic int g_resume_gen;

static int session_mixes(void) {
    return (AVAudioSession.sharedInstance.categoryOptions & AVAudioSessionCategoryOptionMixWithOthers) != 0;
}
static int session_configure(void) {
    AVAudioSession *session = AVAudioSession.sharedInstance;
    AVAudioSessionCategoryOptions opts = 0;
    if (session_mixes()) opts |= AVAudioSessionCategoryOptionMixWithOthers;
    NSError *err = nil;
    BOOL ok = [session setCategory:AVAudioSessionCategoryPlayback withOptions:opts error:&err];
    audio_diag("[audio] session playback mix=%d ok=%d", session_mixes(), ok ? 1 : 0);
    return ok ? 1 : 0;
}
static int session_activate(void) {
    AVAudioSession *session = AVAudioSession.sharedInstance;
    for (int attempt = 0; attempt < 5; attempt++) {
        NSError *err = nil;
        if ([session setActive:YES error:&err]) return 1;
        if (attempt < 4) usleep((useconds_t)(100000u << attempt));
    }
    return 0;
}
static void session_force_restart(void) {
    session_configure();
    int ok = session_activate() && audio_host_restart(0);
    audio_diag(ok ? "[audio] restart ok" : "[audio] restart err");
}
static void resume_after(int generation, int should_resume, int rebuild) {
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^{
        if (atomic_load(&g_resume_gen) != generation) return;
        session_configure();
        int active = session_activate();
        if (atomic_load(&g_resume_gen) != generation) return;
        int ok = active && (rebuild ? audio_host_restart(1) : audio_host_set_interrupted(0));
        if (rebuild) audio_diag(ok ? "[audio] restart ok" : "[audio] restart err");
        else {
            audio_diag("[audio] interruption ended resume=%d ok=%d", should_resume, ok);
            audio_diag(ok ? "[audio] restart ok" : "[audio] restart err");
        }
    });
}

void totk_audio_foreground(void) {
    if (!audio_host_needs_restart()) return;
    int generation = atomic_load(&g_resume_gen);
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^{
        if (atomic_load(&g_resume_gen) != generation) return;
        if (!audio_host_needs_restart()) return;
        int active = session_activate();
        int ok = active && audio_host_restart(0);
        audio_diag(ok ? "[audio] restart ok" : "[audio] restart err");
    });
}

static void on_interruption(NSNotification *note) {
    NSUInteger type = [note.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue];
    if (type == AVAudioSessionInterruptionTypeBegan) {
        atomic_fetch_add(&g_resume_gen, 1);
        audio_diag("[audio] interruption began");
        audio_host_set_interrupted(1);
        return;
    }
    NSUInteger opt = [note.userInfo[AVAudioSessionInterruptionOptionKey] unsignedIntegerValue];
    int should_resume = (opt & AVAudioSessionInterruptionOptionShouldResume) ? 1 : 0;
    /* Games resume even when the system leaves shouldResume unset. */
    resume_after(atomic_load(&g_resume_gen), should_resume, 0);
}
static void on_reset(NSNotification *note) {
    (void)note;
    int generation = atomic_fetch_add(&g_resume_gen, 1) + 1;
    audio_diag("[audio] media services reset");
    audio_host_set_interrupted(1);
    resume_after(generation, 1, 1);
}
static void on_route(NSNotification *note) {
    NSUInteger reason = [note.userInfo[AVAudioSessionRouteChangeReasonKey] unsignedIntegerValue];
    audio_diag("[audio] route change reason=%u", (unsigned)reason);
    if (audio_host_is_paused()) return;
    int generation = atomic_load(&g_resume_gen);
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^{
        if (atomic_load(&g_resume_gen) != generation || audio_host_is_paused()) return;
        int ok = session_activate() && audio_host_restart(0);
        audio_diag(ok ? "[audio] restart ok" : "[audio] restart err");
    });
}

void totk_audio_session_start(void) {
    audio_host_set_restart_hook(session_force_restart);
    session_configure();
    session_activate();
    NSNotificationCenter *center = NSNotificationCenter.defaultCenter;
    [center addObserverForName:AVAudioSessionInterruptionNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *note) { on_interruption(note); }];
    [center addObserverForName:AVAudioSessionMediaServicesWereResetNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *note) { on_reset(note); }];
    [center addObserverForName:AVAudioSessionRouteChangeNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *note) { on_route(note); }];
    [center addObserverForName:UIApplicationDidBecomeActiveNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *note) { (void)note; totk_audio_foreground(); }];
    [center addObserverForName:UIApplicationWillEnterForegroundNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *note) { (void)note; totk_audio_foreground(); }];
}
