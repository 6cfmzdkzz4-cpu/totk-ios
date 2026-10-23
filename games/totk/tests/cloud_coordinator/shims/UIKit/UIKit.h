// UI-only test declarations. The coordinator uses real Foundation persistence.
#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
typedef NSUInteger UIBackgroundTaskIdentifier;
static const UIBackgroundTaskIdentifier UIBackgroundTaskInvalid = NSUIntegerMax;
typedef NS_ENUM(NSInteger, UIAlertControllerStyle) { UIAlertControllerStyleAlert };
typedef NS_ENUM(NSInteger, UIAlertActionStyle) { UIAlertActionStyleDefault, UIAlertActionStyleCancel };
typedef NS_ENUM(NSInteger, NSTextAlignment) { NSTextAlignmentCenter = 1 };
typedef NS_OPTIONS(NSUInteger, UIViewAutoresizing) { UIViewAutoresizingFlexibleWidth = 2, UIViewAutoresizingFlexibleHeight = 16 };
@class UIAlertAction;
@interface UIView : NSObject
@property CGRect frame;
@property CGRect bounds;
@property UIViewAutoresizing autoresizingMask;
- (void)addSubview:(UIView *)view;
- (void)removeFromSuperview;
@end
@interface UIColor : NSObject
+ (instancetype)whiteColor;
@end
@interface UILabel : UIView
@property(copy) NSString *text;
@property(strong) UIColor *textColor;
@property NSTextAlignment textAlignment;
@end
@interface UIViewController : NSObject
@property(strong) UIView *view;
- (void)presentViewController:(UIViewController *)controller animated:(BOOL)animated completion:(void (^)(void))completion;
@end
@interface UIDevice : NSObject
+ (instancetype)currentDevice;
@property(readonly) NSString *model;
@end
@interface UIApplication : NSObject
+ (instancetype)sharedApplication;
- (UIBackgroundTaskIdentifier)beginBackgroundTaskWithName:(NSString *)name expirationHandler:(void (^)(void))handler;
- (void)endBackgroundTask:(UIBackgroundTaskIdentifier)identifier;
@end
@interface UIAlertController : UIViewController
+ (instancetype)alertControllerWithTitle:(NSString *)title message:(NSString *)message preferredStyle:(UIAlertControllerStyle)style;
- (void)addAction:(UIAlertAction *)action;
@end
@interface UIAlertAction : NSObject
+ (instancetype)actionWithTitle:(NSString *)title style:(UIAlertActionStyle)style handler:(void (^)(UIAlertAction *))handler;
@end
