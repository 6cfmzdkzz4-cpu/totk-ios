/* Pure motion-source admission and landscape basis; independent of Apple APIs and title memory. */
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

typedef enum { TOTK_MOTION_OFF, TOTK_MOTION_CONTROLLER, TOTK_MOTION_DEVICE, TOTK_MOTION_AUTO } TOTKMotionMode;
typedef enum { TOTK_SENSOR_NONE, TOTK_SENSOR_CONTROLLER, TOTK_SENSOR_DEVICE } TOTKMotionSource;
typedef enum { TOTK_LANDSCAPE_UNKNOWN, TOTK_LANDSCAPE_LEFT, TOTK_LANDSCAPE_RIGHT } TOTKMotionOrientation;
enum { TOTK_MOTION_FRESH_NS=200000000, TOTK_MOTION_RECOVERY_NS=50000000 };
typedef struct { double acc[3], rate[3]; uint64_t timestamp_ns; } TOTKMotionSample;
typedef struct {
    TOTKMotionMode requested;
    TOTKMotionSource selected;
    TOTKMotionOrientation orientation;
    int active;
    uint64_t epoch, last_ns, accepted[3], rejected[3];
} TOTKMotionRouter;
typedef struct { uint64_t since, last; unsigned count; } TOTKMotionRecovery;

static inline int totk_motion_sample_valid(TOTKMotionSample sample,uint64_t now,uint64_t last) {
    if(!sample.timestamp_ns || sample.timestamp_ns>now || now-sample.timestamp_ns>=TOTK_MOTION_FRESH_NS ||
       (last && sample.timestamp_ns<=last))return 0;
    for(int i=0;i<3;i++)if(!isfinite(sample.acc[i]) || !isfinite(sample.rate[i]) ||
        fabs(sample.acc[i])>32 || fabs(sample.rate[i])>200)return 0;
    return 1;
}
/* UIKit interface LandscapeLeft == UIDevice LandscapeRight (home button left).
   Body axes: portrait right/top/out. Guest axes: landscape right/top/out.
   These are proper +/-90-degree rotations, so rate and acceleration use the same R. */
static inline int totk_motion_device_basis(TOTKMotionOrientation orientation,TOTKMotionSample *sample) {
    if(orientation!=TOTK_LANDSCAPE_LEFT && orientation!=TOTK_LANDSCAPE_RIGHT)return 0;
    double sign=orientation==TOTK_LANDSCAPE_LEFT?1:-1;
    double ax=sample->acc[0],gx=sample->rate[0];
    sample->acc[0]=sign*sample->acc[1];sample->acc[1]=-sign*ax;
    sample->rate[0]=sign*sample->rate[1];sample->rate[1]=-sign*gx;
    return 1;
}
static inline void totk_motion_router_invalidate(TOTKMotionRouter *router) {
    router->epoch++;router->selected=TOTK_SENSOR_NONE;router->last_ns=0;
}
static inline void totk_motion_router_select(TOTKMotionRouter *router,TOTKMotionSource source) {
    totk_motion_router_invalidate(router);router->selected=source;
}
static inline int totk_motion_controller_nonempty(TOTKMotionSample sample) {
    for(int i=0;i<3;i++)if(sample.acc[i]!=0 || sample.rate[i]!=0)return 1;
    return 0;
}
static inline int totk_motion_router_accept(TOTKMotionRouter *router,TOTKMotionSource source,
                                           uint64_t epoch,TOTKMotionSample sample,uint64_t now) {
    if(source!=TOTK_SENSOR_CONTROLLER && source!=TOTK_SENSOR_DEVICE)return 0;
    int mode_ok=router->requested==TOTK_MOTION_AUTO ||
        (router->requested==TOTK_MOTION_CONTROLLER && source==TOTK_SENSOR_CONTROLLER) ||
        (router->requested==TOTK_MOTION_DEVICE && source==TOTK_SENSOR_DEVICE);
    if(!router->active || !mode_ok || source!=router->selected || epoch!=router->epoch ||
       (source==TOTK_SENSOR_CONTROLLER && !totk_motion_controller_nonempty(sample)) ||
       (source==TOTK_SENSOR_DEVICE && router->orientation==TOTK_LANDSCAPE_UNKNOWN) ||
       !totk_motion_sample_valid(sample,now,router->last_ns)){
        router->rejected[source]++;return 0;
    }
    router->last_ns=sample.timestamp_ns;router->accepted[source]++;return 1;
}
static inline int totk_motion_recovery_ready(TOTKMotionRecovery *r,uint64_t timestamp) {
    if(!r->last || timestamp<=r->last || timestamp-r->last>100000000){r->since=timestamp;r->count=0;}
    r->last=timestamp;r->count++;
    return r->count>=3 && timestamp>=r->since && timestamp-r->since>=TOTK_MOTION_RECOVERY_NS;
}
/* Core Motion timestamp seconds and runtime_now_ns both use mach_absolute_time.
   Keep original measurement age, never relabel a queued sample with arrival time. */
static inline uint64_t totk_motion_uptime_ns(double seconds) {
    if(!isfinite(seconds) || seconds<=0 || seconds>=(double)UINT64_MAX/1e9)return 0;
    return (uint64_t)(seconds*1e9);
}

static inline TOTKMotionSource totk_motion_initial_source(TOTKMotionMode mode,int controller,int device,int landscape) {
    if((mode==TOTK_MOTION_CONTROLLER || mode==TOTK_MOTION_AUTO) && controller)return TOTK_SENSOR_CONTROLLER;
    if((mode==TOTK_MOTION_DEVICE || mode==TOTK_MOTION_AUTO) && device && landscape)return TOTK_SENSOR_DEVICE;
    return TOTK_SENSOR_NONE;
}
static inline int totk_motion_watchdog_expired(const TOTKMotionRouter *r,uint64_t started,uint64_t now) {
    uint64_t since=r->last_ns?r->last_ns:started;
    return r->selected!=TOTK_SENSOR_NONE && now>=since && now-since>=TOTK_MOTION_FRESH_NS;
}
static inline TOTKMotionSource totk_motion_stall_fallback(TOTKMotionMode mode,TOTKMotionSource failed,int device) {
    return mode==TOTK_MOTION_AUTO && failed==TOTK_SENSOR_CONTROLLER && device?TOTK_SENSOR_DEVICE:TOTK_SENSOR_NONE;
}
