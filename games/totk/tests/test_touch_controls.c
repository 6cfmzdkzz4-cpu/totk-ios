/* Test the exact touch ownership/axes/gear policy used by the UIKit receiver. */
#include <assert.h>
#include <stdio.h>
#include "../ios/Sources/touch_policy.h"

static const uint64_t NOW=UINT64_C(1000000000);
static dm_tc_point gear(dm_tc_layout l){return (dm_tc_point){l.gear.x+22,l.gear.y+22};}
static int overlap(dm_tc_rect a,dm_tc_rect b){return a.x<b.x+b.width && a.x+a.width>b.x && a.y<b.y+b.height && a.y+a.height>b.y;}
static void neutral(TOTKTouchPolicy *s,dm_tc_layout l){TOTKTouchOutput o=tc_output(s,l);assert(!o.buttons && !o.lx && !o.ly && !o.rx && !o.ry);}
static uint64_t begin_gear(TOTKTouchPolicy *s,dm_tc_layout l){tc_reset(s);return tc_begin(s,l,l.safe.width+l.safe.x*2,1,gear(l),NOW,0);}
static void exercise(double width,double height,dm_tc_insets insets){
    dm_tc_layout l=dm_tc_make_layout(width,height,insets);TOTKTouchPolicy s={0};tc_configure(&s,1,0,1);
    assert(l.gear.width==44 && l.gear.height==44);
    for(int plus=0;plus<2;plus++){
        dm_tc_rect p=dm_tc_pill_hit(l,plus);
        assert(!overlap(p,l.gear));
        dm_tc_point center=tc_button_center(l,plus?TC_PLUS:TC_MINUS);
        tc_begin(&s,l,width,1,center,NOW,0);assert(tc_output(&s,l).buttons==(UINT64_C(1)<<(plus?TC_PLUS:TC_MINUS)));tc_end(&s,1);neutral(&s,l);
    }
    for(int b=TC_A;b<=TC_Y;b++){
        dm_tc_point p=dm_tc_face_position(l,b);tc_begin(&s,l,width,1,p,NOW,0);
        assert(tc_contact(&s,1)->kind==TC_FACE);assert(tc_output(&s,l).buttons==(UINT64_C(1)<<b));
        tc_end(&s,1);neutral(&s,l);
    }
    tc_begin(&s,l,width,1,l.left_stick,NOW,0);neutral(&s,l);
    assert(tc_contact(&s,1)->kind==TC_LEFT_STICK && tc_contact(&s,1)->origin.x==l.left_stick.x);
    dm_tc_point full={l.left_stick.x+DM_TC_STICK_R*l.unit,l.left_stick.y};tc_move(&s,l,1,full);
    assert(tc_output(&s,l).lx==32767 && tc_output(&s,l).ly==0);
    tc_move(&s,l,1,(dm_tc_point){l.left_stick.x,l.left_stick.y-DM_TC_STICK_R*l.unit});assert(tc_output(&s,l).ly==32767);
    tc_end(&s,1);neutral(&s,l);
    tc_begin(&s,l,width,1,l.dpad,NOW,0);assert(tc_contact(&s,1)->kind==TC_DPAD);neutral(&s,l);
    tc_move(&s,l,1,(dm_tc_point){l.dpad.x+20*l.unit,l.dpad.y-20*l.unit});
    assert(tc_output(&s,l).buttons==((UINT64_C(1)<<TC_UP)|(UINT64_C(1)<<TC_RIGHT)));tc_end(&s,1);
    dm_tc_point camera={width*.7,height*.45};tc_begin(&s,l,width,1,camera,NOW,0);
    assert(tc_contact(&s,1)->kind==TC_RIGHT_STICK);neutral(&s,l);
    tc_move(&s,l,1,(dm_tc_point){camera.x+200,camera.y});
    assert(tc_contact(&s,1)->origin.x>camera.x && tc_output(&s,l).rx==32767);tc_end(&s,1);neutral(&s,l);
    tc_begin(&s,l,width,1,tc_button_center(l,TC_ZL),NOW,0);
    tc_move(&s,l,1,l.face);assert(tc_output(&s,l).buttons==(UINT64_C(1)<<TC_ZL));tc_end(&s,1);
    /* Only an owned stationary release opens; time passing does nothing. */
    uint64_t token=begin_gear(&s,l);assert(token);neutral(&s,l);
    assert(!s.gear.opened && s.gear.tracking);
    assert(tc_tap_settings(&s,l,1,gear(l)));
    assert(!tc_tap_settings(&s,l,1,gear(l)));tc_end(&s,1);neutral(&s,l);
    token=begin_gear(&s,l);tc_move(&s,l,1,camera);assert(tc_contact(&s,1)->kind==TC_GEAR);
    assert(!tc_tap_settings(&s,l,1,gear(l)));neutral(&s,l);tc_end(&s,1);
    token=begin_gear(&s,l);tc_reset(&s);assert(!tc_tap_settings(&s,l,1,gear(l)));neutral(&s,l);
    /* Inclusive slop, release endpoint, sticky excursion and begin generation. */
    token=begin_gear(&s,l);tc_move(&s,l,1,(dm_tc_point){gear(l).x+12,gear(l).y});
    assert(tc_tap_settings(&s,l,1,(dm_tc_point){gear(l).x+12,gear(l).y}));neutral(&s,l);
    token=begin_gear(&s,l);tc_move(&s,l,1,(dm_tc_point){gear(l).x+12.001,gear(l).y});
    assert(!tc_tap_settings(&s,l,1,gear(l)));
    token=begin_gear(&s,l);assert(!tc_tap_settings(&s,l,1,(dm_tc_point){gear(l).x+12.001,gear(l).y}));
    token=begin_gear(&s,l);tc_move(&s,l,1,(dm_tc_point){NAN,0});assert(!tc_tap_settings(&s,l,1,gear(l)));
    token=begin_gear(&s,l);s.gear.generation++;assert(!tc_tap_settings(&s,l,1,gear(l)));
    token=begin_gear(&s,l);assert(!tc_tap_settings(&s,l,2,gear(l)));tc_end(&s,1);assert(!tc_tap_settings(&s,l,1,gear(l)));
    /* Resting/continuing gameplay fingers are allowed; presentation resets
       every owner. Only another gear-owned finger blocks the tap. */
    tc_reset(&s);tc_begin(&s,l,width,2,l.left_stick,NOW,0);token=tc_begin(&s,l,width,1,gear(l),NOW,0);
    assert(tc_tap_settings(&s,l,1,gear(l)));
    tc_reset(&s);token=tc_begin(&s,l,width,1,gear(l),NOW,0);tc_begin(&s,l,width,2,l.face,NOW,0);
    assert(tc_tap_settings(&s,l,1,gear(l)));
    token=begin_gear(&s,l);tc_begin(&s,l,width,2,l.face,NOW+1,0);tc_end(&s,2);
    assert(tc_tap_settings(&s,l,1,gear(l)));
    token=begin_gear(&s,l);tc_begin(&s,l,width,2,gear(l),NOW+1,0);tc_end(&s,2);
    assert(!tc_tap_settings(&s,l,1,gear(l)));
    tc_reset(&s);token=tc_begin(&s,l,width,1,gear(l),NOW,1);tc_begin(&s,l,width,2,gear(l),NOW,1);
    assert(!tc_tap_settings(&s,l,1,gear(l)));
    /* Focus, presentation, rotation and configuration clear owners. */
    token=begin_gear(&s,l);tc_configure(&s,1,0,0);assert(!tc_tap_settings(&s,l,1,gear(l)));neutral(&s,l);
    tc_configure(&s,1,0,1);tc_move(&s,l,1,l.face);neutral(&s,l);assert(!tc_contacts(&s));
    tc_begin(&s,l,width,2,l.left_stick,NOW,0);tc_move(&s,l,2,full);assert(tc_output(&s,l).lx);
    tc_reset(&s);tc_move(&s,l,2,full);neutral(&s,l); /* Fresh down is required after reset/rotation. */
    token=begin_gear(&s,l);tc_configure(&s,1,1,1);assert(!tc_tap_settings(&s,l,1,gear(l)));
    tc_begin(&s,l,width,2,l.face,NOW,0);neutral(&s,l);tc_reset(&s);
    token=tc_begin(&s,l,width,1,gear(l),NOW,0);assert(tc_tap_settings(&s,l,1,gear(l))); /* Gear remains with pad. */
    tc_reset(&s);tc_configure(&s,0,0,1);tc_begin(&s,l,width,1,camera,NOW,0);neutral(&s,l);
    tc_end(&s,1);tc_begin(&s,l,width,1,l.dpad,NOW,0);assert(tc_contact(&s,1)->kind!=TC_DPAD);tc_reset(&s);
    tc_begin(&s,l,width,1,dm_tc_face_position(l,TC_A),NOW,0);assert(tc_output(&s,l).buttons==1);tc_reset(&s);
}
int main(void){
    assert(DM_TC_VERSION==6);
    assert(TC_A==0 && TC_B==1 && TC_PLUS==10 && TC_MINUS==11 && TC_LEFT==12 && TC_DOWN==15);
    exercise(956,440,(dm_tc_insets){0,62,21,62});
    exercise(844,390,(dm_tc_insets){0,59,21,0});
    exercise(844,390,(dm_tc_insets){0,0,21,59});
    exercise(568,320,(dm_tc_insets){0,0,0,0});
    exercise(1376,1032,(dm_tc_insets){24,0,20,0});
    exercise(1194,834,(dm_tc_insets){24,0,20,0});
    exercise(678,466,(dm_tc_insets){0,0,21,0});
    exercise(466,678,(dm_tc_insets){24,0,21,0});
    exercise(951,669,(dm_tc_insets){0,0,21,0});
    exercise(669,951,(dm_tc_insets){24,0,21,0});
    exercise(320,700,(dm_tc_insets){24,0,21,0});
    puts("TOTK touch: 11 phone/iPad/Duo/intermediate geometries, ownership/axes and guarded settings passed");
}
