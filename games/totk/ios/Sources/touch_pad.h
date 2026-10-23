#import <UIKit/UIKit.h>

/* On-screen controller for iPhone/iPad, laid out like a Switch controller. It reports its state to the runtime through
   runtime_touch_pad_set (runtime/gamepad.m), which merges it with any physical controller. */
@interface TOTKTouchPad : UIView
@property(nonatomic,copy) BOOL (^settingsRequested)(void);
@property(nonatomic,getter=isSuspended) BOOL suspended;
- (void)reloadPreferences;
- (void)invalidateDisplayGeometry;
@end
