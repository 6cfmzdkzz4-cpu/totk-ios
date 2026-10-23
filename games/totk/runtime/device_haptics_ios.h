/* Local Core Haptics sink. Every call belongs to the shared serial feedback queue. */
#ifndef TOTK_DEVICE_HAPTICS_IOS_H
#define TOTK_DEVICE_HAPTICS_IOS_H
#import <Foundation/Foundation.h>
#include "device_haptics.h"
void runtime_device_haptics_initialize(dispatch_queue_t owner);
void runtime_device_haptics_preferences(void);
int runtime_device_haptics_wanted(int foreground, int controller);
void runtime_device_haptics_quiet(uint64_t now_ns);
void runtime_device_haptics_submit(TOTKFeedbackOutput left, TOTKFeedbackOutput right,
    uint64_t heartbeat_ns, uint64_t now_ns, int foreground, int controller, int paused);
NSDictionary *runtime_device_haptics_diagnostics(void);
#endif
