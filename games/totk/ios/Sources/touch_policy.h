/* Odyssey touch ownership adapted to TOTK's unchanged Npad bit/axis ABI. */
#pragma once
#include "dm_touch_controls.h"
#include <string.h>

enum { TC_A,TC_B,TC_X,TC_Y,TC_L3,TC_R3,TC_L,TC_R,TC_ZL,TC_ZR,TC_PLUS,TC_MINUS,TC_LEFT,TC_UP,TC_RIGHT,TC_DOWN,TC_BUTTONS };
enum { TC_NONE,TC_LEFT_STICK,TC_RIGHT_STICK,TC_FACE,TC_DPAD,TC_BUTTON,TC_GEAR };
enum { TC_MAX_CONTACTS=16 };
typedef struct { uint64_t id; int kind,index; dm_tc_point point,origin; } TOTKTouchContact;
typedef struct {
    TOTKTouchContact contacts[TC_MAX_CONTACTS];
    dm_tc_gear gear;
    uint64_t gear_id,gear_token;
    int full,pad_off,enabled;
} TOTKTouchPolicy;
typedef struct { uint64_t buttons; int32_t lx,ly,rx,ry; } TOTKTouchOutput;

static inline double tc_distance(dm_tc_point a,dm_tc_point b) { return hypot(a.x-b.x,a.y-b.y); }
static inline int tc_drop(int b) { return b==TC_ZL || b==TC_L || b==TC_ZR || b==TC_R; }
static inline int tc_click(int b) { return b==TC_L3 || b==TC_R3; }
static inline int tc_full_only(int b) { return tc_click(b) || b>=TC_LEFT; }
static inline dm_tc_point tc_button_center(dm_tc_layout l,int b) {
    if(b<=TC_Y)return dm_tc_face_position(l,b);
    if(tc_drop(b))return dm_tc_shoulder(l,b==TC_ZR || b==TC_R,b==TC_ZL || b==TC_ZR);
    if(tc_click(b))return dm_tc_click(l,b==TC_R3);
    dm_tc_rect r=dm_tc_pill(l,b==TC_PLUS);return (dm_tc_point){r.x+r.width/2,r.y+r.height/2};
}
static inline unsigned tc_contacts(const TOTKTouchPolicy *s) {
    unsigned n=0;for(unsigned i=0;i<TC_MAX_CONTACTS;i++)n+=s->contacts[i].id!=0;return n;
}
static inline unsigned tc_gear_contacts(const TOTKTouchPolicy *s) {
    unsigned n=0;for(unsigned i=0;i<TC_MAX_CONTACTS;i++)n+=s->contacts[i].id && s->contacts[i].kind==TC_GEAR;return n;
}
static inline TOTKTouchContact *tc_contact(TOTKTouchPolicy *s,uint64_t id) {
    if(!id)return NULL;
    for(unsigned i=0;i<TC_MAX_CONTACTS;i++)if(s->contacts[i].id==id)return &s->contacts[i];
    return NULL;
}
static inline void tc_reset(TOTKTouchPolicy *s) {
    memset(s->contacts,0,sizeof s->contacts);s->gear_id=s->gear_token=0;dm_tc_gear_cancel(&s->gear);
}
static inline void tc_configure(TOTKTouchPolicy *s,int full,int pad_off,int enabled) {
    if(s->full!=full || s->pad_off!=pad_off || s->enabled!=enabled)tc_reset(s);
    s->full=full;s->pad_off=pad_off;s->enabled=enabled;
}
static inline int tc_has_stick(const TOTKTouchPolicy *s,int kind) {
    for(unsigned i=0;i<TC_MAX_CONTACTS;i++)if(s->contacts[i].id && s->contacts[i].kind==kind)return 1;
    return 0;
}
static inline uint64_t tc_face_bits(dm_tc_layout l,dm_tc_point p) {
    uint64_t bits=0;
    for(int b=TC_A;b<=TC_Y;b++)if(tc_distance(p,dm_tc_face_position(l,b))<=DM_TC_FACE_R*l.unit*1.25)bits|=UINT64_C(1)<<b;
    return bits;
}
static inline uint64_t tc_dpad_bits(dm_tc_layout l,dm_tc_point p) {
    double dx=p.x-l.dpad.x,dy=p.y-l.dpad.y;
    if(hypot(dx,dy)<DM_TC_DPAD_R*l.unit*.25)return 0;
    double a=atan2(-dy,dx)*180/3.14159265358979323846;uint64_t bits=0;
    if(a>-67.5 && a<67.5)bits|=UINT64_C(1)<<TC_RIGHT;
    if(a>22.5 && a<157.5)bits|=UINT64_C(1)<<TC_UP;
    if(a>112.5 || a<-112.5)bits|=UINT64_C(1)<<TC_LEFT;
    if(a<-22.5 && a>-157.5)bits|=UINT64_C(1)<<TC_DOWN;
    return bits;
}
static inline int tc_locked_button(dm_tc_layout l,int full,dm_tc_point p) {
    const int buttons[]={TC_ZL,TC_L,TC_ZR,TC_R,TC_MINUS,TC_PLUS,TC_L3,TC_R3};
    for(unsigned i=0;i<sizeof buttons/sizeof *buttons;i++){
        int b=buttons[i];if(tc_full_only(b) && !full)continue;
        if(tc_drop(b) || tc_click(b)){
            double radius=(tc_click(b)?DM_TC_CLICK_R:b==TC_ZL || b==TC_ZR?DM_TC_ZL_R:DM_TC_L_R)*l.unit;
            if(tc_distance(p,tc_button_center(l,b))<=radius*1.35)return b;
        }else{
            if(dm_tc_contains(dm_tc_pill_hit(l,b==TC_PLUS),p))return b;
        }
    }
    return -1;
}
static inline uint64_t tc_begin(TOTKTouchPolicy *s,dm_tc_layout l,double width,
    uint64_t id,dm_tc_point p,uint64_t now,unsigned simultaneous_gears) {
    if(!s->enabled || !id || l.unit<=0 || !isfinite(l.unit) || tc_contact(s,id) || !isfinite(p.x) || !isfinite(p.y))return 0;
    unsigned others=tc_gear_contacts(s)+simultaneous_gears;
    if(s->gear_id && dm_tc_contains(l.gear,p))dm_tc_gear_block(&s->gear);
    TOTKTouchContact *c=NULL;
    for(unsigned i=0;i<TC_MAX_CONTACTS;i++)if(!s->contacts[i].id){c=&s->contacts[i];break;}
    if(!c)return 0;
    *c=(TOTKTouchContact){.id=id,.point=p,.origin=p};
    /* Exclusive ownership precedes every gameplay test, even with a controller. */
    if(dm_tc_contains(l.gear,p)){
        c->kind=TC_GEAR;
        if(s->gear_id)return 0;
        s->gear_id=id;s->gear_token=dm_tc_gear_begin(&s->gear,p,now,others);return s->gear_token;
    }
    if(s->pad_off)return 0;
    int button=tc_locked_button(l,s->full,p);
    if(button>=0){c->kind=TC_BUTTON;c->index=button;return 0;}
    double best=1,f=tc_distance(p,l.face)/((DM_TC_FACE_GAP+DM_TC_FACE_R*1.6)*l.unit);
    if(f<=best){best=f;c->kind=TC_FACE;}
    if(s->full){double d=tc_distance(p,l.dpad)/(DM_TC_DPAD_R*l.unit*DM_TC_DPAD_GRAB);if(d<best){best=d;c->kind=TC_DPAD;}}
    if(!tc_has_stick(s,TC_LEFT_STICK)){
        double d=tc_distance(p,l.left_stick)/(DM_TC_STICK_R*l.unit*DM_TC_STICK_GRAB);
        if(d<best){c->kind=TC_LEFT_STICK;c->origin=l.left_stick;}
    }
    if(c->kind==TC_NONE && s->full && !tc_has_stick(s,TC_RIGHT_STICK) && p.x>=width*(1-DM_TC_RSWIPE))c->kind=TC_RIGHT_STICK;
    return 0;
}
static inline void tc_move(TOTKTouchPolicy *s,dm_tc_layout l,uint64_t id,dm_tc_point p) {
    TOTKTouchContact *c=tc_contact(s,id);if(!c)return; /* Reset fingers never reacquire on a move. */
    if(!isfinite(p.x) || !isfinite(p.y)){if(id==s->gear_id)dm_tc_gear_block(&s->gear);return;}
    c->point=p;
    if(c->kind==TC_GEAR && id==s->gear_id){
        dm_tc_gear_move(&s->gear,p);if(!dm_tc_contains(l.gear,p))dm_tc_gear_block(&s->gear);
    }else if(c->kind==TC_RIGHT_STICK){
        double d=tc_distance(p,c->origin),radius=DM_TC_RSTICK_R*l.unit;
        if(d>radius)c->origin=(dm_tc_point){p.x-(p.x-c->origin.x)*radius/d,p.y-(p.y-c->origin.y)*radius/d};
    }
}
static inline void tc_end(TOTKTouchPolicy *s,uint64_t id) {
    TOTKTouchContact *c=tc_contact(s,id);if(!c)return;
    if(id==s->gear_id){dm_tc_gear_cancel(&s->gear);s->gear_id=s->gear_token=0;}
    *c=(TOTKTouchContact){0};
}
static inline int tc_tap_settings(TOTKTouchPolicy *s,dm_tc_layout l,uint64_t id,dm_tc_point released) {
    if(!s->enabled || id!=s->gear_id || !tc_contact(s,id))return 0;
    if(tc_gear_contacts(s)>1)dm_tc_gear_block(&s->gear);
    return dm_tc_gear_tap(&s->gear,s->gear_token,released,l.gear);
}
static inline dm_tc_point tc_knob(const TOTKTouchContact *c,dm_tc_layout l) {
    double dx=c->point.x-c->origin.x,dy=c->point.y-c->origin.y,d=hypot(dx,dy);
    double radius=(c->kind==TC_LEFT_STICK?DM_TC_STICK_R:DM_TC_RSTICK_R)*l.unit;
    if(d>radius){dx*=radius/d;dy*=radius/d;}
    return (dm_tc_point){dx,dy};
}
static inline TOTKTouchOutput tc_output(const TOTKTouchPolicy *s,dm_tc_layout l) {
    TOTKTouchOutput out={0};if(!s->enabled || s->pad_off || l.unit<=0 || !isfinite(l.unit))return out;
    for(unsigned i=0;i<TC_MAX_CONTACTS;i++){
        const TOTKTouchContact *c=&s->contacts[i];if(!c->id)continue;
        if(c->kind==TC_BUTTON)out.buttons|=UINT64_C(1)<<c->index;
        else if(c->kind==TC_FACE)out.buttons|=tc_face_bits(l,c->point);
        else if(c->kind==TC_DPAD)out.buttons|=tc_dpad_bits(l,c->point);
        else if(c->kind==TC_LEFT_STICK || c->kind==TC_RIGHT_STICK){
            dm_tc_point p=tc_knob(c,l);double d=hypot(p.x,p.y);
            double radius=(c->kind==TC_LEFT_STICK?DM_TC_STICK_R:DM_TC_RSTICK_R)*l.unit,dz=radius*DM_TC_DEAD;
            if(d<=dz)continue;
            double m=fmin((d-dz)/(radius*DM_TC_FULL-dz),1)/d;
            int32_t x=(int32_t)lrint(p.x*m*32767),y=(int32_t)lrint(-p.y*m*32767);
            if(c->kind==TC_LEFT_STICK){out.lx=x;out.ly=y;}else{out.rx=x;out.ry=y;}
        }
    }
    return out;
}
