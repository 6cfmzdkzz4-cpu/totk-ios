// Exercise the real coordinator/archive and real Foundation I/O with synthetic
// saves. Only UIKit and CloudKit service callbacks are substituted; no network,
// game data, user Documents, provisioning or physical device is accessed.
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "../../ios/Sources/TOTKSaveArchive.m"
#include "../../ios/Sources/TOTKCloudSave.m"

static unsigned checks,failures,presentations;
static NSMutableArray *observations;
static void check(BOOL ok,NSString *name){
    checks++;if(!ok)failures++;
    NSData *data=[NSJSONSerialization dataWithJSONObject:@{@"check":name,@"pass":@(ok)} options:0 error:nil];
    printf("%s\n",[[[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding] UTF8String]);fflush(stdout);
}
static NSString *child(NSString *root,NSString *name){return [root stringByAppendingPathComponent:name];}
static void mkdirs(NSString *path){NSError *e=nil;if(![NSFileManager.defaultManager createDirectoryAtPath:path withIntermediateDirectories:YES attributes:nil error:&e])abort();}
static void removeFixture(NSString *path){if(![NSFileManager.defaultManager removeItemAtPath:path error:nil])abort();}

@implementation UIView
- (void)addSubview:(UIView *)view{(void)view;}
- (void)removeFromSuperview{}
@end
@implementation UIColor
+ (instancetype)whiteColor{return [self new];}
@end
@implementation UILabel @end
@implementation UIViewController
- (instancetype)init{if((self=[super init]))self.view=[UIView new];return self;}
- (void)presentViewController:(UIViewController *)controller animated:(BOOL)animated completion:(void (^)(void))completion {
    (void)controller;(void)animated;presentations++;if(completion)completion();
}
@end
@implementation UIDevice
+ (instancetype)currentDevice{return [self new];}
- (NSString *)model{return @"synthetic Foundation audit";}
@end
@implementation UIApplication
+ (instancetype)sharedApplication{return [self new];}
- (UIBackgroundTaskIdentifier)beginBackgroundTaskWithName:(NSString *)name expirationHandler:(void (^)(void))handler{(void)name;(void)handler;return 1;}
- (void)endBackgroundTask:(UIBackgroundTaskIdentifier)identifier{(void)identifier;}
@end
@implementation UIAlertController
+ (instancetype)alertControllerWithTitle:(NSString *)title message:(NSString *)message preferredStyle:(UIAlertControllerStyle)style{(void)title;(void)message;(void)style;return [self new];}
- (void)addAction:(UIAlertAction *)action{(void)action;}
@end
@implementation UIAlertAction
+ (instancetype)actionWithTitle:(NSString *)title style:(UIAlertActionStyle)style handler:(void (^)(UIAlertAction *))handler{(void)title;(void)style;(void)handler;return [self new];}
@end

@interface AuditAccount : NSObject
@property(copy) NSString *user;
@property NSUInteger reads;
- (void)fetchUserRecordIDWithCompletionHandler:(void (^)(CKRecordID *,NSError *))completion;
@end
@implementation AuditAccount
- (void)fetchUserRecordIDWithCompletionHandler:(void (^)(CKRecordID *,NSError *))completion{self.reads++;completion([[CKRecordID alloc] initWithRecordName:self.user],nil);}
@end
@interface AuditDatabase : NSObject
@property NSUInteger uploads,fetches,successfulUploads;
@property BOOL allCAS;
@property(copy) NSString *serverDirectory;
@property(strong) CKRecord *head;
@property(strong) NSError *uploadError;
@property(copy) void (^beforeUpload)(void);
- (void)addOperation:(CKDatabaseOperation *)operation;
@end
@implementation AuditDatabase
- (void)addOperation:(CKDatabaseOperation *)operation {
    if([operation isKindOfClass:CKFetchRecordsOperation.class]){
        CKFetchRecordsOperation *fetch=(id)operation;self.fetches++;
        NSError *missing=self.head?nil:[NSError errorWithDomain:CKErrorDomain code:CKErrorUnknownItem userInfo:nil];
        if(fetch.perRecordCompletionBlock)fetch.perRecordCompletionBlock([self.head copy],fetch.recordIDs.firstObject,missing);
        if(fetch.fetchRecordsCompletionBlock)fetch.fetchRecordsCompletionBlock(@{},nil);
    }else if([operation isKindOfClass:CKModifyRecordsOperation.class]){
        CKModifyRecordsOperation *modify=(id)operation;self.uploads++;self.allCAS &= modify.savePolicy==CKRecordSaveIfServerRecordUnchanged;
        if(self.beforeUpload)self.beforeUpload();
        CKRecord *candidate=modify.recordsToSave.firstObject;
        if(!self.uploadError){
            // A mock server keeps its own asset after the client's temp upload
            // is removed. CKRecord objects and all archive decoding remain real.
            self.successfulUploads++;CKRecord *saved=[candidate copy];CKAsset *asset=saved[@"payload"];
            NSString *file=child(self.serverDirectory,[NSString stringWithFormat:@"server-%lu.plist",(unsigned long)self.uploads]);
            if(![NSFileManager.defaultManager copyItemAtPath:asset.fileURL.path toPath:file error:nil])abort();
            saved[@"payload"]=[[CKAsset alloc] initWithFileURL:[NSURL fileURLWithPath:file]];self.head=saved;
        }
        if(modify.perRecordSaveBlock)modify.perRecordSaveBlock(candidate.recordID,self.uploadError?nil:[self.head copy],self.uploadError);
        if(modify.modifyRecordsCompletionBlock)modify.modifyRecordsCompletionBlock(@[],@[],self.uploadError);
    }else abort();
}
@end

static TOTKCloudSave *coordinator(NSString *directory,NSString *user){
    mkdirs(directory);
    // Avoid init's real Documents path and real CKContainer lookup. All audited
    // persistence, scheduling, reconciliation, upload and import methods run as-is.
    TOTKCloudSave *value=class_createInstance(TOTKCloudSave.class,0);
    value.queue=dispatch_queue_create("synthetic.totk.cloud-audit",DISPATCH_QUEUE_SERIAL);
    value.snapshotLock=[NSLock new];value.journalLock=[NSLock new];value.directory=directory;value.profile=child(directory,@"synthetic-local-save");
    value.state=[[NSDictionary dictionaryWithContentsOfFile:child(directory,@"state.plist")] mutableCopy]?:[NSMutableDictionary new];
    AuditAccount *account=[AuditAccount new];account.user=user;value.container=(id)account;
    AuditDatabase *db=[AuditDatabase new];db.allCAS=YES;db.serverDirectory=child(directory,@"mock-server");mkdirs(db.serverDirectory);value.database=(id)db;value.started=YES;return value;
}
static NSDictionary *snapshot(NSString *tag){
    uint64_t words[2]={1,2};NSMutableDictionary *files=[NSMutableDictionary new];files[@"account.uid"]=[NSData dataWithBytes:words length:sizeof words];
    NSString *folder=@"save/00000000000000020000000000000001/slot_00";
    for(NSString *name in @[@"progress.sav",@"caption.sav",@"footprint.sav",@"direct_file_save_related.sav"])
        files[child(folder,name)]=[[NSString stringWithFormat:@"authored synthetic %@; no game progress",tag] dataUsingEncoding:NSUTF8StringEncoding];
    return @{@"format":@1,@"files":files,@"digest":digest(files)};
}
static NSDictionary *status(TOTKCloudSave *value){NSData *data=[NSData dataWithContentsOfFile:child(value.directory,@"status.json")];return data?[NSJSONSerialization JSONObjectWithData:data options:0 error:nil]:nil;}
static void drain(TOTKCloudSave *value){
    for(int i=0;i<100;i++){__block BOOL busy;dispatch_sync(value.queue,^{busy=value.syncScheduled;});if(!busy)return;usleep(1000);}check(NO,@"serial sync drained without a stuck retry");
}
static BOOL acked(TOTKCloudSave *value,NSDictionary *save){NSDictionary *stored=[NSDictionary dictionaryWithContentsOfFile:child(value.directory,@"state.plist")];return [stored[@"baseDigest"] isEqual:save[@"digest"]] && [status(value)[@"status"] isEqual:@"Synced with iCloud"] && !value.pendingState;}
static void commit(TOTKCloudSave *value,NSDictionary *save){[value committed:save];drain(value);}
static void poll(TOTKCloudSave *value){[value checkForChanges];drain(value);}
static void blockState(TOTKCloudSave *value){NSString *file=child(value.directory,@"state.plist");if([NSFileManager.defaultManager fileExistsAtPath:file])removeFixture(file);mkdirs(file);}

static void journalFailure(NSString *root,NSDictionary *save){
    TOTKCloudSave *c=coordinator(child(root,@"journal"),@"synthetic-A");AuditDatabase *db=(id)c.database;NSString *file=child(c.directory,@"last-committed.plist");mkdirs(file);
    NSError *e=nil;check(![TOTKEncodeSave(save,&e) writeToFile:file options:NSDataWritingAtomic error:&e] && e!=nil,@"negative control uses an actual Foundation journal write error");
    commit(c,save);check(db.uploads==0 && db.fetches==0 && ((AuditAccount *)(id)c.container).reads==0,@"failed journal prevents all cloud work");
    check([[c snapshot][@"digest"] isEqual:save[@"digest"]] && !c.journaledDigest,@"failed commit remains available for retry without a false durable digest");
    check([status(c)[@"status"] isEqual:@"iCloud sync pending"] && !c.state[@"baseDigest"],@"failed journal never reports Synced or advances ancestry");
    removeFixture(file);poll(c);NSDictionary *journal=TOTKDecodeSave([NSData dataWithContentsOfFile:file],nil);
    check([journal[@"digest"] isEqual:save[@"digest"]] && db.uploads==1 && acked(c,save),@"normal retry durably journals then uploads and acknowledges the same commit");
    [observations addObject:@{@"case":@"journal_failure_recovery",@"closed":@YES,@"uploads_after_repair":@(db.uploads)}];
}
static void acknowledgementFailure(NSString *root,NSDictionary *a,NSDictionary *b){
    TOTKCloudSave *c=coordinator(child(root,@"ack-state"),@"synthetic-A");AuditDatabase *db=(id)c.database;__weak TOTKCloudSave *weak=c;
    db.beforeUpload=^{blockState(weak);};commit(c,a);db.beforeUpload=nil;
    check(db.successfulUploads==1 && !c.state[@"baseDigest"] && [c.pendingState[@"baseDigest"] isEqual:a[@"digest"]],@"confirmed upload retains pending ancestry instead of publishing failed state");
    check(![status(c)[@"status"] isEqual:@"Synced with iCloud"],@"state failure after server confirmation is pending, never Synced");
    __block NSError *persistenceError=nil;__block BOOL acknowledged=YES;
    dispatch_sync(c.queue,^{acknowledged=[c acknowledge:a error:&persistenceError];});
    check(!acknowledged && persistenceError!=nil,@"failed acknowledgement persistence returns an actionable NSError");
    NSUInteger fetches=db.fetches;commit(c,b);
    check(db.uploads==1 && db.fetches==fetches && [[c snapshot][@"digest"] isEqual:b[@"digest"]],@"newer commit waits while confirmed prior ancestry cannot persist");
    removeFixture(child(c.directory,@"state.plist"));poll(c);
    check(db.uploads==2 && acked(c,b),@"retry persists confirmed parent before reconciling/uploading newer progress; no false conflict");
    check([TOTKDecodeSave([NSData dataWithContentsOfFile:child(c.directory,@"last-committed.plist")],nil)[@"digest"] isEqual:b[@"digest"]],@"newer committed journal is retained through failed-ack recovery");
    [observations addObject:@{@"case":@"state_failure_recovery",@"closed":@YES,@"uploads":@(db.uploads)}];
}
static void equalAckRetry(NSString *root,NSDictionary *save){
    TOTKCloudSave *c=coordinator(child(root,@"equal-ack-retry"),@"synthetic-A");AuditDatabase *db=(id)c.database;__weak TOTKCloudSave *weak=c;
    db.beforeUpload=^{blockState(weak);};commit(c,save);db.beforeUpload=nil;
    removeFixture(child(c.directory,@"state.plist"));poll(c);
    check(db.uploads==1 && acked(c,save),@"pending acknowledgement retry does not upload an already confirmed save twice");
    poll(c);check(db.uploads==1 && acked(c,save),@"normal equal-digest polling remains idempotent");
}
static void accountFailure(NSString *root,NSDictionary *save){
    TOTKCloudSave *c=coordinator(child(root,@"account-pin"),@"synthetic-A");AuditDatabase *db=(id)c.database;blockState(c);commit(c,save);
    check(db.uploads==0 && db.fetches==0 && !c.state[@"cloudUser"],@"failed account pin permits neither record fetch/upload nor an in-memory-only account pin");
    __block NSError *error=nil;__block BOOL accepted=YES;dispatch_sync(c.queue,^{accepted=[c checkAccount:&error];});
    check(!accepted && error!=nil,@"failed account pin returns an actionable NSError");
    removeFixture(child(c.directory,@"state.plist"));poll(c);check(acked(c,save) && [c.state[@"cloudUser"] isEqual:@"synthetic-A"],@"writable account control pins before normal sync");
    TOTKCloudSave *restart=coordinator(c.directory,@"synthetic-B");NSError *mismatch=nil;
    check(![restart checkAccount:&mismatch] && mismatch!=nil,@"persisted pin still rejects another account after restart");
    check(((AuditDatabase *)(id)restart.database).uploads==0 && ((AuditDatabase *)(id)restart.database).fetches==0,@"rejected account never reaches the cloud record service");
    [observations addObject:@{@"case":@"account_pin_failure_recovery",@"closed":@YES}];
}
static void inFlightCommit(NSString *root,NSDictionary *a,NSDictionary *b){
    TOTKCloudSave *c=coordinator(child(root,@"newer-during-upload"),@"synthetic-A");AuditDatabase *db=(id)c.database;__weak TOTKCloudSave *weak=c;__weak AuditDatabase *weakDB=db;
    db.beforeUpload=^{weakDB.beforeUpload=nil;[weak committed:b];};commit(c,a);
    check(db.uploads==2 && acked(c,b),@"commit arriving during upload drains normally without losing newest progress");
    check([TOTKDecodeSave([NSData dataWithContentsOfFile:child(c.directory,@"last-committed.plist")],nil)[@"digest"] isEqual:b[@"digest"]],@"serialized publication/journaling cannot regress to the older in-flight snapshot");
}
static void casAndConflict(NSString *root,NSDictionary *a,NSDictionary *b,NSDictionary *other){
    TOTKCloudSave *c=coordinator(child(root,@"CAS-conflict"),@"synthetic-A");AuditDatabase *db=(id)c.database;commit(c,a);
    db.uploadError=[NSError errorWithDomain:CKErrorDomain code:CKErrorServerRecordChanged userInfo:nil];commit(c,b);
    check([c.state[@"baseDigest"] isEqual:a[@"digest"]] && !c.pendingState && ![status(c)[@"status"] isEqual:@"Synced with iCloud"],@"CAS failure never acknowledges a rejected upload");
    db.uploadError=nil;poll(c);check(acked(c,b) && db.allCAS,@"CAS policy and normal retry remain unchanged");
    CKRecord *remote=[db.head copy];NSString *file=child(c.directory,@"authored-remote-conflict.plist");[TOTKEncodeSave(other,nil) writeToFile:file atomically:YES];remote[@"payload"]=[[CKAsset alloc] initWithFileURL:[NSURL fileURLWithPath:file]];remote[@"digest"]=other[@"digest"];db.head=remote;
    NSUInteger uploads=db.uploads;commit(c,a);
    check(db.uploads==uploads && [status(c)[@"status"] isEqual:@"Another device has a save"],@"real three-way reconciliation still preserves divergent saves without upload");
    for(NSString *name in @[@"local-conflict.plist",@"cloud-conflict.plist"])
        check(TOTKDecodeSave([NSData dataWithContentsOfFile:child(c.directory,name)],nil)!=nil,@"both conflict backups remain complete and validated");
}
static BOOL boot(TOTKCloudSave *c){
    c.started=NO;__block BOOL complete=NO;unsigned before=presentations;
    [c prepareForController:[UIViewController new] completion:^{complete=YES;}];
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:3];
    while(!complete && presentations==before && deadline.timeIntervalSinceNow>0)[NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.005]];
    [c.timer invalidate];c.timer=nil;drain(c);return complete;
}
static void bootPaths(NSString *root,NSDictionary *save){
    TOTKCloudSave *local=coordinator(child(root,@"local-boot"),@"synthetic-A");
    check(TOTKRestoreSave(save,local.profile,child(local.directory,@"Backups"),nil),@"synthetic local save passes the real atomic restore/readback path");
    mkdirs(child(local.directory,@"last-committed.plist"));check(!boot(local),@"failed startup journal does not start the game or silently upload");
    check(((AuditDatabase *)(id)local.database).uploads==0,@"startup journal error gates cloud work");
    removeFixture(child(local.directory,@"last-committed.plist"));check(boot(local) && acked(local,save),@"normal startup retry journals and syncs before continuing");
    TOTKCloudSave *remote=coordinator(child(root,@"remote-boot"),@"synthetic-A");AuditDatabase *db=(id)remote.database;
    NSString *asset=child(remote.directory,@"authored-remote.plist");[TOTKEncodeSave(save,nil) writeToFile:asset atomically:YES];CKRecord *head=[[CKRecord alloc] initWithRecordType:@"TOTKSave" recordID:remote.recordID];head[@"digest"]=save[@"digest"];head[@"payload"]=[[CKAsset alloc] initWithFileURL:[NSURL fileURLWithPath:asset]];db.head=head;
    mkdirs(child(remote.directory,@"last-committed.plist"));check(!boot(remote),@"imported save is not acknowledged/launched if its committed journal fails");
    check([TOTKReadSave(remote.profile,nil)[@"digest"] isEqual:save[@"digest"]] && !remote.state[@"baseDigest"],@"failed post-import journal leaves complete local progress intact without false acknowledgement");
    removeFixture(child(remote.directory,@"last-committed.plist"));check(boot(remote) && acked(remote,save) && db.uploads==0,@"preboot import retry succeeds without a spurious upload or format change");
}
int main(int argc,char **argv){@autoreleasepool{
    if(argc!=2)return 2;NSString *root=@(argv[1]);mkdirs(root);observations=[NSMutableArray new];NSDictionary *a=snapshot(@"A"),*b=snapshot(@"B"),*other=snapshot(@"remote-C");
    check(TOTKEncodeSave(a,nil)!=nil && TOTKEncodeSave(b,nil)!=nil,@"complete authored snapshots pass real archive validation");
    journalFailure(root,a);acknowledgementFailure(root,a,b);equalAckRetry(root,a);accountFailure(root,a);inFlightCommit(root,a,b);casAndConflict(root,a,b,other);bootPaths(root,a);
    NSDictionary *result=@{@"status":failures?@"failed":@"passed",@"checks":@(checks),@"failures":@(failures),@"closed_failure_classes":observations,
        @"scope":@"Real unchanged archive and repaired coordinator, real Foundation filesystem failures/recovery; UI and CloudKit service callbacks are fake. All data synthetic. No game, network or device operations."};
    [[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:nil] writeToFile:child(root,@"observations.json") options:NSDataWritingAtomic error:nil];
    printf("{\"summary\":true,\"checks\":%u,\"failures\":%u}\n",checks,failures);return failures?1:0;
}}
