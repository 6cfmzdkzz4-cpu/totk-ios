/* Exercise real SDK vibration dispatch under independent iOS sink preferences. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "runtime.h"
#include "feedback_preferences_ios.h"
#include "device_haptics.h"

typedef struct {float low,low_hz,high,high_hz;} Vibration;
static TOTKFeedbackSample received[2];
static unsigned calls;
static const uint64_t NOW=UINT64_C(1000000000);
_Noreturn void runtime_fail(const char *message){fprintf(stderr,"%s\n",message);abort();}
void runtime_motion_start(void){}
int runtime_motion_read(RuntimeMotionState *out,int max){(void)out;(void)max;return 0;}
void runtime_rumble_set(unsigned side,float low,float low_hz,float high,float high_hz){
    assert(side<2);received[side]=(TOTKFeedbackSample){low,low_hz,high,high_hz,NOW};calls++;
}
int main(void){
    const uint32_t left=3,right=3|(1u<<16);
    const Vibration positive={.5f,100,.2f,300},stop={0,160,0,320};
    unsigned combinations=0;
    for(int handles=0;handles<=1;handles++)for(int triggers=0;triggers<=1;triggers++)
    for(int mode=0;mode<=2;mode++)for(int controller=0;controller<=1;controller++){
        /* This is the actual launch helper consumed by main.m, not a mocked
           environment setup that could conceal the old handles||triggers bug. */
        totk_ios_feedback_environment(handles,triggers,.9,.55);
        assert(!strcmp(getenv("TOTK_RUMBLE"),"1") && runtime_rumble_enabled());
        assert(!strcmp(getenv("TOTK_CONTROLLER_HAPTICS"),handles?"1":"0"));
        assert(!strcmp(getenv("TOTK_ADAPTIVE_TRIGGERS"),triggers?"1":"0"));
        assert(!strcmp(getenv("TOTK_RUMBLE_GAIN"),"0.900") && !strcmp(getenv("TOTK_TRIGGER_GAIN"),"0.550"));
        uint32_t (*send)(const uint32_t *,const Vibration *)=runtime_hid_feedback_override(
            "_ZN2nn3hid6detail18SendVibrationValueERKNS0_21VibrationDeviceHandleERKNS0_14VibrationValueE");
        uint32_t (*permitted)(bool *)=runtime_hid_feedback_override("_ZN2nn3hid6detail20IsVibrationPermittedEPb");
        assert(send && permitted);bool permission=false;assert(!permitted(&permission) && permission);
        unsigned before=calls;assert(!send(&left,&positive) && calls==before+1 && received[0].low_amplitude==.5f);
        TOTKFeedbackOutput native=totk_feedback_output(received[0],NOW,.9,.55,1),zero={0};
        int active=totk_device_haptics_eligible(1,mode,1,controller);
        TOTKFeedbackOutput local=totk_device_haptics_output(native,zero,NOW,NOW,NOW,.25f,active,0);
        assert((local.intensity>0)==active);
        assert(!send(&left,&stop));native=totk_feedback_output(received[0],NOW,.9,.55,1);
        assert(!totk_device_haptics_output(native,zero,NOW,NOW,NOW,.25f,active,0).intensity);
        combinations++;
    }
    assert(combinations==24);
    /* Start with every output disabled, then enable ONLY local touch feedback
       without rerunning startup or replacing SDK function pointers/handles. */
    totk_ios_feedback_environment(0,0,.9,.55);
    uint32_t (*send)(const uint32_t *,const Vibration *)=runtime_hid_feedback_override(
        "_ZN2nn3hid6detail18SendVibrationValueERKNS0_21VibrationDeviceHandleERKNS0_14VibrationValueE");
    assert(send);send(&right,&positive);
    TOTKFeedbackOutput native=totk_feedback_output(received[1],NOW,.9,.55,1),zero={0};
    assert(!totk_device_haptics_output(native,zero,NOW,NOW,NOW,.25f,
        totk_device_haptics_eligible(1,TOTK_DEVICE_HAPTICS_OFF,1,0),0).intensity);
    assert(totk_device_haptics_output(native,zero,NOW,NOW,NOW,.25f,
        totk_device_haptics_eligible(1,TOTK_DEVICE_HAPTICS_TOUCH,1,0),0).intensity>0);
    assert(runtime_hid_feedback_flag("rumble",0));send(&right,&positive);
    assert(!received[1].low_amplitude && !received[1].high_amplitude);
    assert(runtime_hid_feedback_flag("rumble",1));send(&right,&positive);
    assert(received[1].low_amplitude==.5f && received[1].high_amplitude==.2f);
    send(&right,&stop);assert(!received[1].low_amplitude && !received[1].high_amplitude);
    puts("SDK feedback transport: 24 independent sink combinations, live local enable and native mute/stops passed");
}
