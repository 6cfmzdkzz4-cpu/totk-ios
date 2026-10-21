// Optional CloudKit save handoff. Without TOTKCloudContainer, saves stay local.
// Imports happen only before the game runtime starts.
#import <UIKit/UIKit.h>
@interface TOTKCloudSave : NSObject
+ (instancetype)shared;
- (void)prepareForController:(UIViewController *)controller completion:(void (^)(void))completion;
- (void)checkForChanges;
- (void)flushForBackground;
@end
void runtime_ios_save_committed(const char *profile);
