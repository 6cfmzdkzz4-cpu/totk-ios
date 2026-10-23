/* Foundation-only preference migration/binding test; no game or device APIs. */
#import <Foundation/Foundation.h>
#include <assert.h>
#include "simple_settings_ios.h"

int main(void){@autoreleasepool{
    NSString *suite=[@"org.macnativegames.test.simple-settings." stringByAppendingString:NSUUID.UUID.UUIDString];
    NSUserDefaults *p=[[NSUserDefaults alloc] initWithSuiteName:suite];unsigned cases=0;
    for(NSString *gyro in @[@"off",@"controller",@"device",@"auto"])
    for(NSString *mode in @[@"off",@"touch",@"always"]){
        [p removePersistentDomainForName:suite];
        NSDictionary *legacy=@{@"TOTKTouchLayout":@NO,@"TOTKGyroSource":gyro,@"TOTKGyroAiming":@YES,
            @"TOTKDeviceHaptics":mode,@"TOTKDeviceHapticIntensity":@.13,@"TOTKControllerHaptics":@NO,
            @"TOTKAdaptiveTriggers":@YES,@"TOTKFeedbackStrength":@.81,@"TOTKTriggerStrength":@.42,@"TOTKResolution":@"540"};
        for(NSString *key in legacy)[p setObject:legacy[key] forKey:key];
        totk_simple_settings_initialize(p);
        assert(totk_simple_gyro_enabled(p)==![gyro isEqual:@"off"]);
        assert(totk_simple_haptics_enabled(p)==![mode isEqual:@"off"]);
        assert(totk_simple_with_controller(p)==[mode isEqual:@"always"]);
        for(int enabled=0;enabled<=1;enabled++)for(int with=0;with<=1;with++){
            [p setBool:enabled forKey:TOTK_SIMPLE_HAPTICS_KEY];[p setBool:with forKey:TOTK_SIMPLE_WITH_CONTROLLER_KEY];
            assert(totk_simple_device_haptic_mode(p)==(!enabled?TOTK_DEVICE_HAPTICS_OFF:with?TOTK_DEVICE_HAPTICS_ALWAYS:TOTK_DEVICE_HAPTICS_TOUCH));cases++;
        }
        [p setBool:NO forKey:TOTK_SIMPLE_GYRO_KEY];assert(totk_simple_motion_preference(p)==TOTK_MOTION_OFF);
        [p setBool:YES forKey:TOTK_SIMPLE_GYRO_KEY];assert(totk_simple_motion_preference(p)==TOTK_MOTION_AUTO);
        for(NSString *key in legacy)assert([[p objectForKey:key] isEqual:legacy[key]]);
        totk_simple_settings_initialize(p);assert(totk_simple_gyro_enabled(p)); /* Never overwrite explicit switches. */
    }
    [p removePersistentDomainForName:suite];[p setBool:NO forKey:@"TOTKGyroAiming"];totk_simple_settings_initialize(p);
    assert(totk_simple_motion_preference(p)==TOTK_MOTION_OFF);
    [p removePersistentDomainForName:suite];
    printf("Simple settings: %u sink combinations; legacy/raw preferences retained; gyro Off/Auto and remembered controller opt-in passed\n",cases);
}}
