/* Native three-switch settings; advanced stored preferences remain untouched. */
#import "TOTKSettings.h"
#import <CoreHaptics/CoreHaptics.h>
#include "simple_settings_ios.h"
#include "totk_frame_cap.h"
void runtime_ios_apply_frame_cap(void);

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
- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView {(void)tableView;return 2;}
- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    (void)tableView;return section==0?3:4;
}
- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section {
    (void)tableView;return section==0?@"Controls":@"Frame cap";
}
- (NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section {
    (void)tableView;
    if(section==1)return @"30 and 60 are the only caps this build paces. 30 is the default and holds presents at 30 Hz, with the real-time frame step following that spacing. 60 uses the existing present-interval patch and is a ceiling, not a sustained rate. 90 and 120 are unavailable: the game's interval is a whole 60 Hz tick floored at 1, and frame-counted interface logic diverges above that. No 90 or 120 gameplay patch was added. The top-left counter is a separate display.";
    return self.supportsDeviceHaptics?@"Changes apply when you close settings.":@"Device haptics are unavailable on this device. Gyro changes apply when you close settings.";
}
- (void)selectFrameCap:(int)hz {
    if(totk_frame_cap_unavailable(hz) || !totk_frame_cap_normalize(hz))return;
    [NSUserDefaults.standardUserDefaults setInteger:hz forKey:TOTK_FRAME_CAP_KEY];
    totk_frame_cap_set(hz);runtime_ios_apply_frame_cap();
    [self.tableView reloadData];
}
- (UITableViewCell *)frameCapCellForRow:(NSInteger)row {
    static const int rates[]={30,60,90,120};
    int requested=totk_simple_frame_cap_requested(NSUserDefaults.standardUserDefaults);
    int rate=rates[row];
    BOOL unavailable=totk_frame_cap_unavailable(rate);
    UITableViewCell *cell=[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1 reuseIdentifier:nil];
    cell.textLabel.text=[NSString stringWithFormat:@"%d FPS",rate];
    cell.detailTextLabel.text=unavailable?@"Unavailable":nil;
    cell.accessoryType=!unavailable && requested==rate?UITableViewCellAccessoryCheckmark:UITableViewCellAccessoryNone;
    cell.selectionStyle=unavailable?UITableViewCellSelectionStyleNone:UITableViewCellSelectionStyleDefault;
    cell.userInteractionEnabled=!unavailable;
    if(unavailable)cell.textLabel.textColor=UIColor.secondaryLabelColor;
    cell.accessibilityLabel=unavailable?[NSString stringWithFormat:@"%d FPS, Unavailable",rate]:[NSString stringWithFormat:@"%d FPS",rate];
    cell.accessibilityHint=unavailable?@"Unavailable":nil;
    return cell;
}
- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)path {
    if(path.section==1)return [self frameCapCellForRow:path.row];
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
- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)path {
    [tableView deselectRowAtIndexPath:path animated:YES];
    if(path.section!=1)return;
    static const int rates[]={30,60,90,120};
    if(totk_frame_cap_unavailable(rates[path.row]))return;
    [self selectFrameCap:rates[path.row]];
}
@end

UIViewController *TOTKSettingsPresentation(void (^completion)(void)) {
    TOTKSettingsRoot *root=[[TOTKSettingsRoot alloc] initWithStyle:UITableViewStyleInsetGrouped];
    TOTKSettingsNavigation *navigation=[[TOTKSettingsNavigation alloc] initWithRootViewController:root];
    navigation.finished=completion;navigation.modalPresentationStyle=UIModalPresentationFormSheet;
    return navigation;
}
