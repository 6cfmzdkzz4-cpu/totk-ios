// Snapshot only committed game progress and its account identity; caches remain device-local.
#import "TOTKSaveArchive.h"
#import <CommonCrypto/CommonDigest.h>
#include <stdio.h>
#include <sys/stat.h>
#include <limits.h>
#include <stdlib.h>

static BOOL reject(NSError **error, NSString *message) {
    if (error) *error = [NSError errorWithDomain:@"TOTKSave" code:1 userInfo:@{NSLocalizedDescriptionKey:message}];
    return NO;
}
static NSString *digest(NSDictionary<NSString *,NSData *> *files) {
    CC_SHA256_CTX ctx; CC_SHA256_Init(&ctx);
    for (NSString *path in [files.allKeys sortedArrayUsingSelector:@selector(compare:)]) {
        NSData *name = [path dataUsingEncoding:NSUTF8StringEncoding], *data = files[path];
        uint64_t sizes[2] = {CFSwapInt64HostToBig(name.length), CFSwapInt64HostToBig(data.length)};
        CC_SHA256_Update(&ctx,sizes,sizeof sizes);
        CC_SHA256_Update(&ctx,name.bytes,(CC_LONG)name.length);
        CC_SHA256_Update(&ctx,data.bytes,(CC_LONG)data.length);
    }
    unsigned char bytes[CC_SHA256_DIGEST_LENGTH]; CC_SHA256_Final(bytes,&ctx);
    NSMutableString *result = [NSMutableString string];
    for (unsigned i=0;i<sizeof bytes;i++) [result appendFormat:@"%02x",bytes[i]];
    return result;
}
static BOOL validate(NSDictionary *save, NSError **error) {
    if (![save isKindOfClass:NSDictionary.class] || ![save[@"format"] isEqual:@1] ||
        ![save[@"files"] isKindOfClass:NSDictionary.class] || ![save[@"digest"] isKindOfClass:NSString.class])
        return reject(error,@"Invalid save snapshot format.");
    NSDictionary *files=save[@"files"]; NSData *uid=files[@"account.uid"];
    if (![uid isKindOfClass:NSData.class] || uid.length!=16 || files.count>4096)
        return reject(error,@"Invalid save account.");
    uint64_t words[2]; [uid getBytes:words length:16];
    if (!(words[0]|words[1])) return reject(error,@"Empty save account.");
    NSString *prefix=[NSString stringWithFormat:@"save/%016llx%016llx/",words[1],words[0]];
    NSUInteger total=0,slots=0;
    for (id key in files) {
        id data=files[key];
        if (![key isKindOfClass:NSString.class] || ![data isKindOfClass:NSData.class]) return reject(error,@"Invalid save entry.");
        NSString *path=key;
        if ([path isEqual:@"account.uid"]) continue;
        if (![path hasPrefix:prefix] || [path containsString:@"\\"] || [path hasPrefix:@"/"])
            return reject(error,@"Unexpected save path.");
        for (NSString *component in path.pathComponents)
            if ([component isEqual:@".."] || [component isEqual:@"."] || !component.length)
                return reject(error,@"Unsafe save path.");
        NSString *relative=[path substringFromIndex:prefix.length];
        NSArray *parts=relative.pathComponents;
        BOOL slot=parts.count==2 && [parts[0] hasPrefix:@"slot_"] && [parts[1] hasSuffix:@".sav"];
        BOOL photo=parts.count==2 && [parts[0] isEqual:@"album"] && [parts[1] hasSuffix:@".jpg"];
        if (!slot && !photo) return reject(error,@"Snapshot contains non-save data.");
        if ([path hasSuffix:@"/progress.sav"]) {
            slots++;
            NSString *folder=path.stringByDeletingLastPathComponent;
            for (NSString *name in @[@"progress.sav",@"caption.sav",@"footprint.sav",@"direct_file_save_related.sav"])
                if (![files[[folder stringByAppendingPathComponent:name]] length]) return reject(error,@"Incomplete save slot.");
        }
        if ([data length]>64*1024*1024 || total>256*1024*1024-[data length]) return reject(error,@"Save snapshot is too large.");
        total+=[data length];
    }
    if (!slots) return reject(error,@"No complete game save is available yet.");
    if (![digest(files) isEqual:save[@"digest"]]) return reject(error,@"Save snapshot checksum mismatch.");
    return YES;
}
NSDictionary *TOTKReadSave(NSString *profile, NSError **error) {
    char resolved[PATH_MAX];
    if(realpath(profile.fileSystemRepresentation,resolved))profile=@(resolved);
    NSFileManager *fm=NSFileManager.defaultManager;
    NSString *account=[profile stringByAppendingPathComponent:@"account.uid"];
    struct stat accountStat;
    if(lstat(account.fileSystemRepresentation,&accountStat) || !S_ISREG(accountStat.st_mode)) {reject(error,@"Invalid local account file.");return nil;}
    NSData *uid=[NSData dataWithContentsOfFile:account options:0 error:error];
    if (uid.length!=16) return nil;
    uint64_t words[2]; [uid getBytes:words length:16];
    NSString *root=[NSString stringWithFormat:@"save/%016llx%016llx",words[1],words[0]];
    NSString *saveRoot=[profile stringByAppendingPathComponent:root];
    NSMutableDictionary *files=[@{@"account.uid":uid} mutableCopy];
    __block NSError *enumerationError=nil;NSUInteger total=0;
    NSDirectoryEnumerator *enumerator=[fm enumeratorAtURL:[NSURL fileURLWithPath:saveRoot]
        includingPropertiesForKeys:@[NSURLIsSymbolicLinkKey,NSURLIsRegularFileKey,NSURLFileSizeKey]
        options:0 errorHandler:^BOOL(NSURL *url,NSError *e){(void)url;enumerationError=e;return NO;}];
    for (NSURL *url in enumerator) {
        NSNumber *symlink=nil,*regular=nil,*size=nil;
        if (![url getResourceValue:&symlink forKey:NSURLIsSymbolicLinkKey error:error] || symlink.boolValue) {reject(error,@"Save contains a symbolic link.");return nil;}
        if (![url getResourceValue:&regular forKey:NSURLIsRegularFileKey error:error]) return nil;
        if (!regular.boolValue) continue;
        if(![url.path hasPrefix:[saveRoot stringByAppendingString:@"/"]]){reject(error,@"Save path escaped its root.");return nil;}
        NSString *relative=[url.path substringFromIndex:saveRoot.length+1];
        if ([relative hasPrefix:@"storage/"]) continue;
        if (![url getResourceValue:&size forKey:NSURLFileSizeKey error:error] || size.unsignedLongLongValue>64*1024*1024 || files.count>=4096 || total>256*1024*1024-size.unsignedLongLongValue) {reject(error,@"Save file limit exceeded.");return nil;}
        total+=size.unsignedLongLongValue;
        NSData *data=[NSData dataWithContentsOfURL:url options:0 error:error]; if(!data)return nil;
        files[[root stringByAppendingPathComponent:relative]]=data;
    }
    if(enumerationError){if(error)*error=enumerationError;return nil;}
    NSDictionary *save=@{@"format":@1,@"files":files,@"digest":digest(files)};
    return validate(save,error)?save:nil;
}
NSData *TOTKEncodeSave(NSDictionary *save,NSError **error) {
    if(!validate(save,error))return nil;
    return [NSPropertyListSerialization dataWithPropertyList:save format:NSPropertyListBinaryFormat_v1_0 options:0 error:error];
}
NSDictionary *TOTKDecodeSave(NSData *data,NSError **error) {
    if (!data || data.length>260*1024*1024) {reject(error,@"Invalid snapshot size.");return nil;}
    NSDictionary *save=[NSPropertyListSerialization propertyListWithData:data options:NSPropertyListImmutable format:nil error:error];
    return validate(save,error)?save:nil;
}
BOOL TOTKRestoreSave(NSDictionary *save,NSString *profile,NSString *backups,NSError **error) {
    if(!validate(save,error))return NO;
    NSFileManager *fm=NSFileManager.defaultManager;
    NSString *identifier=NSUUID.UUID.UUIDString;
    NSString *stage=[profile.stringByDeletingLastPathComponent stringByAppendingPathComponent:[@".save-import-" stringByAppendingString:identifier]];
    BOOL exists=[fm fileExistsAtPath:profile];
    if(exists) {if(![fm copyItemAtPath:profile toPath:stage error:error])return NO;}
    else if(![fm createDirectoryAtPath:stage withIntermediateDirectories:YES attributes:nil error:error])return NO;
    for(NSString *path in @[@"account.uid",@"save"]) {
        NSString *old=[stage stringByAppendingPathComponent:path];
        if([fm fileExistsAtPath:old] && ![fm removeItemAtPath:old error:error]){[fm removeItemAtPath:stage error:nil];return NO;}
    }
    NSDictionary *files=save[@"files"];
    for(NSString *path in files) {
        NSString *out=[stage stringByAppendingPathComponent:path];
        if(![fm createDirectoryAtPath:out.stringByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:error] ||
           ![files[path] writeToFile:out options:NSDataWritingAtomic error:error]){[fm removeItemAtPath:stage error:nil];return NO;}
    }
    NSDictionary *readback=TOTKReadSave(stage,error);
    if(![readback[@"digest"] isEqual:save[@"digest"]]){[fm removeItemAtPath:stage error:nil];return NO;}
    if(![fm createDirectoryAtPath:backups withIntermediateDirectories:YES attributes:nil error:error]){[fm removeItemAtPath:stage error:nil];return NO;}
    if(exists) {
        if(renamex_np(stage.fileSystemRepresentation,profile.fileSystemRepresentation,RENAME_SWAP)){
            reject(error,@"Could not atomically replace the local save; existing save was retained.");[fm removeItemAtPath:stage error:nil];return NO;
        }
        // The swapped-out directory is already a complete recoverable backup, even if moving it fails.
        [fm moveItemAtPath:stage toPath:[backups stringByAppendingPathComponent:identifier] error:nil];
    } else if(![fm moveItemAtPath:stage toPath:profile error:error])return NO;
    return YES;
}
NSString *TOTKSaveDecision(NSString *local,NSString *base,NSString *remote) {
    if([local isEqual:remote])return @"equal";
    if(!local.length)return remote.length?@"download":@"empty";
    if(!remote.length)return base.length?@"conflict":@"upload";
    if([local isEqual:base])return @"download";
    if([remote isEqual:base])return @"upload";
    return @"conflict";
}
