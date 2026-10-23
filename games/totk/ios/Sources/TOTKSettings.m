/* Native three-switch settings; advanced stored preferences remain untouched. */
#import "TOTKSettings.h"
#import <CoreHaptics/CoreHaptics.h>
#include "simple_settings_ios.h"

@interface TOTKSettingsNavigation : UINavigationController <UIAdaptivePresentationControllerDelegate>
@property(nonatomic,copy) void (^finished)(void);
- (void)finish;
@end
@implementation TOTKSettingsNavigation
- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated]; self.presentationController.delegate=self;
}
- (void)finish {
    void (^callback)(void)=self.finished; self.finished=nil; if(callback)callback();
}
- (void)presentationControllerDidDismiss:(UIPresentationController *)presentationController {
    (void)presentationController; [self finish];
}
- (void)viewDidDisappear:(BOOL)animated {
    [super viewDidDisappear:animated];
    if(self.isBeingDismissed || !self.presentingViewController)[self finish];
}
@end

@interface TOTKSettingsRoot : UITableViewController
@property(nonatomic) BOOL supportsDeviceHaptics;
@end
@implementation TOTKSettingsRoot
- (void)viewDidLoad {
    [super viewDidLoad];self.title=@"Settings";
    self.supportsDeviceHaptics=CHHapticEngine.capabilitiesForHardware.supportsHaptics;
    totk_simple_settings_initialize(NSUserDefaults.standardUserDefaults);
    self.navigationItem.rightBarButtonItem=[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemDone target:self action:@selector(done)];
}
- (void)done {
    TOTKSettingsNavigation *navigation=(TOTKSettingsNavigation *)self.navigationController;
    [navigation dismissViewControllerAnimated:YES completion:^{[navigation finish];}];
}
- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView {(void)tableView;return 1;}
- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {(void)tableView;(void)section;return 3;}
- (NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section {
    (void)tableView;(void)section;
    return self.supportsDeviceHaptics?@"Changes apply when you close settings.":@"Device haptics are unavailable on this device. Gyro changes apply when you close settings.";
}
- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)path {
    (void)tableView;
    NSArray *titles=@[@"Gyro",@"Device haptics",@"Also with controller"];
    NSArray *keys=@[TOTK_SIMPLE_GYRO_KEY,TOTK_SIMPLE_HAPTICS_KEY,TOTK_SIMPLE_WITH_CONTROLLER_KEY];
    NSUserDefaults *defaults=NSUserDefaults.standardUserDefaults;NSString *key=keys[path.row];
    UITableViewCell *cell=[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:nil];
    cell.textLabel.text=titles[path.row];cell.selectionStyle=UITableViewCellSelectionStyleNone;
    UISwitch *control=[UISwitch new];BOOL available=path.row==0 || self.supportsDeviceHaptics;
    control.on=available && [defaults boolForKey:key];control.enabled=available;control.accessibilityLabel=titles[path.row];
    if(!available){cell.textLabel.textColor=UIColor.secondaryLabelColor;control.accessibilityHint=@"Unavailable on this device";}
    __weak UISwitch *weakControl=control;
    [control addAction:[UIAction actionWithHandler:^(__kindof UIAction *action){
        (void)action;if(weakControl.enabled)[defaults setBool:weakControl.on forKey:key];
    }] forControlEvents:UIControlEventValueChanged];
    cell.accessoryView=control;return cell;
}
@end

UIViewController *TOTKSettingsPresentation(void (^completion)(void)) {
    TOTKSettingsRoot *root=[[TOTKSettingsRoot alloc] initWithStyle:UITableViewStyleInsetGrouped];
    TOTKSettingsNavigation *navigation=[[TOTKSettingsNavigation alloc] initWithRootViewController:root];
    navigation.finished=completion;navigation.modalPresentationStyle=UIModalPresentationFormSheet;
    return navigation;
}
