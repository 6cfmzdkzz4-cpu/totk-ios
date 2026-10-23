// CloudKit transfers one checksum-verified asset per committed save, guarded by server change tags.
#import "TOTKCloudSave.h"
#import "TOTKSaveArchive.h"
#import <CloudKit/CloudKit.h>

@interface TOTKCloudSave ()
@property(strong) dispatch_queue_t queue;
@property(strong) CKContainer *container;
@property(strong) CKDatabase *database;
@property(copy) NSString *profile;
@property(copy) NSString *directory;
@property(strong) NSMutableDictionary *state;
@property(strong) NSMutableDictionary *pendingState; // serial queue: confirmed ancestry awaiting persistence
@property(strong) NSLock *snapshotLock;
@property(strong) NSLock *journalLock; // local journal write/publication only; never a network wait
@property(copy) NSString *journaledDigest; // journalLock; nil after failure or process restart
@property(strong) NSDictionary *latest;
@property(copy) NSString *detail;
@property(weak) UIViewController *controller;
@property BOOL started;
@property BOOL syncScheduled;
@property(strong) NSTimer *timer;
- (NSDictionary *)snapshotForSync:(NSError **)error;
@end

static NSError *cloudError(NSString *message) {
    return [NSError errorWithDomain:@"TOTKiCloud" code:1 userInfo:@{NSLocalizedDescriptionKey:message}];
}
static NSString *configuredCloudContainer(void) {
    id value=NSBundle.mainBundle.infoDictionary[@"TOTKCloudContainer"];
    if(![value isKindOfClass:NSString.class])return nil;
    NSString *identifier=[value stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    NSCharacterSet *invalid=[[NSCharacterSet characterSetWithCharactersInString:@"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-"] invertedSet];
    if(![identifier hasPrefix:@"iCloud."] || identifier.length<=7 ||
       [identifier rangeOfCharacterFromSet:invalid].location!=NSNotFound ||
       [identifier containsString:@".."] || [identifier hasSuffix:@"."])return nil;
    return identifier;
}
@implementation TOTKCloudSave
+ (instancetype)shared {
    static TOTKCloudSave *value; static dispatch_once_t once;
    dispatch_once(&once,^{value=[self new];}); return value;
}
- (instancetype)init {
    if((self=[super init])) {
        _queue=dispatch_queue_create("org.macnativegames.totk.cloud-save",DISPATCH_QUEUE_SERIAL);
        _snapshotLock=[NSLock new];_journalLock=[NSLock new];
        NSString *docs=NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,NSUserDomainMask,YES).firstObject;
        _profile=[docs stringByAppendingPathComponent:@"save"];
        _directory=[docs stringByAppendingPathComponent:@"iCloudSync"];
        [NSFileManager.defaultManager createDirectoryAtPath:_directory withIntermediateDirectories:YES attributes:nil error:nil];
        _state=[[NSDictionary dictionaryWithContentsOfFile:[_directory stringByAppendingPathComponent:@"state.plist"]] mutableCopy] ?: [NSMutableDictionary new];
        NSString *identifier=configuredCloudContainer();
        if(identifier) {
            _container=[CKContainer containerWithIdentifier:identifier];
            _database=_container.privateCloudDatabase;
            _detail=@"Game progress is saved locally while iCloud is unavailable.";
        } else {
            _detail=@"iCloud save sync is not configured. Game progress is saved locally.";
        }
    } return self;
}
- (BOOL)saveState:(NSMutableDictionary *)next error:(NSError **)error {
    if(![next writeToFile:[self.directory stringByAppendingPathComponent:@"state.plist"] atomically:YES]) {
        if(error)*error=cloudError(@"Could not persist iCloud sync state. Local progress is retained; retrying automatically.");
        return NO;
    }
    self.state=next;return YES;
}
- (NSDictionary *)snapshot {
    [self.snapshotLock lock];NSDictionary *save=self.latest;[self.snapshotLock unlock];return save;
}
- (void)publish:(NSDictionary *)save {
    [self.snapshotLock lock];self.latest=save;[self.snapshotLock unlock];
}
- (void)setMessage:(NSString *)status detail:(NSString *)detail {
    fprintf(stderr,"[totk-cloud] %s: %s\n",status.UTF8String,detail.UTF8String);
    // State belongs to the serial sync queue; commits can report status from the game thread.
    NSDictionary *receipt=@{@"status":status,@"detail":detail,@"date":NSDate.date.description,@"device":UIDevice.currentDevice.model};
    NSData *json=[NSJSONSerialization dataWithJSONObject:receipt options:NSJSONWritingPrettyPrinted error:nil];
    [json writeToFile:[self.directory stringByAppendingPathComponent:@"status.json"] options:NSDataWritingAtomic error:nil];
    dispatch_async(dispatch_get_main_queue(),^{
        self.detail=detail;
    });
}
- (CKRecordID *)recordID { return [[CKRecordID alloc] initWithRecordName:@"totk-save-format-1"]; }
- (BOOL)checkAccount:(NSError **)error {
    // Retain a server-confirmed base in memory until it can be persisted. This
    // prevents our own prior upload looking like remote divergence on retry.
    if(self.pendingState) {
        if(![self saveState:self.pendingState error:error])return NO;
        self.pendingState=nil;
    }
    dispatch_semaphore_t done=dispatch_semaphore_create(0);
    __block CKRecordID *user=nil; __block NSError *failure=nil;
    [self.container fetchUserRecordIDWithCompletionHandler:^(CKRecordID *recordID,NSError *e){user=recordID;failure=e;dispatch_semaphore_signal(done);}];
    if(dispatch_semaphore_wait(done,dispatch_time(DISPATCH_TIME_NOW,20*NSEC_PER_SEC))) {if(error)*error=cloudError(@"iCloud did not respond. Your local save is safe.");return NO;}
    if(failure || !user) {if(error)*error=failure ?: cloudError(@"Sign in to iCloud to sync saves.");return NO;}
    NSString *previous=self.state[@"cloudUser"];
    if(previous && ![previous isEqual:user.recordName]) {if(error)*error=cloudError(@"The iCloud account changed. Sync is paused to protect the saves on both accounts.");return NO;}
    NSMutableDictionary *next=[self.state mutableCopy];next[@"cloudUser"]=user.recordName;
    return [self saveState:next error:error];
}
- (CKRecord *)fetchRecordWithAsset:(BOOL)asset error:(NSError **)error {
    CKFetchRecordsOperation *operation=[[CKFetchRecordsOperation alloc] initWithRecordIDs:@[self.recordID]];
    if(!asset)operation.desiredKeys=@[@"digest",@"device",@"updated"];
    operation.configuration.timeoutIntervalForRequest=20; operation.configuration.timeoutIntervalForResource=30;
    dispatch_semaphore_t done=dispatch_semaphore_create(0);
    __block CKRecord *result=nil; __block NSError *failure=nil;
    operation.perRecordCompletionBlock=^(CKRecord *record,CKRecordID *identifier,NSError *e){(void)identifier;result=record;failure=e;};
    operation.fetchRecordsCompletionBlock=^(NSDictionary *records,NSError *e){(void)records;if(!failure)failure=e;dispatch_semaphore_signal(done);};
    [self.database addOperation:operation];
    if(dispatch_semaphore_wait(done,dispatch_time(DISPATCH_TIME_NOW,35*NSEC_PER_SEC))) {[operation cancel];if(error)*error=cloudError(@"iCloud download timed out. Your local save is safe.");return nil;}
    if(failure.code==CKErrorUnknownItem && [failure.domain isEqual:CKErrorDomain])failure=nil;
    if(error)*error=failure; return result;
}
- (NSDictionary *)readRecord:(CKRecord *)record error:(NSError **)error {
    if(!record)return nil;
    CKAsset *asset=record[@"payload"];
    if(![asset isKindOfClass:CKAsset.class] || !asset.fileURL){if(error)*error=cloudError(@"The iCloud save download is incomplete.");return nil;}
    NSData *data=[NSData dataWithContentsOfURL:asset.fileURL options:0 error:error];
    NSDictionary *save=data?TOTKDecodeSave(data,error):nil;
    if(save && ![save[@"digest"] isEqual:record[@"digest"]]){if(error)*error=cloudError(@"The iCloud save checksum did not match.");return nil;}
    return save;
}
- (BOOL)acknowledge:(NSDictionary *)save error:(NSError **)error {
    NSMutableDictionary *next=[self.state mutableCopy];
    next[@"baseDigest"]=save[@"digest"];next[@"lastSynced"]=NSDate.date;
    if(![self saveState:next error:error]){self.pendingState=next;return NO;}
    self.pendingState=nil;
    [self setMessage:@"Synced with iCloud" detail:@"Your latest committed save is in iCloud. Close this game before continuing on another device."];
    return YES;
}
- (BOOL)upload:(NSDictionary *)save replacing:(CKRecord *)record error:(NSError **)error {
    NSData *data=TOTKEncodeSave(save,error);if(!data)return NO;
    NSString *file=[self.directory stringByAppendingPathComponent:[NSString stringWithFormat:@"upload-%@.plist",NSUUID.UUID.UUIDString]];
    if(![data writeToFile:file options:NSDataWritingAtomic error:error])return NO;
    CKRecord *head=record ?: [[CKRecord alloc] initWithRecordType:@"TOTKSave" recordID:self.recordID];
    head[@"payload"]=[[CKAsset alloc] initWithFileURL:[NSURL fileURLWithPath:file]];
    head[@"digest"]=save[@"digest"];head[@"device"]=UIDevice.currentDevice.model;head[@"updated"]=NSDate.date;
    CKModifyRecordsOperation *operation=[[CKModifyRecordsOperation alloc] initWithRecordsToSave:@[head] recordIDsToDelete:nil];
    operation.savePolicy=CKRecordSaveIfServerRecordUnchanged;
    operation.configuration.timeoutIntervalForRequest=20;operation.configuration.timeoutIntervalForResource=40;
    dispatch_semaphore_t done=dispatch_semaphore_create(0);
    __block CKRecord *saved=nil;__block NSError *failure=nil;
    operation.perRecordSaveBlock=^(CKRecordID *identifier,CKRecord *result,NSError *e){(void)identifier;saved=result;failure=e;};
    operation.modifyRecordsCompletionBlock=^(NSArray *records,NSArray *deleted,NSError *e){(void)records;(void)deleted;if(!failure)failure=e;dispatch_semaphore_signal(done);};
    [self.database addOperation:operation];
    if(dispatch_semaphore_wait(done,dispatch_time(DISPATCH_TIME_NOW,45*NSEC_PER_SEC))){[operation cancel];if(error)*error=cloudError(@"iCloud upload timed out. Your save will retry automatically.");return NO;}
    [NSFileManager.defaultManager removeItemAtPath:file error:nil];
    if(failure || !saved){if(error)*error=failure ?: cloudError(@"iCloud did not confirm this upload.");return NO;}
    return [self acknowledge:save error:error];
}
- (NSString *)sync:(NSDictionary *)local boot:(BOOL)boot error:(NSError **)error {
    if(![self checkAccount:error])return @"error";
    CKRecord *head=[self fetchRecordWithAsset:NO error:error];if(error && *error)return @"error";
    NSString *remoteDigest=head[@"digest"];
    if(head && ![remoteDigest isKindOfClass:NSString.class]){if(error)*error=cloudError(@"Invalid iCloud save metadata.");return @"error";}
    NSString *decision=TOTKSaveDecision(local[@"digest"],self.state[@"baseDigest"],remoteDigest);
    if([decision isEqual:@"equal"])return [self acknowledge:local error:error]?@"done":@"error";
    if([decision isEqual:@"empty"]){[self setMessage:@"Ready for iCloud" detail:@"Your first in-game save will sync automatically."];return @"done";}
    if([decision isEqual:@"upload"]){return [self upload:local replacing:head error:error]?@"done":@"error";}
    // A complete asset is downloaded only for an import or conflict backup, never on every poll.
    head=[self fetchRecordWithAsset:YES error:error];if(error && *error)return @"error";
    NSDictionary *remote=[self readRecord:head error:error];if(error && *error)return @"error";
    if(![remote[@"digest"] isEqual:remoteDigest]){if(error)*error=cloudError(@"The other device saved again. Please retry sync.");return @"error";}
    if([decision isEqual:@"download"] && boot) {
        if(!TOTKRestoreSave(remote,self.profile,[self.directory stringByAppendingPathComponent:@"Backups"],error))return @"error";
        [self publish:remote];
        if(![self snapshotForSync:error])return @"error";
        return [self acknowledge:remote error:error]?@"done":@"error";
    }
    if(local) [TOTKEncodeSave(local,nil) writeToFile:[self.directory stringByAppendingPathComponent:@"local-conflict.plist"] options:NSDataWritingAtomic error:nil];
    if(remote) [TOTKEncodeSave(remote,nil) writeToFile:[self.directory stringByAppendingPathComponent:@"cloud-conflict.plist"] options:NSDataWritingAtomic error:nil];
    [self setMessage:@"Another device has a save" detail:@"Both versions are preserved. Close and reopen the game to choose which progress to continue."];
    return @"conflict";
}
- (void)finishBoot:(void (^)(void))completion {
    self.started=YES;
    if(self.database)self.timer=[NSTimer scheduledTimerWithTimeInterval:30 repeats:YES block:^(NSTimer *timer){(void)timer;[self checkForChanges];}];
    completion();
}
- (void)prepareForController:(UIViewController *)controller completion:(void (^)(void))completion {
    self.controller=controller;
    if(!self.database) {
        fprintf(stderr,"[totk-cloud] disabled: no valid TOTKCloudContainer in Info.plist; local saves remain active\n");
        completion();
        return;
    }
    dispatch_async(self.queue,^{@autoreleasepool {
        NSError *error=nil;NSDictionary *local=TOTKReadSave(self.profile,nil);
        [self publish:local];local=[self snapshotForSync:&error];
        NSString *result=error?@"error":[self sync:local boot:YES error:&error];
        if(error)[self setMessage:@"iCloud sync pending" detail:error.localizedDescription];
        dispatch_async(dispatch_get_main_queue(),^{
            if([result isEqual:@"done"]){[self finishBoot:completion];return;}
            UIAlertController *alert=[UIAlertController alertControllerWithTitle:[result isEqual:@"conflict"]?@"Choose game progress":@"iCloud is unavailable" message:self.detail preferredStyle:UIAlertControllerStyleAlert];
            if([result isEqual:@"conflict"]) {
                [alert addAction:[UIAlertAction actionWithTitle:@"Use iCloud save" style:UIAlertActionStyleDefault handler:^(UIAlertAction *action){(void)action;[self resolveCloud:YES completion:completion];}]];
                [alert addAction:[UIAlertAction actionWithTitle:@"Keep this device's save" style:UIAlertActionStyleDefault handler:^(UIAlertAction *action){(void)action;[self resolveCloud:NO completion:completion];}]];
            } else {
                [alert addAction:[UIAlertAction actionWithTitle:@"Retry" style:UIAlertActionStyleDefault handler:^(UIAlertAction *action){(void)action;[self prepareForController:controller completion:completion];}]];
            }
            [alert addAction:[UIAlertAction actionWithTitle:@"Play offline" style:UIAlertActionStyleCancel handler:^(UIAlertAction *action){(void)action;[self finishBoot:completion];}]];
            [controller presentViewController:alert animated:YES completion:nil];
        });
    }});
}
- (void)resolveCloud:(BOOL)useCloud completion:(void (^)(void))completion {
    dispatch_async(self.queue,^{@autoreleasepool {
        NSError *error=nil;BOOL account=[self checkAccount:&error];CKRecord *head=account?[self fetchRecordWithAsset:YES error:&error]:nil;NSDictionary *remote=head?[self readRecord:head error:&error]:nil;
        BOOL ok=NO;
        if(!error && remote) {
            if(useCloud) {
                ok=TOTKRestoreSave(remote,self.profile,[self.directory stringByAppendingPathComponent:@"Backups"],&error);
                if(ok){[self publish:remote];ok=[self snapshotForSync:&error] && [self acknowledge:remote error:&error];}
            } else {
                NSDictionary *local=[self snapshotForSync:&error];
                if(!error)ok=[self upload:local replacing:head error:&error];
            }
        }
        if(!ok)[self setMessage:@"iCloud sync pending" detail:error.localizedDescription ?: @"Could not resolve the save; both copies are preserved."];
        dispatch_async(dispatch_get_main_queue(),^{
            if(ok){[self finishBoot:completion];return;}
            UIAlertController *alert=[UIAlertController alertControllerWithTitle:@"The save could not be loaded" message:self.detail preferredStyle:UIAlertControllerStyleAlert];
            [alert addAction:[UIAlertAction actionWithTitle:@"Retry" style:UIAlertActionStyleDefault handler:^(UIAlertAction *action){(void)action;[self resolveCloud:useCloud completion:completion];}]];
            [alert addAction:[UIAlertAction actionWithTitle:@"Play local save" style:UIAlertActionStyleCancel handler:^(UIAlertAction *action){(void)action;[self finishBoot:completion];}]];
            [self.controller presentViewController:alert animated:YES completion:nil];
        });
    }});
}
- (void)checkForChanges {
    if(!self.database || !self.started)return;
    [self.snapshotLock lock];
    if(self.syncScheduled){[self.snapshotLock unlock];return;}
    self.syncScheduled=YES;[self.snapshotLock unlock];
    dispatch_async(self.queue,^{@autoreleasepool {
        // Read the newest commit when the work executes; a queued timer must never upload an older snapshot.
        NSError *error=nil;NSDictionary *snapshot=[self snapshotForSync:&error];
        NSString *result=error?@"error":[self sync:snapshot boot:NO error:&error];
        if(error)[self setMessage:@"iCloud sync pending" detail:error.localizedDescription];
        // Background sync stays silent; a conflicting save is preserved for the next startup choice.
        [self.snapshotLock lock];self.syncScheduled=NO;BOOL newer=![self.latest[@"digest"] isEqual:snapshot[@"digest"]] && self.latest!=nil;[self.snapshotLock unlock];
        if(newer && [result isEqual:@"done"])[self checkForChanges];
    }});
}
- (void)flushForBackground {
    if(!self.database || !self.started)return;
    __block UIBackgroundTaskIdentifier task=UIBackgroundTaskInvalid;
    task=[UIApplication.sharedApplication beginBackgroundTaskWithName:@"Sync completed TOTK save" expirationHandler:^{
        if(task!=UIBackgroundTaskInvalid){[UIApplication.sharedApplication endBackgroundTask:task];task=UIBackgroundTaskInvalid;}
    }];
    [self checkForChanges];
    dispatch_async(self.queue,^{dispatch_async(dispatch_get_main_queue(),^{
        if(task!=UIBackgroundTaskInvalid){[UIApplication.sharedApplication endBackgroundTask:task];task=UIBackgroundTaskInvalid;}
    });});
}
- (BOOL)writeJournal:(NSDictionary *)save error:(NSError **)error {
    NSData *encoded=TOTKEncodeSave(save,error);if(!encoded)return NO;
    return [encoded writeToFile:[self.directory stringByAppendingPathComponent:@"last-committed.plist"] options:NSDataWritingAtomic error:error];
}
- (NSDictionary *)snapshotForSync:(NSError **)error {
    [self.journalLock lock];NSDictionary *save=[self snapshot];
    if(save && ![self.journaledDigest isEqual:save[@"digest"]]) {
        if(![self writeJournal:save error:error]) {
            if(error && !*error)*error=cloudError(@"Could not retain the completed-save journal. Local progress is safe; retrying automatically.");
            [self.journalLock unlock];return nil;
        }
        self.journaledDigest=save[@"digest"];
    }
    [self.journalLock unlock];return save;
}
- (void)committed:(NSDictionary *)save {
    // Serialize the local write and publication so an older retry cannot replace
    // a newer journal. No CloudKit operation runs while journalLock is held.
    [self.journalLock lock];NSError *error=nil;BOOL written=[self writeJournal:save error:&error];
    self.journaledDigest=written?save[@"digest"]:nil;[self publish:save];[self.journalLock unlock];
    if(!written)fprintf(stderr,"[totk-cloud] snapshot backup failed; local game save retained: %s\n",error.localizedDescription.UTF8String ?: "encoding failed");
    if(!self.database)return;
    if(written)[self setMessage:@"Syncing with iCloud" detail:@"Uploading your completed save. Local progress is already saved."];
    else [self setMessage:@"iCloud sync pending" detail:error.localizedDescription ?: @"Local progress is retained; the completed-save journal will retry."];
    [self checkForChanges];
}
@end
void runtime_ios_save_committed(const char *profile) {
    @autoreleasepool {
        NSError *error=nil;NSDictionary *save=TOTKReadSave(@(profile),&error);
        if(save)[[TOTKCloudSave shared] committed:save];
        else fprintf(stderr,"[totk-cloud] commit has no complete snapshot: %s\n",error.localizedDescription.UTF8String ?: "not available");
    }
}
