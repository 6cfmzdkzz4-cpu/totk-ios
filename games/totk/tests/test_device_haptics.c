/* Exercise local-device eligibility, caps, lifecycle and actual native/semantic input. */
#include <assert.h>
#include <stdio.h>
#include "device_haptics.h"

int main(void) {
    const uint64_t now = UINT64_C(1000000000);
    TOTKFeedbackSample native = {.low_amplitude=.5f,.low_frequency=100,
        .high_amplitude=.2f,.high_frequency=300,.timestamp_ns=now};
    TOTKFeedbackOutput left=totk_feedback_output(native,now,.9f,.55f,1), zero={0};
    assert(left.intensity>0);
    for (int mode=0; mode<=2; mode++) {
        assert(!totk_device_haptics_eligible(0,mode,1,0)); /* iPad: honest unsupported. */
        assert(!totk_device_haptics_eligible(1,mode,0,0)); /* Background always cancels. */
    }
    assert(!totk_device_haptics_eligible(1,TOTK_DEVICE_HAPTICS_OFF,1,0));
    assert(totk_device_haptics_eligible(1,TOTK_DEVICE_HAPTICS_TOUCH,1,0));
    assert(!totk_device_haptics_eligible(1,TOTK_DEVICE_HAPTICS_TOUCH,1,1));
    assert(totk_device_haptics_eligible(1,TOTK_DEVICE_HAPTICS_ALWAYS,1,1));
    assert(!totk_device_haptics_eligible(1,42,1,0));
    TOTKFeedbackOutput out=totk_device_haptics_output(left,zero,now,now,now,.25f,1,0);
    assert(fabsf(out.intensity-left.intensity*.25f)<.00001f);
    assert(out.sharpness>=.15f && out.sharpness<=.8f);
    TOTKFeedbackOutput stereo=totk_device_haptics_output(left,left,now,now,now,.25f,1,0);
    assert(stereo.intensity==out.intensity); /* Never sum both handles into one device. */
    TOTKFeedbackOutput strong={.intensity=10,.sharpness=10};
    out=totk_device_haptics_output(strong,zero,now,now,now,10,1,0);
    assert(out.intensity==.22f && out.sharpness<=.801f);
    assert(!totk_device_haptics_output(left,zero,now,now,now,.25f,0,0).intensity);
    assert(!totk_device_haptics_output(left,zero,now,now,now,.25f,1,1).intensity);
    assert(!totk_device_haptics_output(left,zero,now,now,now,0,1,0).intensity);
    assert(!totk_device_haptics_output(left,zero,now,now,now,NAN,1,0).intensity);
    assert(!totk_device_haptics_output(left,zero,0,now,now,.25f,1,0).intensity);
    assert(!totk_device_haptics_output(left,zero,now+1,now,now,.25f,1,0).intensity);
    assert(!totk_device_haptics_output(left,zero,now-1,now,now,.25f,1,0).intensity);
    assert(!totk_device_haptics_output(left,zero,now-UINT64_C(200000000),0,now,.25f,1,0).intensity);
    assert(totk_device_haptics_output(left,zero,now-UINT64_C(199999999),0,now,.25f,1,0).intensity>0);
    TOTKFeedbackOutput malformed={.intensity=NAN,.sharpness=INFINITY};
    assert(!totk_device_haptics_output(malformed,zero,now,now,now,.25f,1,0).intensity);
    assert(totk_device_haptics_output(malformed,left,now,now,now,.25f,1,0).intensity>0);
    /* Source stop and native freshness are preserved before local scaling. */
    native.low_amplitude=native.high_amplitude=0;
    left=totk_feedback_output(native,now,.9f,.55f,1);
    assert(!totk_device_haptics_output(left,zero,now,now,now,.25f,1,0).intensity);
    native.low_amplitude=.5f; native.timestamp_ns=now-UINT64_C(250000000);
    left=totk_feedback_output(native,now,.9f,.55f,1);
    assert(!totk_device_haptics_output(left,zero,now,now,now,.25f,1,0).intensity);
    /* Existing landing composition reaches the local actuator without altering
       its controller state or promoting small terrain steps to new events. */
    TOTKSemanticContext context={.timestamp_ns=now,.valid=1};
    TOTKSemanticEvents events={.landing_timestamp_ns=now};
    TOTKSemanticFeedbackState state={0};
    TOTKSemanticFeedbackOutput semantic=totk_semantic_feedback_output(native,context,events,now,.9f,.55f,1,&state);
    left=(TOTKFeedbackOutput){.intensity=semantic.handle_intensity,.sharpness=semantic.handle_sharpness};
    assert(left.intensity>0);
    out=totk_device_haptics_output(left,zero,now,now,now,.25f,1,0);
    assert(out.intensity>0 && out.intensity<left.intensity);
    assert(TOTK_DEVICE_HAPTICS_LEASE_NS==UINT64_C(100000000));
    assert(TOTK_DEVICE_HAPTICS_RENEW_NS<TOTK_DEVICE_HAPTICS_LEASE_NS);
    puts("local-device haptic policy passed");
}
