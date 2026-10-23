/* Consume the dedicated development viewer's atomic scene/input publications.
   Geometry is requested here and latched only by the guest present boundary. */
#import <Foundation/Foundation.h>
#include "totk_duo_scene.h"
#include "totk_live_display.h"
#include <math.h>
#include <pthread.h>

static pthread_mutex_t scene_lock=PTHREAD_MUTEX_INITIALIZER;
static dispatch_source_t scene_timer;
static NSString *scene_file,*viewer_session;
static uint64_t last_sequence,last_fresh;
static RuntimeGamepadState virtual_pad;
static uint64_t pending_buttons;
static double last_geometry[7];
static int last_orientation,have_geometry;
static int foreground,configured,last_focus;

static int numeric(id value,double *out) {
    if(![value isKindOfClass:NSNumber.class])return 0;
    *out=[value doubleValue];return isfinite(*out);
}
static int integer(id value,int64_t minimum,int64_t maximum,int64_t *out) {
    double number;if(!numeric(value,&number) || number<minimum || number>maximum || trunc(number)!=number)return 0;
    *out=(int64_t)number;return 1;
}
static void scene_expire(void) {
    pthread_mutex_lock(&scene_lock);uint64_t now=runtime_now_ns();
    int expired=last_fresh && (now<last_fresh || now-last_fresh>250000000);
    int notify=expired && last_focus;
    if(expired){virtual_pad=(RuntimeGamepadState){0};pending_buttons=0;last_focus=0;}
    pthread_mutex_unlock(&scene_lock);if(notify)runtime_oe_push_message(15);
}
static void scene_poll(void) {
    @autoreleasepool {
        NSData *data=[NSData dataWithContentsOfFile:scene_file];
        if(!data || data.length>4096)return;
        id object=[NSJSONSerialization JSONObjectWithData:data options:0 error:NULL];
        if(![object isKindOfClass:NSDictionary.class])return;
        NSDictionary *value=object;int64_t schema,sequence,buttons,axis[4],orientation,active;
        NSString *session=value[@"session"];
        if(![session isKindOfClass:NSString.class] || !session.length || session.length>128 ||
           !integer(value[@"schema"],1,1,&schema) || !integer(value[@"sequence"],1,INT64_C(9007199254740991),&sequence) ||
           !integer(value[@"foreground_active"],0,1,&active) || !integer(value[@"buttons"],0,65535,&buttons) ||
           !integer(value[@"interface_orientation"],1,4,&orientation))return;
        NSArray *keys=@[@"lx",@"ly",@"rx",@"ry"];
        for(unsigned i=0;i<4;i++)if(!integer(value[keys[i]],-32767,32767,&axis[i]))return;
        double width,height,scale,insets[4];id inset_value=value[@"safe_insets"];
        if(!numeric(value[@"width"],&width) || !numeric(value[@"height"],&height) || !numeric(value[@"scale"],&scale) ||
           ![inset_value isKindOfClass:NSArray.class] || [inset_value count]!=4)return;
        for(unsigned i=0;i<4;i++)if(!numeric(inset_value[i],&insets[i]))return;
        pthread_mutex_lock(&scene_lock);
        int changed_session=![viewer_session isEqualToString:session];
        int fresh=changed_session || (uint64_t)sequence>last_sequence;
        pthread_mutex_unlock(&scene_lock);
        if(!fresh)return;
        /* A transient empty scene retains the previous requested geometry but
           contributes neutral virtual input. Physical controllers are separate. */
        int geometry_ok=runtime_live_display_request_scene(width,height,scale,insets,(unsigned)orientation);
        double geometry[7]={width,height,scale,insets[0],insets[1],insets[2],insets[3]};
        int geometry_changed=geometry_ok && have_geometry && (memcmp(last_geometry,geometry,sizeof geometry) || last_orientation!=(int)orientation);
        if(geometry_ok){memcpy(last_geometry,geometry,sizeof geometry);last_orientation=(int)orientation;have_geometry=1;}
        RuntimeGamepadState next={0};
        if(active && geometry_ok)next=(RuntimeGamepadState){(uint64_t)buttons,(int32_t)axis[0],(int32_t)axis[1],(int32_t)axis[2],(int32_t)axis[3],1};
        pthread_mutex_lock(&scene_lock);
        viewer_session=session;last_sequence=(uint64_t)sequence;last_fresh=runtime_now_ns();
        if(changed_session || geometry_changed || !active || !geometry_ok)pending_buttons=0;
        if(geometry_changed)next=(RuntimeGamepadState){0};
        if(active && geometry_ok)pending_buttons|=next.buttons&~virtual_pad.buttons;
        foreground=(int)active;virtual_pad=next;
        int notify=last_focus!=(int)active;last_focus=(int)active;
        pthread_mutex_unlock(&scene_lock);
        if(notify)runtime_oe_push_message(15);
    }
}
static void scene_start(void) {
    static dispatch_once_t once;
    dispatch_once(&once,^{
        const char *path=getenv("DM_DUO_RELAY");if(!path || !*path)return;
        NSString *directory=[NSString stringWithUTF8String:path];BOOL is_directory=NO;
        if(!directory.isAbsolutePath || ![NSFileManager.defaultManager fileExistsAtPath:directory isDirectory:&is_directory] || !is_directory)return;
        scene_file=[directory stringByAppendingPathComponent:@"scene-input.json"];configured=1;
        dispatch_queue_t queue=dispatch_queue_create("org.nxnative.totk.duo-scene",DISPATCH_QUEUE_SERIAL);
        scene_timer=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,queue);
        dispatch_source_set_timer(scene_timer,DISPATCH_TIME_NOW,16*NSEC_PER_MSEC,2*NSEC_PER_MSEC);
        dispatch_source_set_event_handler(scene_timer,^{scene_expire();scene_poll();});dispatch_resume(scene_timer);
    });
}
int totk_duo_scene_configured(void) {scene_start();return configured;}
int totk_duo_scene_active(void) {
    scene_start();pthread_mutex_lock(&scene_lock);uint64_t now=runtime_now_ns();
    int active=foreground && last_fresh && now>=last_fresh && now-last_fresh<=250000000;
    pthread_mutex_unlock(&scene_lock);return active;
}
void totk_duo_scene_read(RuntimeGamepadState *state) {
    if(!state)return;scene_start();pthread_mutex_lock(&scene_lock);uint64_t now=runtime_now_ns();
    int fresh=foreground && last_fresh && now>=last_fresh && now-last_fresh<=250000000;
    *state=fresh?virtual_pad:(RuntimeGamepadState){0};if(fresh)state->buttons|=pending_buttons;pending_buttons=0;
    pthread_mutex_unlock(&scene_lock);
}
