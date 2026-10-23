/* Three-switch preferences; preserve legacy/advanced values and controller tuning. */
#pragma once
#ifdef __OBJC__
#import <Foundation/Foundation.h>
#include "motion_source.h"
#include "device_haptics.h"

#define TOTK_SIMPLE_GYRO_KEY @"TOTKGyroEnabled"
#define TOTK_SIMPLE_HAPTICS_KEY @"TOTKDeviceHapticsEnabled"
#define TOTK_SIMPLE_WITH_CONTROLLER_KEY @"TOTKDeviceHapticsWithController"

static inline BOOL totk_simple_gyro_enabled(NSUserDefaults *p) {
    id value=[p objectForKey:TOTK_SIMPLE_GYRO_KEY];if(value)return [value boolValue];
    NSString *old=[p stringForKey:@"TOTKGyroSource"];
    if(old.length)return ![old isEqualToString:@"off"];
    id legacy=[p objectForKey:@"TOTKGyroAiming"];return legacy?[legacy boolValue]:YES;
}
static inline BOOL totk_simple_haptics_enabled(NSUserDefaults *p) {
    id value=[p objectForKey:TOTK_SIMPLE_HAPTICS_KEY];if(value)return [value boolValue];
    return ![[p stringForKey:@"TOTKDeviceHaptics"] isEqualToString:@"off"];
}
static inline BOOL totk_simple_with_controller(NSUserDefaults *p) {
    id value=[p objectForKey:TOTK_SIMPLE_WITH_CONTROLLER_KEY];if(value)return [value boolValue];
    return [[p stringForKey:@"TOTKDeviceHaptics"] isEqualToString:@"always"];
}
static inline void totk_simple_settings_initialize(NSUserDefaults *p) {
    BOOL gyro=totk_simple_gyro_enabled(p),haptics=totk_simple_haptics_enabled(p),with=totk_simple_with_controller(p);
    if(![p objectForKey:TOTK_SIMPLE_GYRO_KEY])[p setBool:gyro forKey:TOTK_SIMPLE_GYRO_KEY];
    if(![p objectForKey:TOTK_SIMPLE_HAPTICS_KEY])[p setBool:haptics forKey:TOTK_SIMPLE_HAPTICS_KEY];
    if(![p objectForKey:TOTK_SIMPLE_WITH_CONTROLLER_KEY])[p setBool:with forKey:TOTK_SIMPLE_WITH_CONTROLLER_KEY];
}
static inline TOTKMotionMode totk_simple_motion_preference(NSUserDefaults *p) {
    return totk_simple_gyro_enabled(p)?TOTK_MOTION_AUTO:TOTK_MOTION_OFF;
}
static inline int totk_simple_device_haptic_mode(NSUserDefaults *p) {
    return !totk_simple_haptics_enabled(p)?TOTK_DEVICE_HAPTICS_OFF:
        totk_simple_with_controller(p)?TOTK_DEVICE_HAPTICS_ALWAYS:TOTK_DEVICE_HAPTICS_TOUCH;
}
#endif
