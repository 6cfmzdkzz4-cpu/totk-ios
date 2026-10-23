/* iOS launch preferences: mute individual sinks without removing native source handles. */
#pragma once
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static inline void totk_ios_feedback_environment(int handles,int triggers,double strength,double trigger_strength) {
    setenv("TOTK_CONTROLLER_HAPTICS",handles?"1":"0",1);
    setenv("TOTK_ADAPTIVE_TRIGGERS",triggers?"1":"0",1);
    /* The SDK discovers handles at startup. Keep transport available so local
       Off -> On works later, even when both controller sinks started muted.
       Guest-authored stops and the runtime's explicit permit/mute path remain
       in hid_feedback.c; each output sink still applies its own enable flag. */
    setenv("TOTK_RUMBLE","1",1);
    char gain[32];snprintf(gain,sizeof gain,"%.3f",isfinite(strength)?fmax(0,fmin(1,strength)):0.9);
    setenv("TOTK_RUMBLE_GAIN",gain,1);
    snprintf(gain,sizeof gain,"%.3f",isfinite(trigger_strength)?fmax(0,fmin(1,trigger_strength)):0.55);
    setenv("TOTK_TRIGGER_GAIN",gain,1);
}
