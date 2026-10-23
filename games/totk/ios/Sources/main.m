// Native game entry point and scene lifecycle for iPhone and iPad, including iPadOS 27.
#import <UIKit/UIKit.h>
#import <AVFAudio/AVFAudio.h>
#import <QuartzCore/CAMetalLayer.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include "runtime.h"
#include "dm_display_geometry.h"
#include "totk_scene_geometry.h"
#include "feedback_preferences_ios.h"
#include "simple_settings_ios.h"
#import "touch_pad.h"
#import "TOTKCloudSave.h"
#import "TOTKSettings.h"
#import "DmFPSOverlay.h"
#include "fps_overlay.h"

int totk_host_main(int argc, char **argv);
void runtime_ios_attach_layer(CAMetalLayer *layer, UIView *host);
int runtime_ios_update_geometry(CAMetalLayer *layer, UIView *host);
double runtime_ios_picture_aspect(void);
int runtime_ios_live_display_profile(void);
static char *host_argv[3];
static void *run_host(void *unused) { (void)unused; return (void *)(intptr_t)totk_host_main(3, host_argv); }
/* The desktop keyboard-overlay shortcut has no counterpart on iOS. */
void runtime_shortcut_help_toggle(void) {}

@interface TOTKMetalView : UIView @end
@implementation TOTKMetalView
+ (Class)layerClass { return CAMetalLayer.class; }
@end

@interface TOTKGameController : UIViewController
@property(nonatomic,strong) TOTKMetalView *gameView;
@property(nonatomic,strong) TOTKTouchPad *touchPad;
@property(nonatomic,strong) DmFPSOverlay *fpsOverlay;
- (BOOL)openSettings;
- (void)updateDisplayGeometry;
@end
@implementation TOTKGameController
- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = UIColor.blackColor;
    self.gameView = [[TOTKMetalView alloc] initWithFrame:self.view.bounds];
    // Narrow live scenes own the full view; the renderer fits a source-bound
    // baseline only when a layout/profile is explicitly unqualified.
    self.gameView.autoresizingMask = UIViewAutoresizingNone;
    [self.view addSubview:self.gameView];
    [self updateDisplayGeometry];
    runtime_ios_attach_layer((CAMetalLayer *)self.gameView.layer, self.gameView);
    TOTKTouchPad *pad=[[TOTKTouchPad alloc] initWithFrame:self.view.bounds];pad.autoresizingMask=UIViewAutoresizingFlexibleWidth|UIViewAutoresizingFlexibleHeight;[self.view addSubview:pad];
    self.touchPad=pad;__weak TOTKGameController *weakSelf=self;
    pad.settingsRequested=^BOOL{return [weakSelf openSettings];};
    self.fpsOverlay=[[DmFPSOverlay alloc] initWithPresentedFrames:^{return fps_overlay_displayed_frames();}
        generatedFrames:^{return fps_overlay_generated_frames();} initiallyVisible:fps_overlay_enabled()];
    [self.view addSubview:self.fpsOverlay];
}
- (void)updateDisplayGeometry {
    CGFloat scale=self.view.window.screen.nativeScale;
    if(!(scale>0))scale=self.view.traitCollection.displayScale;
    dm_display_geometry g=totk_scene_picture(self.view.bounds.size.width,self.view.bounds.size.height,scale,runtime_ios_picture_aspect(),runtime_ios_live_display_profile());
    if(g.valid){
        CGRect picture=CGRectMake(g.picture.x,g.picture.y,g.picture.width,g.picture.height);
        if(!CGRectEqualToRect(self.gameView.frame,picture)){
            [self.touchPad invalidateDisplayGeometry];
            self.gameView.frame=picture;
        }
        if(runtime_ios_update_geometry((CAMetalLayer *)self.gameView.layer,self.gameView))
            [self.touchPad invalidateDisplayGeometry];
    }
    [self.fpsOverlay placeInBounds:self.view.bounds safeArea:self.view.safeAreaInsets];
}
- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    [self updateDisplayGeometry];
}
- (void)viewSafeAreaInsetsDidChange {
    [super viewSafeAreaInsetsDidChange];
    [self.touchPad invalidateDisplayGeometry];
    [self updateDisplayGeometry];
}
- (BOOL)openSettings {
    if(self.presentedViewController || self.touchPad.isSuspended)return NO;
    self.touchPad.suspended=YES;runtime_ios_settings_open(1);
    __weak TOTKGameController *weakSelf=self;
    UIViewController *settings=TOTKSettingsPresentation(^{
        TOTKGameController *controller=weakSelf;if(!controller)return;
        [controller.touchPad reloadPreferences];controller.touchPad.suspended=NO;
        runtime_ios_settings_open(0);
    });
    [self presentViewController:settings animated:YES completion:nil];return YES;
}
- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    [self updateDisplayGeometry];
    runtime_device_motion_orientation((int)self.view.window.windowScene.interfaceOrientation);
    static BOOL started; if (started) return; started = YES;
    [[TOTKCloudSave shared] prepareForController:self completion:^{
        pthread_attr_t attr; pthread_attr_init(&attr); pthread_attr_setstacksize(&attr, 64u << 20);
        pthread_t thread; pthread_create(&thread, &attr, run_host, NULL);
        pthread_detach(thread); pthread_attr_destroy(&attr);
    }];
}
- (void)viewWillTransitionToSize:(CGSize)size withTransitionCoordinator:(id<UIViewControllerTransitionCoordinator>)coordinator {
    [super viewWillTransitionToSize:size withTransitionCoordinator:coordinator];
    [self.touchPad invalidateDisplayGeometry];
    // Invalidate before rotation, then use the actual scene's completed interface basis.
    runtime_device_motion_orientation(UIInterfaceOrientationUnknown);
    [coordinator animateAlongsideTransition:nil completion:^(id<UIViewControllerTransitionCoordinatorContext> context) {
        (void)context;[self updateDisplayGeometry];runtime_device_motion_orientation((int)self.view.window.windowScene.interfaceOrientation);
    }];
}
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }
- (BOOL)prefersStatusBarHidden { return YES; }
- (UIInterfaceOrientationMask)supportedInterfaceOrientations { return UIInterfaceOrientationMaskLandscape; }
@end

@interface TOTKScene : UIResponder <UIWindowSceneDelegate> @property(nonatomic,strong) UIWindow *window; @end
@implementation TOTKScene
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    (void)session; (void)options;
    if (![scene isKindOfClass:UIWindowScene.class]) return;
    self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    self.window.rootViewController = [TOTKGameController new];
    [self.window makeKeyAndVisible];
}
- (void)sceneDidBecomeActive:(UIScene *)scene {runtime_device_motion_orientation((int)((UIWindowScene *)scene).interfaceOrientation);runtime_ios_foreground(1);[[TOTKCloudSave shared] checkForChanges];}
- (void)sceneDidEnterBackground:(UIScene *)scene {(void)scene;runtime_ios_foreground(0);[[TOTKCloudSave shared] flushForBackground];}
@end

@interface TOTKApp : UIResponder <UIApplicationDelegate> @end
@implementation TOTKApp
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options {
    (void)options;
    // Controller input does not reset the idle timer; auto-lock would suspend the game mid-play or mid shader import.
    application.idleTimerDisabled = YES;
    // Controller preferences are local to this device; the game still owns the authored rumble and motion-aiming behavior.
    NSUserDefaults *preferences=NSUserDefaults.standardUserDefaults;
    [preferences registerDefaults:@{@"TOTKControllerHaptics":@YES,@"TOTKAdaptiveTriggers":@YES,
        @"TOTKGyroAiming":@YES,@"TOTKResolution":@"1080",@"TOTKFeedbackStrength":@0.9,@"TOTKTriggerStrength":@0.55}];
    totk_simple_settings_initialize(preferences);
    BOOL haptics=[preferences boolForKey:@"TOTKControllerHaptics"];
    BOOL triggers=[preferences boolForKey:@"TOTKAdaptiveTriggers"];
    totk_ios_feedback_environment(haptics,triggers,[preferences doubleForKey:@"TOTKFeedbackStrength"],
        [preferences doubleForKey:@"TOTKTriggerStrength"]);
    // Keep stable guest handles across live source switches; Off is neutral/disconnected in the shared ring.
    setenv("TOTK_GYRO","1",1);
    NSFileManager *fm = NSFileManager.defaultManager;
    NSString *docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,NSUserDomainMask,YES).firstObject;
    NSString *res = [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"GameData"];
    for (NSString *name in @[@"save", @"temp", @"shader-cache", @"game-slot"]) [fm createDirectoryAtPath:[docs stringByAppendingPathComponent:name] withIntermediateDirectories:YES attributes:nil error:nil];
    NSString *log = [docs stringByAppendingPathComponent:@"log.txt"];
    if (freopen(log.fileSystemRepresentation,"w",stderr)) setvbuf(stderr,NULL,_IOLBF,0);
    // Display profiles pair their signed camera/UI patches with device-local resolution preferences.
    NSString *maximum = [NSBundle.mainBundle objectForInfoDictionaryKey:@"TOTKOutputResolution"];
    if (maximum.length) {
        NSString *aspect=[NSBundle.mainBundle objectForInfoDictionaryKey:@"TOTKDisplayAspect"];
        BOOL ipad=[aspect isEqual:@"4:3"],iphoneNative=[aspect isEqual:@"239:110"];
        // Apply the requested 1080p upgrade once; later Settings choices remain local and persistent.
        if(iphoneNative && ![NSUserDefaults.standardUserDefaults boolForKey:@"TOTKiPhone1080Configured"]){
            [NSUserDefaults.standardUserDefaults setObject:@"1080" forKey:@"TOTKResolution"];
            [NSUserDefaults.standardUserDefaults setBool:YES forKey:@"TOTKiPhoneNativeAspectConfigured"];
            [NSUserDefaults.standardUserDefaults setBool:YES forKey:@"TOTKiPhone1080Configured"];
        }
        NSString *preset = [NSUserDefaults.standardUserDefaults stringForKey:@"TOTKResolution"] ?: (ipad?@"native":@"1080");
        NSDictionary *sizes = ipad?@{@"native":maximum,@"1440":@"2560x1920",@"1080":@"1920x1440",@"720":@"1280x960"}:
            @{@"native":maximum,@"1080":@"1920x1080",@"900":@"1600x900",@"720":@"1280x720",@"540":@"960x540"};
        NSString *selected = sizes[preset] ?: maximum;
        setenv("TOTK_OUTPUT_RES",maximum.UTF8String,1);
        setenv("TOTK_OUTPUT_START_RES",selected.UTF8String,1);
        setenv("TOTK_RENDER_RES",selected.UTF8String,1);
        setenv("TOTK_IOS_DRAWABLE_H",[selected isEqualToString:maximum] ? "0" : [selected componentsSeparatedByString:@"x"].lastObject.UTF8String,1);
        unsetenv("TOTK_IPAD_ASPECT_PATCH");unsetenv("TOTK_IPHONE_ASPECT_PATCH");unsetenv("TOTK_ASPECT_UI_PATCHED");
        unsetenv("TOTK_LIVE_DISPLAY");
        if(ipad){setenv("TOTK_ASPECT","4:3",1);setenv("TOTK_IPAD_ASPECT_PATCH","1",1);
            if([[NSBundle.mainBundle objectForInfoDictionaryKey:@"TOTKLiveDisplayNarrow"] boolValue]) {
                setenv("TOTK_LIVE_DISPLAY","1",1);setenv("TOTK_ASPECT_UI_PATCHED","1",1);
            }
        }
        else if(iphoneNative){setenv("TOTK_ASPECT","239:110",1);setenv("TOTK_IPHONE_ASPECT_PATCH","1",1);setenv("TOTK_ASPECT_UI_PATCHED","1",1);}
        else {unsetenv("TOTK_ASPECT");}
        fprintf(stderr,"[totk] display preset %s: scene and output %s, maximum %s\n",preset.UTF8String,selected.UTF8String,maximum.UTF8String);
    }
    setenv("TOTK_ROMFS",[res stringByAppendingPathComponent:@"romfs"].fileSystemRepresentation,1);
    setenv("TOTK_OPERATION_MODE","docked",1);
    setenv("TOTK_ROMFS_PATCHES",[res stringByAppendingPathComponent:@"romfs-patches"].fileSystemRepresentation,1);
    setenv("TOTK_ROMFS_PATCHES_OUT",[res stringByAppendingPathComponent:@"romfs-patches-out"].fileSystemRepresentation,1);
    setenv("TOTK_SHARED_FONTS",[res stringByAppendingPathComponent:@"shared-fonts"].fileSystemRepresentation,1);
    setenv("TOTK_SHADER_SEED_DIR",[res stringByAppendingPathComponent:@"shader-seed"].fileSystemRepresentation,1);
    setenv("TOTK_SHADER_CACHE",[docs stringByAppendingPathComponent:@"shader-cache"].fileSystemRepresentation,1);
    // Shaders the shared list lacks (totk_shader_contrib.h), in the Files app for players who want to send them; only the Mac asks.
    setenv("TOTK_SHADER_CONTRIB",[docs stringByAppendingPathComponent:@"shader-list-contribution.txt"].fileSystemRepresentation,1);
    setenv("DM_NVN_SHMAP",[res stringByAppendingPathComponent:@"shader-seed/shader_map.tsv"].fileSystemRepresentation,1);
    setenv("DM_MSL_DIR",[res stringByAppendingPathComponent:@"shader-seed"].fileSystemRepresentation,1);
    NSString *font = [res stringByAppendingPathComponent:@"font"]; setenv("TOTK_FONT_DIR",font.fileSystemRepresentation,1);
    // Same player environment as the Mac launcher (app/launcher.m); the bundle is read-only, so state goes to Documents.
    setenv("TOTK_PROFILE_DIR",[docs stringByAppendingPathComponent:@"save"].fileSystemRepresentation,1);
    setenv("TOTK_GAME_SLOT_DIR",[docs stringByAppendingPathComponent:@"game-slot"].fileSystemRepresentation,1);
    setenv("TOTK_ROMFS_PROOF_DIR",docs.fileSystemRepresentation,1);   // hash the 17 GB RomFS once, not on every launch
    for (NSString *name in @[@"TOTK_GPU_ALLOW_SUBMIT",@"TOTK_GPU_WIDEN",@"TOTK_GPU_BLOCKRT",@"TOTK_GPU_MIDCLEAR"]) setenv(name.UTF8String,"1",1);
    setenv("TOTK_SLOT_OWNER","user",0); setenv("TOTK_SLOT_PURPOSE","player",0);
    unsetenv("TOTK_AUTOLOAD");
    chdir(docs.fileSystemRepresentation);
    host_argv[0] = strdup("TOTK"); host_argv[1] = strdup(res.fileSystemRepresentation); host_argv[2] = strdup("boot");
    return YES;
}
@end

int main(int argc, char **argv) { @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(TOTKApp.class)); } }
