/* Verify source policy, original-age rejection, landscape basis and the real fusion/history consumer. */
#include "../runtime/runtime.h"
#include "../runtime/motion_source.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stdlib.h>
static _Atomic uint64_t clock_ns=1000000000;
uint64_t runtime_now_ns(void){return clock_ns;}
void runtime_hid_feedback_stats(uint64_t *values,uint64_t *reads){*values=*reads=0;}
_Noreturn void runtime_fail(const char *message){fprintf(stderr,"%s\n",message);abort();}
static TOTKMotionSample sample(uint64_t ns){return (TOTKMotionSample){{0,0,-1},{0,0,0},ns};}
static void near(double a,double b){assert(fabs(a-b)<1e-5);}
static void *producer(void *unused){
    (void)unused;const double a[3]={0,0,-1},g[3]={0.01,0,0};
    for(int i=0;i<4000;i++){
        uint64_t now=atomic_fetch_add(&clock_ns,1000000)+1000000;
        runtime_motion_feed(a,g,now);if(i%97==0)runtime_motion_reset();
    }
    return NULL;
}
static void *consumer(void *unused){
    (void)unused;RuntimeMotionState states[RUNTIME_MOTION_MAX];
    for(int i=0;i<4000;i++){
        int n=runtime_motion_read(states,RUNTIME_MOTION_MAX);
        for(int j=0;j<n;j++){
            assert(states[j].sampling>0 && isfinite(states[j].gyro[0]));
            if(j)assert(states[j-1].sampling>states[j].sampling);
        }
    }
    return NULL;
}
int main(void){
    assert(totk_motion_initial_source(TOTK_MOTION_DEVICE,0,1,1)==TOTK_SENSOR_DEVICE);
    assert(totk_motion_initial_source(TOTK_MOTION_DEVICE,1,1,1)==TOTK_SENSOR_DEVICE);
    assert(totk_motion_initial_source(TOTK_MOTION_CONTROLLER,0,1,1)==TOTK_SENSOR_NONE);
    assert(totk_motion_initial_source(TOTK_MOTION_AUTO,1,1,1)==TOTK_SENSOR_CONTROLLER);
    assert(totk_motion_initial_source(TOTK_MOTION_AUTO,0,1,1)==TOTK_SENSOR_DEVICE);
    assert(totk_motion_initial_source(TOTK_MOTION_OFF,1,1,1)==TOTK_SENSOR_NONE);
    assert(totk_motion_initial_source(TOTK_MOTION_DEVICE,0,1,0)==TOTK_SENSOR_NONE);
    assert(totk_motion_stall_fallback(TOTK_MOTION_AUTO,TOTK_SENSOR_CONTROLLER,1)==TOTK_SENSOR_DEVICE);
    assert(totk_motion_stall_fallback(TOTK_MOTION_CONTROLLER,TOTK_SENSOR_CONTROLLER,1)==TOTK_SENSOR_NONE);
    TOTKMotionRouter r={.active=1,.requested=TOTK_MOTION_DEVICE,.orientation=TOTK_LANDSCAPE_LEFT};
    totk_motion_router_select(&r,TOTK_SENSOR_DEVICE);uint64_t epoch=r.epoch;
    TOTKMotionSample s=sample(clock_ns);
    assert(totk_motion_router_accept(&r,TOTK_SENSOR_DEVICE,epoch,s,clock_ns));
    assert(!totk_motion_router_accept(&r,TOTK_SENSOR_CONTROLLER,epoch,s,clock_ns));
    assert(!totk_motion_router_accept(&r,TOTK_SENSOR_DEVICE,epoch,s,clock_ns)); // duplicate original timestamp
    s.timestamp_ns=clock_ns-1;assert(!totk_motion_router_accept(&r,TOTK_SENSOR_DEVICE,epoch,s,clock_ns));
    s.timestamp_ns=clock_ns+1;assert(!totk_motion_router_accept(&r,TOTK_SENSOR_DEVICE,epoch,s,clock_ns));
    s=sample(clock_ns-250000000);assert(!totk_motion_sample_valid(s,clock_ns,0)); // delayed queue must not refresh it
    s=sample(clock_ns);s.rate[1]=NAN;assert(!totk_motion_sample_valid(s,clock_ns,0));
    s=sample(clock_ns);s.acc[1]=33;assert(!totk_motion_sample_valid(s,clock_ns,0));
    s=sample(clock_ns);s.rate[0]=201;assert(!totk_motion_sample_valid(s,clock_ns,0));
    r.requested=TOTK_MOTION_OFF;s=sample(++clock_ns);assert(!totk_motion_router_accept(&r,TOTK_SENSOR_DEVICE,epoch,s,clock_ns));
    r.requested=TOTK_MOTION_AUTO;totk_motion_router_select(&r,TOTK_SENSOR_CONTROLLER);
    assert(!totk_motion_router_accept(&r,TOTK_SENSOR_DEVICE,epoch,s,clock_ns)); // callback queued before switch
    assert(!totk_motion_router_accept(&r,TOTK_SENSOR_CONTROLLER,r.epoch,(TOTKMotionSample){.timestamp_ns=clock_ns},clock_ns));
    assert(totk_motion_router_accept(&r,TOTK_SENSOR_CONTROLLER,r.epoch,s,clock_ns));
    assert(!totk_motion_watchdog_expired(&r,clock_ns,clock_ns+199999999));
    assert(totk_motion_watchdog_expired(&r,clock_ns,clock_ns+200000000));
    r.active=0;totk_motion_router_invalidate(&r);assert(!totk_motion_router_accept(&r,TOTK_SENSOR_CONTROLLER,r.epoch,s,clock_ns));
    TOTKMotionRecovery recovery={0};assert(!totk_motion_recovery_ready(&recovery,1000000000));
    assert(!totk_motion_recovery_ready(&recovery,1020000000));assert(totk_motion_recovery_ready(&recovery,1060000000));
    assert(!totk_motion_recovery_ready(&recovery,1300000000)); // discontinuity restarts hysteresis
    near(totk_motion_uptime_ns(1.25),1250000000);assert(!totk_motion_uptime_ns(NAN));
    assert(!totk_motion_uptime_ns(INFINITY));assert(!totk_motion_uptime_ns((double)UINT64_MAX/1e9));
    for(int orientation=TOTK_LANDSCAPE_LEFT;orientation<=TOTK_LANDSCAPE_RIGHT;orientation++){
        s=(TOTKMotionSample){{1,2,3},{4,5,6},clock_ns};assert(totk_motion_device_basis(orientation,&s));
        double sign=orientation==TOTK_LANDSCAPE_LEFT?1:-1;
        near(s.acc[0],sign*2);near(s.acc[1],-sign);near(s.acc[2],3);
        near(s.rate[0],sign*5);near(s.rate[1],-sign*4);near(s.rate[2],6);
        TOTKMotionSample x={{1,0,0},{0},0},y={{0,1,0},{0},0},z={{0,0,1},{0},0};
        totk_motion_device_basis(orientation,&x);totk_motion_device_basis(orientation,&y);totk_motion_device_basis(orientation,&z);
        near(x.acc[0]*y.acc[1]-x.acc[1]*y.acc[0],z.acc[2]); // right-handed determinant +1
    }
    s=sample(clock_ns);assert(!totk_motion_device_basis(TOTK_LANDSCAPE_UNKNOWN,&s));
    runtime_motion_deadzone_default(0);runtime_motion_reset();RuntimeMotionState states[RUNTIME_MOTION_MAX];
    for(int i=0;i<80;i++){clock_ns+=4000000;runtime_motion_feed(s.acc,s.rate,clock_ns);}
    assert(runtime_motion_read(states,RUNTIME_MOTION_MAX)==RUNTIME_MOTION_MAX);
    uint64_t last=states[0].sampling;
    runtime_motion_reset();assert(runtime_motion_read(states,RUNTIME_MOTION_MAX)==0);
    clock_ns+=4000000;s=sample(clock_ns);runtime_motion_feed(s.acc,s.rate,clock_ns);
    assert(runtime_motion_read(states,RUNTIME_MOTION_MAX)==1 && states[0].sampling>last);
    near(states[0].acc[2],-1);near(states[0].angle[2],0);near(states[0].dir[8],1);
    for(int i=0;i<250;i++){clock_ns+=4000000;s.rate[2]=M_PI/2;runtime_motion_feed(s.acc,s.rate,clock_ns);}
    assert(runtime_motion_read(states,1)==1);near(states[0].gyro[2],0.25);near(states[0].angle[2],0.25);
    runtime_motion_start();assert(runtime_motion_read(states,RUNTIME_MOTION_MAX)==0); // guest restart/recenter flushes history
    clock_ns+=4000000;s=sample(clock_ns);runtime_motion_feed(s.acc,s.rate,clock_ns);
    assert(runtime_motion_read(states,RUNTIME_MOTION_MAX)==1);near(states[0].angle[2],0);
    clock_ns+=200000000;assert(runtime_motion_read(states,RUNTIME_MOTION_MAX)==0);
    uint64_t reads,returned;runtime_motion_stats(&reads,&returned);assert(reads>=7 && returned>0);
    pthread_t writer,reader;assert(!pthread_create(&writer,NULL,producer,NULL));assert(!pthread_create(&reader,NULL,consumer,NULL));
    assert(!pthread_join(writer,NULL));assert(!pthread_join(reader,NULL));
    puts("PASS: motion source modes, epochs, timing, recovery, landscapes and concurrent guest fusion/reset/history");
}
