/* One iOS motion owner: controller/device arbitration, original-age admission and shared guest six-axis feed. */
#include <TargetConditionals.h>
#if TARGET_OS_IOS
#import <UIKit/UIKit.h>
#import <CoreMotion/CoreMotion.h>
#import <GameController/GameController.h>
#include <stdatomic.h>
#include "runtime.h"
#include "motion_source.h"
#include "simple_settings_ios.h"

static dispatch_queue_t motionQueue;
static dispatch_source_t watchdog;
static NSOperationQueue *sensorQueue;
static CMMotionManager *manager;
static GCController *motionController;
static TOTKMotionRouter router;
static TOTKMotionRecovery recovery;
static BOOL deviceRunning,controllerRunning,controllerProbing;
static uint64_t controllerLease,deviceLease,startedAt,probeAt,probeDeadline,retryAt,lastReport;
static uint64_t raw[3],calibrationGeneration,errors,stalls;
static _Atomic uint64_t acceptedTotal;
static _Atomic int writingReport;
static NSString *lastError=@"";
static void reconcile(void);
static void ingest(TOTKMotionSource source,uint64_t epoch,uint64_t lease,TOTKMotionSample sample);

static NSString *source_name(TOTKMotionSource source){return source==TOTK_SENSOR_DEVICE?@"device":source==TOTK_SENSOR_CONTROLLER?@"controller":@"none";}
static BOOL controller_available(void){return motionController.motion.hasRotationRate;}
static BOOL device_available(void){return manager.deviceMotionAvailable &&
    ([CMMotionManager availableAttitudeReferenceFrames]&CMAttitudeReferenceFrameXArbitraryZVertical)!=0;}
static void stop_controller(void){
    controllerLease++;
    GCMotion *motion=motionController.motion;motion.valueChangedHandler=nil;
    if(motion.sensorsRequireManualActivation)motion.sensorsActive=NO;
    controllerRunning=controllerProbing=NO;recovery=(TOTKMotionRecovery){0};
}
static void stop_device(void){deviceLease++;[manager stopDeviceMotionUpdates];deviceRunning=NO;}
static void invalidate(void){
    totk_motion_router_invalidate(&router);stop_controller();stop_device();runtime_motion_reset();
    startedAt=runtime_now_ns();calibrationGeneration++;
}
static void report(void){
    uint64_t now=runtime_now_ns();uint64_t values,reads,fusionReads,returned;
    runtime_hid_feedback_stats(&values,&reads);runtime_motion_stats(&fusionReads,&returned);(void)values;
    NSDictionary *info=@{@"requested":@[@"off",@"controller",@"device",@"auto"][router.requested],
        @"selected":source_name(router.selected),@"epoch":@(router.epoch),@"foreground":@(router.active),
        @"controllerAvailable":@(controller_available()),@"deviceAvailable":@(device_available()),
        @"controllerSensorActive":@(controllerRunning),@"controllerRecoveryProbe":@(controllerProbing),
        @"deviceSensorActive":@(deviceRunning),@"raw":@[@(raw[1]),@(raw[2])],
        @"accepted":@[@(router.accepted[1]),@(router.accepted[2])],
        @"rejected":@[@(router.rejected[1]),@(router.rejected[2])],
        @"lastSampleAgeMs":router.last_ns && now>=router.last_ns?@((now-router.last_ns)/1e6):@(-1),
        @"interfaceBasis":@(router.orientation),@"calibrationGeneration":@(calibrationGeneration),
        @"calibration":@"reference reseed; CoreMotion fused sensor calibration; no learned turn bias",
        @"deviceTimestampBasis":@"CMLogItem mach-absolute seconds",
        @"controllerTimestampBasis":@"GCMotion callback entry; SDK exposes no sensor timestamp",
        @"guestReadCalls":@(reads),@"fusionReadCalls":@(fusionReads),@"guestStatesReturned":@(returned),
        @"sensorErrors":@(errors),@"stalls":@(stalls),@"lastError":lastError,
        @"logicalPlayer":@0,@"buildVersion":NSBundle.mainBundle.infoDictionary[@"CFBundleVersion"]?:@"unknown"};
    if(atomic_exchange(&writingReport,1))return;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{@autoreleasepool{
        NSString *docs=NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,NSUserDomainMask,YES).firstObject;
        [[NSJSONSerialization dataWithJSONObject:info options:NSJSONWritingPrettyPrinted error:nil]
            writeToFile:[docs stringByAppendingPathComponent:@"motion-source.json"] options:NSDataWritingAtomic error:nil];
        atomic_store(&writingReport,0);
    }});
}
static void start_controller(BOOL probe){
    if(controllerRunning || !controller_available())return;
    controllerRunning=YES;controllerProbing=probe;
    uint64_t epoch=router.epoch,lease=++controllerLease;
    GCMotion *motion=motionController.motion;
    motion.valueChangedHandler=^(GCMotion *value){
        // Capture at callback entry, before crossing queues. Unlike CoreMotion, GCMotion has no sample timestamp.
        uint64_t timestamp=runtime_now_ns();GCAcceleration a=value.acceleration;
        if(value.hasGravityAndUserAcceleration){a.x=value.gravity.x+value.userAcceleration.x;
            a.y=value.gravity.y+value.userAcceleration.y;a.z=value.gravity.z+value.userAcceleration.z;}
        GCRotationRate g=value.rotationRate;
        TOTKMotionSample sample={{a.x,a.y,a.z},{g.x,g.y,g.z},timestamp};
        dispatch_async(motionQueue,^{ingest(TOTK_SENSOR_CONTROLLER,epoch,lease,sample);});
    };
    if(motion.sensorsRequireManualActivation)motion.sensorsActive=YES;
    if(probe){probeDeadline=runtime_now_ns()+200000000;recovery=(TOTKMotionRecovery){0};}
}
static void start_device(void){
    if(deviceRunning || !device_available() || router.orientation==TOTK_LANDSCAPE_UNKNOWN)return;
    deviceRunning=YES;uint64_t epoch=router.epoch,lease=++deviceLease;
    manager.deviceMotionUpdateInterval=0.01;
    [manager startDeviceMotionUpdatesUsingReferenceFrame:CMAttitudeReferenceFrameXArbitraryZVertical
        toQueue:sensorQueue withHandler:^(CMDeviceMotion *value,NSError *error){
        if(error || !value){
            NSString *message=error.localizedDescription?:@"Device motion returned no sample";
            dispatch_async(motionQueue,^{
                if(epoch!=router.epoch || lease!=deviceLease)return;
                errors++;lastError=message;invalidate();retryAt=runtime_now_ns()+1000000000;report();
            });return;
        }
        CMAcceleration a=value.gravity,u=value.userAcceleration;CMRotationRate g=value.rotationRate;
        TOTKMotionSample sample={{a.x+u.x,a.y+u.y,a.z+u.z},{g.x,g.y,g.z},totk_motion_uptime_ns(value.timestamp)};
        dispatch_async(motionQueue,^{ingest(TOTK_SENSOR_DEVICE,epoch,lease,sample);});
    }];
}
static void select_source(TOTKMotionSource source){
    invalidate();router.selected=source;retryAt=0;
    // Keep the existing controller tuning. Fused device motion adds no dead zone or automatic turn-bias learner.
    runtime_motion_deadzone_default(source==TOTK_SENSOR_CONTROLLER?0.35:0);
    if(source==TOTK_SENSOR_CONTROLLER)start_controller(NO);
    else if(source==TOTK_SENSOR_DEVICE){start_device();probeAt=runtime_now_ns()+1000000000;}
    report();
}
static void ingest(TOTKMotionSource source,uint64_t epoch,uint64_t lease,TOTKMotionSample sample){
    uint64_t now=runtime_now_ns();raw[source]++;
    if(epoch!=router.epoch || lease!=(source==TOTK_SENSOR_DEVICE?deviceLease:controllerLease) || !router.active){
        router.rejected[source]++;return;
    }
    if(source==TOTK_SENSOR_CONTROLLER && controllerProbing){
        if(!totk_motion_controller_nonempty(sample) || !totk_motion_sample_valid(sample,now,recovery.last)){router.rejected[source]++;return;}
        if(totk_motion_recovery_ready(&recovery,sample.timestamp_ns))select_source(TOTK_SENSOR_CONTROLLER);
        return; // A recovery-probe measurement never enters the guest; restart under the new epoch first.
    }
    if(!totk_motion_router_accept(&router,source,epoch,sample,now))return;
    if(source==TOTK_SENSOR_DEVICE && !totk_motion_device_basis(router.orientation,&sample))return;
    runtime_motion_feed(sample.acc,sample.rate,sample.timestamp_ns);atomic_fetch_add(&acceptedTotal,1);
}
static void reconcile(void){
    uint64_t now=runtime_now_ns();
    if(!router.active || router.requested==TOTK_MOTION_OFF){
        if(router.selected!=TOTK_SENSOR_NONE || controllerRunning || deviceRunning)invalidate();return;
    }
    if(router.selected!=TOTK_SENSOR_NONE){
        if(totk_motion_watchdog_expired(&router,startedAt,now)){
            TOTKMotionSource failed=router.selected;stalls++;invalidate();
            if(totk_motion_stall_fallback(router.requested,failed,device_available() &&
                    router.orientation!=TOTK_LANDSCAPE_UNKNOWN)==TOTK_SENSOR_DEVICE)
                select_source(TOTK_SENSOR_DEVICE);
            else retryAt=now+1000000000;
        }
    }
    if(router.selected==TOTK_SENSOR_NONE && now>=retryAt){
        TOTKMotionSource source=totk_motion_initial_source(router.requested,controller_available(),
            device_available(),router.orientation!=TOTK_LANDSCAPE_UNKNOWN);
        if(source!=TOTK_SENSOR_NONE)select_source(source);
    }
    if(router.requested==TOTK_MOTION_AUTO && router.selected==TOTK_SENSOR_DEVICE && controller_available()){
        // Recovery requires a short bounded probe, not permanently running the unselected manual sensor.
        if(controllerProbing && now>=probeDeadline){stop_controller();probeAt=now+2000000000;}
        if(!controllerRunning && now>=probeAt)start_controller(YES);
    }
    if(!lastReport || now-lastReport>=2000000000){lastReport=now;report();}
}
void runtime_device_motion_start(void){
    static dispatch_once_t once;
    dispatch_once(&once,^{
        motionQueue=dispatch_queue_create("org.macnativegames.totk.motion",dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL,QOS_CLASS_USER_INTERACTIVE,0));
        sensorQueue=[NSOperationQueue new];sensorQueue.maxConcurrentOperationCount=1;sensorQueue.qualityOfService=NSQualityOfServiceUserInteractive;
        manager=[CMMotionManager new];router.epoch=1;
        watchdog=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,motionQueue);
        dispatch_source_set_event_handler(watchdog,^{@autoreleasepool{reconcile();}});
        dispatch_source_set_timer(watchdog,DISPATCH_TIME_FOREVER,20*NSEC_PER_MSEC,2*NSEC_PER_MSEC);dispatch_resume(watchdog);
    });
}
static TOTKMotionMode preference(void){
    return totk_simple_motion_preference(NSUserDefaults.standardUserDefaults);
}
void runtime_device_motion_focus(int active){
    runtime_device_motion_start();TOTKMotionMode requested=preference();
    NSUserDefaults *p=NSUserDefaults.standardUserDefaults;BOOL recenter=[p boolForKey:@"TOTKGyroRecenter"];
    if(recenter)[p setBool:NO forKey:@"TOTKGyroRecenter"];
    dispatch_async(motionQueue,^{
        BOOL changed=router.active!=(active!=0) || router.requested!=requested;
        router.active=active!=0;router.requested=requested;
        if(changed || recenter){invalidate();retryAt=0;}
        dispatch_source_set_timer(watchdog,router.active && router.requested!=TOTK_MOTION_OFF?dispatch_time(DISPATCH_TIME_NOW,0):DISPATCH_TIME_FOREVER,20*NSEC_PER_MSEC,2*NSEC_PER_MSEC);
        reconcile();report();
    });
}
void runtime_device_motion_orientation(int orientation){
    runtime_device_motion_start();
    TOTKMotionOrientation basis=orientation==UIInterfaceOrientationLandscapeLeft?TOTK_LANDSCAPE_LEFT:
        orientation==UIInterfaceOrientationLandscapeRight?TOTK_LANDSCAPE_RIGHT:TOTK_LANDSCAPE_UNKNOWN;
    dispatch_async(motionQueue,^{if(router.orientation!=basis){router.orientation=basis;invalidate();retryAt=0;reconcile();report();}});
}
void runtime_device_motion_controller(GCController *controller){
    runtime_device_motion_start();
    dispatch_async(motionQueue,^{
        if(motionController==controller)return;
        invalidate();motionController=controller;retryAt=0;reconcile();report();
    });
}
uint64_t runtime_device_motion_sample_count(void){return atomic_load(&acceptedTotal);}
#endif
