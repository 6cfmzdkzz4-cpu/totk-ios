/* Odyssey-style touch layers backed by the canonical portable control contract.
   Only runtime_touch_pad_set is used; physical GameController routing is untouched. */
#import "touch_pad.h"
#import <GameController/GameController.h>
#import <QuartzCore/QuartzCore.h>
#import <CoreText/CoreText.h>
#include "runtime.h"
#include "touch_policy.h"
#include <stdlib.h>

@interface TOTKTouchPad (SettingsAccessibility)
- (BOOL)accessibilityOpenSettings:(UIAccessibilityCustomAction *)action;
@end
@interface TOTKTouchSettingsElement : UIAccessibilityElement
@property(nonatomic,weak) TOTKTouchPad *owner;
@end
@implementation TOTKTouchSettingsElement
- (BOOL)accessibilityActivate { return [self.owner accessibilityOpenSettings:nil]; }
@end

static UIColor *idleColor,*pressedColor,*lineColor,*ringColor,*textColor,*pressedTextColor;
static CAShapeLayer *shape(CALayer *parent) {
    CAShapeLayer *layer=[CAShapeLayer layer];layer.fillColor=idleColor.CGColor;
    layer.strokeColor=lineColor.CGColor;layer.lineWidth=1.5;
    layer.shadowColor=UIColor.blackColor.CGColor;layer.shadowOpacity=DM_TC_SHADOW_ALPHA;
    layer.shadowRadius=3;layer.shadowOffset=CGSizeMake(0,1);[parent addSublayer:layer];return layer;
}
static CATextLayer *label(CALayer *parent,NSString *text) {
    CATextLayer *layer=[CATextLayer layer];layer.string=text;layer.alignmentMode=kCAAlignmentCenter;
    layer.foregroundColor=textColor.CGColor;layer.contentsScale=UIScreen.mainScreen.scale;
    [parent addSublayer:layer];return layer;
}
static CGPoint point(dm_tc_point p) { return CGPointMake(p.x,p.y); }
static CGRect rect(dm_tc_rect r) { return CGRectMake(r.x,r.y,r.width,r.height); }
static uint64_t touchID(UITouch *touch) { return (uint64_t)(uintptr_t)(__bridge void *)touch; }
static dm_tc_point location(UITouch *touch,UIView *view) { CGPoint p=[touch locationInView:view];return (dm_tc_point){p.x,p.y}; }
static CGPathRef circle(CGPoint p,CGFloat r) { return CGPathCreateWithEllipseInRect(CGRectMake(p.x-r,p.y-r,2*r,2*r),NULL); }
static void path(CAShapeLayer *layer,CGPathRef p) { layer.path=p;layer.shadowPath=p;CGPathRelease(p); }
static void placeLabel(CATextLayer *layer,CGPoint p,CGFloat size) {
    CTFontRef font=CTFontCreateUIFontForLanguage(kCTFontUIFontEmphasizedSystem,size,NULL);
    layer.font=font;layer.fontSize=size;CGFloat height=CTFontGetAscent(font)+CTFontGetDescent(font)+CTFontGetLeading(font);
    layer.frame=CGRectMake(p.x-size*1.5,p.y-height/2,size*3,height);CFRelease(font);
}
static CGPathRef drop(CGPoint c,CGFloat r,CGFloat angle,CGFloat rounding) {
    CGFloat d=r*DM_TC_DROP_TIP,th=acos(r/d);CGPoint tip=CGPointMake(c.x+d*cos(angle),c.y+d*sin(angle));
    CGPoint p1=CGPointMake(c.x+r*cos(angle+th),c.y+r*sin(angle+th)),p2=CGPointMake(c.x+r*cos(angle-th),c.y+r*sin(angle-th));
    CGMutablePathRef p=CGPathCreateMutable();CGPathMoveToPoint(p,NULL,p2.x,p2.y);
    CGPathAddArcToPoint(p,NULL,tip.x,tip.y,p1.x,p1.y,rounding);
    CGPathAddArc(p,NULL,c.x,c.y,r,angle+th,angle-th+2*M_PI,false);CGPathCloseSubpath(p);return p;
}
static CGPathRef arm(CGPoint c,CGFloat radius,CGFloat width,CGFloat inset,CGFloat angle,CGFloat rounding) {
    CGPoint v[5]={{-width,-radius},{width,-radius},{width,-(inset+width)},{0,-inset},{-width,-(inset+width)}};
    CGFloat cs=cos(angle),sn=sin(angle);
    for(int i=0;i<5;i++)v[i]=CGPointMake(c.x+v[i].x*cs-v[i].y*sn,c.y+v[i].x*sn+v[i].y*cs);
    CGMutablePathRef p=CGPathCreateMutable();CGPathMoveToPoint(p,NULL,(v[4].x+v[0].x)/2,(v[4].y+v[0].y)/2);
    for(int i=0;i<5;i++)CGPathAddArcToPoint(p,NULL,v[i].x,v[i].y,v[(i+1)%5].x,v[(i+1)%5].y,rounding);
    CGPathCloseSubpath(p);return p;
}

@implementation TOTKTouchPad {
    TOTKTouchPolicy _policy;dm_tc_layout _layout;
    NSMutableSet<UITouch *> *_touches;
    CALayer *_controls;CAShapeLayer *_keys[TC_BUTTONS],*_ring,*_knob,*_gear;
    CATextLayer *_labels[TC_BUTTONS];UIImageView *_gearIcon;
    TOTKTouchSettingsElement *_gearElement;
    BOOL _active,_suspended,_laidOut;CGSize _lastSize;UIEdgeInsets _lastInsets;
    dispatch_queue_t _diagnosticsQueue;NSObject *_diagnosticLock;
    NSDictionary *_pendingDiagnostic;BOOL _diagnosticWorker;
    uint64_t _gearEntries,_gearChecks,_gearAccepted,_gearRejected;
}
- (instancetype)initWithFrame:(CGRect)frame {
    if((self=[super initWithFrame:frame])){
        self.multipleTouchEnabled=YES;self.opaque=NO;self.backgroundColor=UIColor.clearColor;
        _touches=[NSMutableSet set];_active=UIApplication.sharedApplication.applicationState==UIApplicationStateActive;
        _diagnosticLock=[NSObject new];
        _diagnosticsQueue=dispatch_queue_create("org.macnativegames.totk.touch-diagnostics",dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL,QOS_CLASS_UTILITY,0));
        static dispatch_once_t once;dispatch_once(&once,^{
            idleColor=[UIColor colorWithWhite:1 alpha:DM_TC_IDLE_ALPHA];pressedColor=[UIColor colorWithWhite:1 alpha:DM_TC_PRESSED_ALPHA];
            lineColor=[UIColor colorWithWhite:1 alpha:DM_TC_STROKE_ALPHA];ringColor=[UIColor colorWithWhite:1 alpha:DM_TC_RING_ALPHA];
            textColor=[UIColor colorWithWhite:1 alpha:DM_TC_TEXT_ALPHA];pressedTextColor=[UIColor colorWithWhite:1 alpha:DM_TC_PRESSED_TEXT_ALPHA];
        });
        _controls=[CALayer layer];[self.layer addSublayer:_controls];_ring=shape(_controls);_ring.fillColor=ringColor.CGColor;
        _knob=shape(_controls);CAShapeLayer *inner=[CAShapeLayer layer];inner.fillColor=nil;inner.strokeColor=lineColor.CGColor;inner.lineWidth=1.5;[_knob addSublayer:inner];
        NSString *names[TC_BUTTONS]={@"A",@"B",@"X",@"Y",@"L3",@"R3",@"L",@"R",@"ZL",@"ZR",@"+",@"−"};
        for(int b=0;b<TC_BUTTONS;b++){_keys[b]=shape(_controls);if(names[b])_labels[b]=label(_keys[b],names[b]);}
        _gear=shape(self.layer);
        UIImageSymbolConfiguration *symbol=[UIImageSymbolConfiguration configurationWithPointSize:DM_TC_GEAR_ICON_SIZE weight:UIImageSymbolWeightRegular];
        _gearIcon=[[UIImageView alloc] initWithImage:[UIImage systemImageNamed:[NSString stringWithUTF8String:DM_TC_GEAR_SYMBOL] withConfiguration:symbol]];
        _gearIcon.tintColor=[UIColor colorWithWhite:1 alpha:DM_TC_GEAR_ALPHA];_gearIcon.contentMode=UIViewContentModeScaleAspectFit;_gearIcon.userInteractionEnabled=NO;[self addSubview:_gearIcon];
        _gearElement=[[TOTKTouchSettingsElement alloc] initWithAccessibilityContainer:self];_gearElement.owner=self;
        _gearElement.accessibilityLabel=@"Settings";_gearElement.accessibilityHint=@"Tap to open game settings.";_gearElement.accessibilityTraits=UIAccessibilityTraitButton;
        _gearElement.accessibilityCustomActions=@[[[UIAccessibilityCustomAction alloc] initWithName:@"Open settings" target:self selector:@selector(accessibilityOpenSettings:)]];
        self.isAccessibilityElement=NO;self.accessibilityElements=@[_gearElement];
        NSNotificationCenter *nc=NSNotificationCenter.defaultCenter;
        [nc addObserver:self selector:@selector(padsChanged:) name:GCControllerDidConnectNotification object:nil];
        [nc addObserver:self selector:@selector(padsChanged:) name:GCControllerDidDisconnectNotification object:nil];
        [nc addObserver:self selector:@selector(inactive:) name:UIApplicationWillResignActiveNotification object:nil];
        [nc addObserver:self selector:@selector(active:) name:UIApplicationDidBecomeActiveNotification object:nil];
        [self reloadPreferences];[self padsChanged:nil];
    }
    return self;
}
- (void)dealloc { [NSNotificationCenter.defaultCenter removeObserver:self]; }
- (void)invalidateDisplayGeometry { [self resetTouches];[self setNeedsLayout]; }
- (void)resetTouches { tc_reset(&_policy);[_touches removeAllObjects];[self publish]; }
- (void)diagnose:(NSString *)phase {
    uint64_t now=runtime_now_ns();unsigned gears=tc_gear_contacts(&_policy);
    NSDictionary *value=@{@"phase":phase,@"fullControls":@(_policy.full),@"inputEnabled":@(_policy.enabled),
        @"foreground":@(_active),@"suspended":@(_suspended),@"controllerConnected":@(_policy.pad_off),
        @"applicationState":@(UIApplication.sharedApplication.applicationState),@"windowAttached":@(self.window!=nil),
        @"presentedController":NSStringFromClass(self.window.rootViewController.presentedViewController.class)?:@"none",
        @"settingsCallback":@(self.settingsRequested!=nil),@"gearEntries":@(_gearEntries),@"tapChecks":@(_gearChecks),
        @"acceptedRequests":@(_gearAccepted),@"rejectedRequests":@(_gearRejected),@"gearContacts":@(gears),
        @"gameplayContacts":@(tc_contacts(&_policy)-gears),@"tracking":@(_policy.gear.tracking),
        @"blocked":@(_policy.gear.blocked),@"generation":@(_policy.gear.generation),@"beginToken":@(_policy.gear_token),
        @"gestureAgeMs":@(_policy.gear.began_ns && now>=_policy.gear.began_ns?(now-_policy.gear.began_ns)/1e6:0),
        @"build":NSBundle.mainBundle.infoDictionary[@"CFBundleVersion"]?:@"unknown"};
    @synchronized(_diagnosticLock){_pendingDiagnostic=value;if(_diagnosticWorker)return;_diagnosticWorker=YES;}
    dispatch_async(_diagnosticsQueue,^{for(;;){@autoreleasepool{
        NSDictionary *next;@synchronized(self->_diagnosticLock){next=self->_pendingDiagnostic;self->_pendingDiagnostic=nil;
            if(!next){self->_diagnosticWorker=NO;return;}}
        const char *override=getenv("TOTK_TOUCH_DIAGNOSTICS_DIR");
        NSString *directory=override?[NSString stringWithUTF8String:override]:NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,NSUserDomainMask,YES).firstObject;
        [[NSFileManager defaultManager] createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:nil error:nil];
        [[NSJSONSerialization dataWithJSONObject:next options:NSJSONWritingPrettyPrinted error:nil]
            writeToFile:[directory stringByAppendingPathComponent:@"touch-settings.json"] options:NSDataWritingAtomic error:nil];
    }}});
}
- (BOOL)isSuspended { return _suspended; }
- (void)setSuspended:(BOOL)value {
    _suspended=value;tc_configure(&_policy,_policy.full,_policy.pad_off,_active && !_suspended);[self resetTouches];
    [self diagnose:value?@"suspend":@"resume"];
}
- (void)reloadPreferences {
    /* Full controls are unconditional; retain the obsolete stored choice. */
    tc_configure(&_policy,1,_policy.pad_off,_active && !_suspended);
    [self resetTouches];[self setNeedsLayout];
    [self diagnose:@"preferences"];
}
- (void)padsChanged:(NSNotification *)note {
    BOOL connected=NO;
    for(GCController *pad in GCController.controllers)if(pad.extendedGamepad &&
        !([note.name isEqualToString:GCControllerDidDisconnectNotification] && pad==note.object))connected=YES;
    tc_configure(&_policy,_policy.full,connected,_active && !_suspended);[self resetTouches];[self setNeedsLayout];
    [self diagnose:@"controllerChanged"];
}
- (void)inactive:(NSNotification *)note {
    (void)note;_active=NO;tc_configure(&_policy,_policy.full,_policy.pad_off,0);[self resetTouches];
    [self diagnose:@"inactive"];
}
- (void)active:(NSNotification *)note { (void)note;_active=YES;[self reloadPreferences]; }
- (BOOL)accessibilityOpenSettings:(UIAccessibilityCustomAction *)action {
    (void)action;if(!_active || _suspended)return NO;
    BOOL accepted=self.settingsRequested?self.settingsRequested():NO;
    if(accepted)_gearAccepted++;else _gearRejected++;
    [self diagnose:accepted?@"accessibilityAccepted":@"accessibilityRejected"];return accepted;
}
- (void)recordGearHistory:(UITouch *)touch event:(UIEvent *)event {
    uint64_t identifier=touchID(touch);if(identifier!=_policy.gear_id)return;
    /* Release events must also inspect coalesced excursions before accepting. */
    for(UITouch *sample in [event coalescedTouchesForTouch:touch])
        tc_move(&_policy,_layout,identifier,location(sample,self));
    tc_move(&_policy,_layout,identifier,location(touch,self));
}
- (void)publish {
    TOTKTouchOutput output=tc_output(&_policy,_layout);
    RuntimeGamepadState state={.connected=1,.buttons=output.buttons,.lx=output.lx,.ly=output.ly,.rx=output.rx,.ry=output.ry};
    runtime_touch_pad_set(&state);[self refresh];
}
- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    (void)event;if(!_active || _suspended)return;
    unsigned gearBegins=0;
    for(UITouch *touch in touches)gearBegins+=dm_tc_contains(_layout.gear,location(touch,self));
    for(UITouch *touch in touches){
        dm_tc_point p=location(touch,self);unsigned otherGears=dm_tc_contains(_layout.gear,p) && gearBegins?gearBegins-1:0;
        [_touches addObject:touch];tc_begin(&_policy,_layout,self.bounds.size.width,touchID(touch),p,runtime_now_ns(),otherGears);
        if(dm_tc_contains(_layout.gear,p)){_gearEntries++;[self diagnose:@"gearBegin"];}
    }
    [self publish];
}
- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    for(UITouch *touch in touches){
        uint64_t identifier=touchID(touch);
        /* UIKit may deliver only the newest position in a coalesced move. */
        if(identifier==_policy.gear_id)[self recordGearHistory:touch event:event];
        else tc_move(&_policy,_layout,identifier,location(touch,self));
    }
    [self publish];
}
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    BOOL request=NO;
    for(UITouch *touch in touches){
        uint64_t identifier=touchID(touch);
        if(identifier==_policy.gear_id){
            [self recordGearHistory:touch event:event];_gearChecks++;
            BOOL accepted=_active && !_suspended && tc_tap_settings(&_policy,_layout,identifier,location(touch,self));
            if(accepted)request=YES;else{_gearRejected++;[self diagnose:@"tapGuardRejected"];}
        }
        tc_end(&_policy,identifier);[_touches removeObject:touch];
    }
    [self publish];
    if(request){BOOL accepted=self.settingsRequested?self.settingsRequested():NO;
        if(accepted)_gearAccepted++;else _gearRejected++;
        [self diagnose:accepted?@"requestAccepted":@"presenterRejected"];
    }
}
- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    (void)event;
    for(UITouch *touch in touches){
        uint64_t identifier=touchID(touch);
        if(identifier==_policy.gear_id){_gearRejected++;dm_tc_gear_block(&_policy.gear);[self diagnose:@"tapCancelled"];}
        tc_end(&_policy,identifier);[_touches removeObject:touch];
    }
    [self publish];
}
- (void)safeAreaInsetsDidChange { [super safeAreaInsetsDidChange];[self resetTouches];[self setNeedsLayout]; }
- (void)layoutSubviews {
    [super layoutSubviews];
    if(_laidOut && (!CGSizeEqualToSize(_lastSize,self.bounds.size) || !UIEdgeInsetsEqualToEdgeInsets(_lastInsets,self.safeAreaInsets)))[self resetTouches];
    _laidOut=YES;_lastSize=self.bounds.size;_lastInsets=self.safeAreaInsets;UIEdgeInsets i=self.safeAreaInsets;
    _layout=dm_tc_make_layout(self.bounds.size.width,self.bounds.size.height,(dm_tc_insets){i.top,i.left,i.bottom,i.right});CGFloat u=_layout.unit;
    if(u<=0)return;
    [CATransaction begin];[CATransaction setDisableActions:YES];_controls.frame=self.layer.bounds;
    for(int b=0;b<TC_BUTTONS;b++)_keys[b].hidden=tc_full_only(b) && !_policy.full;
    for(int b=TC_A;b<=TC_Y;b++){CGPoint c=point(dm_tc_face_position(_layout,b));path(_keys[b],circle(c,DM_TC_FACE_R*u));placeLabel(_labels[b],c,20*u);}
    const int buttons[]={TC_ZL,TC_L,TC_ZR,TC_R,TC_MINUS,TC_PLUS,TC_L3,TC_R3};
    for(unsigned j=0;j<sizeof buttons/sizeof *buttons;j++){
        int b=buttons[j];CGPoint c=point(tc_button_center(_layout,b));
        if(tc_drop(b)){CGFloat radius=(b==TC_ZL || b==TC_ZR?DM_TC_ZL_R:DM_TC_L_R)*u;
            path(_keys[b],drop(c,radius,b==TC_ZR || b==TC_R?-M_PI_4:-3*M_PI_4,4*u));placeLabel(_labels[b],c,16*u);}
        else if(tc_click(b)){path(_keys[b],circle(c,DM_TC_CLICK_R*u));placeLabel(_labels[b],c,13*u);}
        else{CGRect r=rect(dm_tc_pill(_layout,b==TC_PLUS));path(_keys[b],CGPathCreateWithRoundedRect(r,r.size.height/2,r.size.height/2,NULL));placeLabel(_labels[b],c,18*u);}
    }
    const struct {int button;CGFloat angle;} arms[]={{TC_UP,0},{TC_RIGHT,M_PI_2},{TC_DOWN,M_PI},{TC_LEFT,-M_PI_2}};
    for(unsigned j=0;j<4;j++)path(_keys[arms[j].button],arm(point(_layout.dpad),DM_TC_DPAD_R*u,DM_TC_ARM_W*u,DM_TC_ARM_IN*u,arms[j].angle,6*u));
    path(_ring,circle(point(_layout.left_stick),DM_TC_STICK_R*u));CGFloat k=DM_TC_KNOB_R*u;
    _knob.bounds=CGRectMake(0,0,2*k,2*k);path(_knob,circle(CGPointMake(k,k),k));
    CAShapeLayer *inner=(CAShapeLayer *)_knob.sublayers.firstObject;inner.frame=_knob.bounds;path(inner,circle(CGPointMake(k,k),k*.62));
    CGRect gear=rect(_layout.gear);path(_gear,CGPathCreateWithRoundedRect(gear,gear.size.height/2,gear.size.height/2,NULL));
    _gearIcon.frame=CGRectMake(CGRectGetMidX(gear)-DM_TC_GEAR_ICON_SIZE/2,CGRectGetMidY(gear)-DM_TC_GEAR_ICON_SIZE/2,
        DM_TC_GEAR_ICON_SIZE,DM_TC_GEAR_ICON_SIZE);_gearElement.accessibilityFrameInContainerSpace=gear;
    [CATransaction commit];[self refresh];
}
- (void)refresh {
    TOTKTouchOutput output=tc_output(&_policy,_layout);
    [CATransaction begin];[CATransaction setDisableActions:YES];_controls.hidden=_policy.pad_off || _suspended || !_active;
    for(int b=0;b<TC_BUTTONS;b++){BOOL down=(output.buttons>>b)&1;_keys[b].fillColor=(down?pressedColor:idleColor).CGColor;_labels[b].foregroundColor=(down?pressedTextColor:textColor).CGColor;}
    dm_tc_point offset={0};BOOL held=NO;
    for(unsigned j=0;j<TC_MAX_CONTACTS;j++)if(_policy.contacts[j].id && _policy.contacts[j].kind==TC_LEFT_STICK){held=YES;offset=tc_knob(&_policy.contacts[j],_layout);}
    _knob.position=CGPointMake(_layout.left_stick.x+offset.x,_layout.left_stick.y+offset.y);_knob.fillColor=(held?pressedColor:idleColor).CGColor;
    _gear.fillColor=(_policy.gear.tracking && !_policy.gear.blocked?pressedColor:idleColor).CGColor;
    [CATransaction commit];
}
@end
