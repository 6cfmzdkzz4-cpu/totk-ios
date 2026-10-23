/* Exercise the real private scene reader with synthetic atomic publications. */
#import <Foundation/Foundation.h>
#include "totk_duo_scene.h"
#include "totk_live_display.h"
#include <assert.h>
#include <stdatomic.h>
#include <time.h>
#include <unistd.h>

static _Atomic unsigned requests,accepted,notifications;
static pthread_mutex_t state_lock=PTHREAD_MUTEX_INITIALIZER;
static double last_width;
uint64_t runtime_now_ns(void) {struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return (uint64_t)t.tv_sec*1000000000+(uint64_t)t.tv_nsec;}
void runtime_oe_push_message(uint32_t code) {assert(code==15);atomic_fetch_add(&notifications,1);}
int runtime_live_display_request_scene(double w,double h,double scale,const double insets[4],unsigned orientation) {
    int ok=w>0 && h>0 && scale>0 && orientation>=1 && orientation<=4 &&
        w/h>=.5 && w/h<=16.0/9.0 && insets[0]>=0 && insets[1]>=0 && insets[2]>=0 && insets[3]>=0 &&
        insets[0]+insets[2]<h && insets[1]+insets[3]<w;
    if(ok){pthread_mutex_lock(&state_lock);last_width=w;pthread_mutex_unlock(&state_lock);atomic_fetch_add(&accepted,1);}
    atomic_fetch_add(&requests,1);return ok;
}
static void publish(NSString *directory,unsigned sequence,unsigned buttons,int foreground,double width,double height,unsigned orientation) {
    NSDictionary *value=@{@"schema":@1,@"session":@"fixture-session",@"sequence":@(sequence),@"foreground_active":@(foreground),
        @"width":@(width),@"height":@(height),@"scale":@3,@"safe_insets":@[@0,@0,@21,@0],
        @"interface_orientation":@(orientation),@"buttons":@(buttons),@"lx":@(buttons?23456:0),@"ly":@0,@"rx":@0,@"ry":@0};
    NSData *data=[NSJSONSerialization dataWithJSONObject:value options:0 error:NULL];assert(data);
    assert([data writeToFile:[directory stringByAppendingPathComponent:@"scene-input.json"] options:NSDataWritingAtomic error:NULL]);
}
static void wait_for(unsigned target) {
    uint64_t until=runtime_now_ns()+1000000000;
    while(atomic_load(&requests)<target && runtime_now_ns()<until)usleep(1000);
    assert(atomic_load(&requests)==target);usleep(2000);
}
int main(int argc,char **argv) {@autoreleasepool {
    assert(argc==2);NSString *directory=@(argv[1]);assert(!setenv("DM_DUO_RELAY",argv[1],1));
    assert(totk_duo_scene_configured());
    publish(directory,1,0,1,951,669,3);wait_for(1);
    RuntimeGamepadState state;totk_duo_scene_read(&state);assert(state.connected && !state.buttons && totk_duo_scene_active());
    publish(directory,2,1,1,951,669,3);wait_for(2);totk_duo_scene_read(&state);assert(state.buttons==1 && state.lx==23456);
    publish(directory,3,0,1,951,669,3);wait_for(3);
    publish(directory,4,2,1,951,669,3);wait_for(4);
    publish(directory,5,0,1,951,669,3);wait_for(5);
    totk_duo_scene_read(&state);assert(state.buttons==2);totk_duo_scene_read(&state);assert(!state.buttons);
    publish(directory,5,65535,1,678,466,4);usleep(50000);assert(atomic_load(&requests)==5);
    usleep(280000);totk_duo_scene_read(&state);assert(!state.buttons && !state.lx && !state.connected && !totk_duo_scene_active());
    publish(directory,6,1,1,951,669,3);wait_for(6);totk_duo_scene_read(&state);assert(state.buttons==1 && totk_duo_scene_active());
    publish(directory,7,512,1,678,466,4);wait_for(7);totk_duo_scene_read(&state);assert(!state.buttons && !state.lx);
    publish(directory,8,512,1,678,466,4);wait_for(8);totk_duo_scene_read(&state);assert(state.buttons==512);
    publish(directory,9,65535,0,678,466,4);wait_for(9);totk_duo_scene_read(&state);assert(!state.buttons && !totk_duo_scene_active());
    publish(directory,10,1,1,0,466,4);wait_for(10);totk_duo_scene_read(&state);assert(!state.buttons && !state.lx);
    pthread_mutex_lock(&state_lock);assert(last_width==678);pthread_mutex_unlock(&state_lock);
    assert(atomic_load(&accepted)==9);
    NSData *invalid=[@"{\"schema\":1,\"sequence\":11,\"lx\":\"bad\"}" dataUsingEncoding:NSUTF8StringEncoding];
    assert([invalid writeToFile:[directory stringByAppendingPathComponent:@"scene-input.json"] options:NSDataWritingAtomic error:NULL]);
    usleep(280000);assert(atomic_load(&requests)==10);assert(!totk_duo_scene_active());
    assert(atomic_load(&notifications)>=4);
    puts("{\"status\":\"pass\",\"fresh_publications\":10,\"accepted_geometries\":9,\"duplicate_stale_geometry_cancel_foreground_and_pending_edges\":true}");
    return 0;
}}
