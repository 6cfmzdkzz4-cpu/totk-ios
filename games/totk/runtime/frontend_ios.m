/* Native iOS view and VI service adapter: the iPhone counterpart of frontend.m (same exported functions).
   The app (ios/app/Sources/main.m) owns UIKit: it creates the full-screen Metal view, hands its layer over with
   runtime_ios_attach_layer, and calls the loader's main on a game thread. No guest frame is synthesized. */
#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>
#include <stdatomic.h>
#include "runtime.h"
#include "totk_scene_geometry.h"
#include "totk_live_display.h"
#include "totk_ui_lang.h"
static CAMetalLayer *metalLayer;
static UIView *bootOverlay;
static UILabel *bootStage,*bootDetail;
static UIProgressView *bootProgress;
static uint64_t bootStartNS;
static _Atomic uint64_t presentedFrames;
static CADisplayLink *displayLink;
static pthread_mutex_t vsyncLock=PTHREAD_MUTEX_INITIALIZER;
static void *vsyncEvents[16];
static unsigned vsyncCount;
void hle_bind_system_event(void *,int);
void hle_signal_system_event(void *);
void *front_metal_layer(void){return (__bridge void *)metalLayer;}
void front_frame_presented(void){
 if(atomic_fetch_add_explicit(&presentedFrames,1,memory_order_relaxed)==0){
  fprintf(stderr,"[totk-boot] %.3f first presented frame\n",(runtime_now_ns()-bootStartNS)/1e9);
  dispatch_async(dispatch_get_main_queue(), ^{bootOverlay.hidden=YES;});
 }
}
void runtime_boot_progress(const char *stage,uint64_t done,uint64_t total){
 static const char *last;
 if(stage!=last){fprintf(stderr,"[totk-boot] %.3f %s\n",bootStartNS?(runtime_now_ns()-bootStartNS)/1e9:0,stage?stage:"startup");last=stage;}
 NSString *s=[NSString stringWithUTF8String:stage?stage:TOTK_UI("正在启动","Starting")];
 dispatch_async(dispatch_get_main_queue(), ^{
  if(!bootOverlay||bootOverlay.hidden)return;
  bootStage.text=s;
  if(total){bootDetail.text=[NSString stringWithFormat:@"%.1f / %.1f GB",done/1e9,total/1e9];bootProgress.hidden=NO;bootProgress.progress=(float)((double)done/(double)total);}
  else {bootDetail.text=@(TOTK_UI("正在处理，请稍候","Working, please wait"));bootProgress.hidden=YES;}
 });
}
void runtime_boot_count(const char *stage,uint64_t done,uint64_t total){
 /* Steps counted in items, not bytes (the shader import: tens of thousands of files, minutes on a phone). */
 static const char *last;
 if(stage!=last){fprintf(stderr,"[totk-boot] %.3f %s\n",bootStartNS?(runtime_now_ns()-bootStartNS)/1e9:0,stage?stage:"startup");last=stage;}
 NSString *s=[NSString stringWithUTF8String:stage?stage:TOTK_UI("正在启动","Starting")];
 dispatch_async(dispatch_get_main_queue(), ^{
  if(!bootOverlay||bootOverlay.hidden)return;
  bootStage.text=s;bootDetail.text=[NSString stringWithFormat:@"%llu / %llu",(unsigned long long)done,(unsigned long long)total];
  if(total){bootProgress.hidden=NO;bootProgress.progress=(float)((double)done/(double)total);}
 });
}
void runtime_boot_status(const char *stage,const char *detail){
 if(bootStartNS)fprintf(stderr,"[totk-boot] %.3f %s — %s\n",(runtime_now_ns()-bootStartNS)/1e9,stage?stage:"startup",detail?detail:"");
 NSString *s=[NSString stringWithUTF8String:stage?stage:TOTK_UI("正在启动","Starting")];
 NSString *d=[NSString stringWithUTF8String:detail?detail:TOTK_UI("正在处理，请稍候","Working, please wait")];
 dispatch_async(dispatch_get_main_queue(), ^{if(!bootOverlay||bootOverlay.hidden)return;bootStage.text=s;bootDetail.text=d;bootProgress.hidden=YES;});
}
/* Background shader/pipeline warm-up progress (iOS ships with the warm manifest off, but the hooks are shared with the Mac front end). */
void runtime_warm_progress(uint64_t done,uint64_t total,uint64_t shaders,uint64_t shader_total,uint64_t pipelines,uint64_t pipeline_total){
 (void)shaders;(void)shader_total;(void)pipelines;(void)pipeline_total;
 dispatch_async(dispatch_get_main_queue(), ^{
  if(!bootOverlay||bootOverlay.hidden)return;
  double frac=total ? fmin(1.0,(double)done/(double)total) : 1.0;
  bootStage.text=[NSString stringWithFormat:@(TOTK_UI("预热着色器与管线 %.0f%%","Warming shaders and pipelines %.0f%%")),floor(frac*100.0)];
  bootDetail.text=[NSString stringWithFormat:@(TOTK_UI("已处理 %llu/%llu 项","%llu/%llu items")),(unsigned long long)done,(unsigned long long)total];
  bootProgress.hidden=NO;bootProgress.progress=(float)frac;
 });
}
void runtime_warm_done(void){}
uint64_t front_game_res(int docked){(void)docked;return (720ULL<<32)|1280;}
static _Atomic int appActive=1;
static _Atomic int appForeground=1, settingsOpen;
int runtime_front_focus(void){return atomic_load(&appActive);}
void mtl_app_active(int active);
/* Not active (background, Control Center, lock): stop GPU submission (iOS rejects background GPU work) and tell the game focus changed, like the Mac window. */
static void app_active_changed(int active){
 atomic_store(&appForeground,active);
 active=active && !atomic_load(&settingsOpen);
 runtime_device_motion_focus(active);
 runtime_feedback_focus(active);
 if(atomic_exchange(&appActive,active)==active)return;
 if(!active)runtime_input_clear();
 else runtime_gamepad_refresh();
 mtl_app_active(active);runtime_oe_push_message(15);
 fprintf(stderr,"[totk] app %s\n",active?"became active":"resigned active: GPU submission paused");
}
void runtime_ios_settings_open(int open){
 atomic_store(&settingsOpen,open!=0);
 app_active_changed(atomic_load(&appForeground));
}
void runtime_ios_foreground(int active){app_active_changed(active);}
void runtime_front_request_close(void){fprintf(stderr,"[totk] close requested; iOS apps are closed by the user\n");}
void runtime_front_text_test(const char *op,const char *text){(void)text;fprintf(stderr,"[totk] text entry test \"%s\" ignored: iOS has no text entry bar (the keyboard request is cancelled)\n",op?op:"");}

@interface TOTKVsync:NSObject
@end
@implementation TOTKVsync
-(void)tick:(CADisplayLink *)l{(void)l;pthread_mutex_lock(&vsyncLock);for(unsigned i=0;i<vsyncCount;i++)hle_signal_system_event(vsyncEvents[i]);pthread_mutex_unlock(&vsyncLock);}
@end
static TOTKVsync *vsyncTarget;

/* Baseline for the selected signed preparation, including explicit fallback. */
int runtime_ios_live_display_profile(void) {
 const char *value=getenv("TOTK_LIVE_DISPLAY");return value && !strcmp(value,"1");
}
double runtime_ios_picture_aspect(void){
 double w=0,h=0;char tail;const char *value=getenv("TOTK_ASPECT");
 if(value && sscanf(value,"%lf:%lf%c",&w,&h,&tail)==2 && isfinite(w) && isfinite(h) && w>0 && h>0)return w/h;
 return 16.0/9.0;
}
void mtl_present_geometry_changed(void);
int runtime_ios_update_geometry(CAMetalLayer *layer,UIView *host){
 if(!layer || !host)return 0;
 CGFloat scale=host.window.screen.nativeScale;
 if(!(scale>0))scale=host.traitCollection.displayScale;
 const char *value=getenv("TOTK_IOS_DRAWABLE_H");long maximum=value?strtol(value,NULL,10):720;
 dm_display_geometry g=totk_scene_drawable(host.bounds.size.width,host.bounds.size.height,scale,maximum);
 if(!g.valid)return 0; /* transient zero/nonfinite bounds do not replace the last drawable */
 static __weak CAMetalLayer *previousLayer;
 static CGSize previousBounds;static CGFloat previousScale;
 static UIEdgeInsets previousInsets;static UIInterfaceOrientation previousOrientation;
 UIEdgeInsets insets=host.safeAreaInsets;UIInterfaceOrientation orientation=host.window.windowScene.interfaceOrientation;
 BOOL sceneChanged=previousLayer!=layer || !CGSizeEqualToSize(previousBounds,host.bounds.size) || previousScale!=scale ||
     !UIEdgeInsetsEqualToEdgeInsets(previousInsets,insets) || previousOrientation!=orientation;
 if(runtime_ios_live_display_profile()) {
  double safe[]={insets.top,insets.left,insets.bottom,insets.right};
  if(!runtime_live_display_request_scene_mode(host.bounds.size.width,host.bounds.size.height,scale,safe,(unsigned)orientation,
      !totk_scene_narrow_supported(host.bounds.size.width,host.bounds.size.height)))return 0;
 }
 previousLayer=layer;previousBounds=host.bounds.size;previousScale=scale;previousInsets=insets;previousOrientation=orientation;
 CGSize pixels=CGSizeMake(g.pixel_width,g.pixel_height);
 if(CGSizeEqualToSize(layer.drawableSize,pixels) && layer.contentsScale==scale)return sceneChanged;
 layer.contentsScale=scale;layer.drawableSize=pixels;
 mtl_present_geometry_changed(); /* atomic request; worker owns interpolation history */
 fprintf(stderr,"[totk-display] drawable %ux%u scale %.3f baseline %.6f live-narrow=%d\n",g.pixel_width,g.pixel_height,scale,runtime_ios_picture_aspect(),runtime_ios_live_display_profile());
 return 1;
}

/* Called on the main thread by the app before the game thread starts. */
void runtime_ios_attach_layer(CAMetalLayer *layer,UIView *host){
 bootStartNS=runtime_now_ns();
 metalLayer=layer;metalLayer.device=MTLCreateSystemDefaultDevice();metalLayer.pixelFormat=MTLPixelFormatBGRA8Unorm;metalLayer.framebufferOnly=NO;
 runtime_ios_update_geometry(layer,host);
 bootOverlay=[[UIView alloc] initWithFrame:host.bounds];bootOverlay.autoresizingMask=UIViewAutoresizingFlexibleWidth|UIViewAutoresizingFlexibleHeight;
 bootOverlay.backgroundColor=[UIColor colorWithRed:0.035 green:0.075 blue:0.09 alpha:1];
 CGFloat w=host.bounds.size.width,h=host.bounds.size.height;
 bootStage=[[UILabel alloc] initWithFrame:CGRectMake(0,h/2-50,w,36)];bootStage.text=@(TOTK_UI("正在启动海拉鲁","Starting Hyrule"));bootStage.textColor=UIColor.whiteColor;
 bootStage.font=[UIFont systemFontOfSize:24 weight:UIFontWeightSemibold];bootStage.textAlignment=NSTextAlignmentCenter;
 bootDetail=[[UILabel alloc] initWithFrame:CGRectMake(0,h/2-8,w,24)];bootDetail.text=@(TOTK_UI("正在准备游戏环境","Preparing the game environment"));bootDetail.textColor=[UIColor colorWithWhite:0.72 alpha:1];
 bootDetail.font=[UIFont systemFontOfSize:14];bootDetail.textAlignment=NSTextAlignmentCenter;
 bootProgress=[[UIProgressView alloc] initWithFrame:CGRectMake(w/2-140,h/2+30,280,8)];bootProgress.hidden=YES;
 [bootOverlay addSubview:bootStage];[bootOverlay addSubview:bootDetail];[bootOverlay addSubview:bootProgress];[host addSubview:bootOverlay];
 NSNotificationCenter *nc=NSNotificationCenter.defaultCenter;
 [nc addObserverForName:UIApplicationWillResignActiveNotification object:nil queue:nil usingBlock:^(NSNotification *n){(void)n;app_active_changed(0);}];
 [nc addObserverForName:UIApplicationDidBecomeActiveNotification object:nil queue:nil usingBlock:^(NSNotification *n){(void)n;app_active_changed(1);}];
 runtime_feedback_focus(UIApplication.sharedApplication.applicationState==UIApplicationStateActive);
 vsyncTarget=[TOTKVsync new];displayLink=[CADisplayLink displayLinkWithTarget:vsyncTarget selector:@selector(tick:)];
 displayLink.preferredFrameRateRange=CAFrameRateRangeMake(60,60,60);[displayLink addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
}
/* The app already runs the loader on a game thread with a large stack; the event loop belongs to UIKit. */
void runtime_front_run(void(*callback)(void)){
 if(!metalLayer)runtime_fail("iOS view not attached before boot");
 runtime_gamepad_start();
 /* Loader initialization resets the observer after view attachment. Rearm its
    touch-only consumer here, even when no physical controller is connected. */
 runtime_feedback_focus(atomic_load(&appActive));
 @autoreleasepool{callback();}
}
static int viReady;
static unsigned displayIdentity,layerIdentity;
static void vi_initialize(void){if(!metalLayer)runtime_fail("VI requires the native iOS view");viReady=1;fprintf(stderr,"[totk] native VI display initialized\n");}
static uint32_t vi_open(void **out){if(!viReady||!out)return 0xf601;*out=&displayIdentity;return 0;}
static uint32_t vi_layer(void **out,void *display){if(!out||display!=&displayIdentity)return 0xf601;*out=&layerIdentity;return 0;}
static uint32_t vi_window(void **out,void *layer){if(!out||layer!=&layerIdentity)return 0xf601;*out=front_metal_layer();return 0;}
static uint32_t vi_vsync(void *ev,void *display){
 if(!ev||display!=&displayIdentity)return 0xf601;hle_bind_system_event(ev,1);
 pthread_mutex_lock(&vsyncLock);if(vsyncCount>=16)runtime_fail("VI display event limit");vsyncEvents[vsyncCount++]=ev;pthread_mutex_unlock(&vsyncLock);return 0;
}
void *runtime_front_override(const char *name){
 static const struct{const char *name;void *fn;} entries[]={
 {"_ZN2nn2vi10InitializeEv",vi_initialize},{"_ZN2nn2vi18OpenDefaultDisplayEPPNS0_7DisplayE",vi_open},
 {"_ZN2nn2vi11CreateLayerEPPNS0_5LayerEPNS0_7DisplayE",vi_layer},{"_ZN2nn2vi15GetNativeWindowEPPvPNS0_5LayerE",vi_window},
 {"_ZN2nn2vi20GetDisplayVsyncEventEPNS_2os15SystemEventTypeEPNS0_7DisplayE",vi_vsync}};
 for(unsigned i=0;i<sizeof(entries)/sizeof(*entries);i++)if(!strcmp(name,entries[i].name))return entries[i].fn;return NULL;
}
