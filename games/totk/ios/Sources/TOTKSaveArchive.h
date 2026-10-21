// Complete, validated save snapshots shared by iCloud sync and its host-side tests.
#import <Foundation/Foundation.h>
NSDictionary *TOTKReadSave(NSString *profile, NSError **error);
NSDictionary *TOTKDecodeSave(NSData *data, NSError **error);
NSData *TOTKEncodeSave(NSDictionary *save, NSError **error);
BOOL TOTKRestoreSave(NSDictionary *save, NSString *profile, NSString *backups, NSError **error);
NSString *TOTKSaveDecision(NSString *local, NSString *base, NSString *remote);
