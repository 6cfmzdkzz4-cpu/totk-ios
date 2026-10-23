/* Private actual-rendering relay. Pixels and source metadata are one immutable
   completed interval; this file never reads the latest requested scene. */
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <CommonCrypto/CommonDigest.h>
#include "totk_duo_capture.h"
#include <math.h>
#include <unistd.h>

void totk_duo_capture_pixels(const void *pixels,size_t width,size_t height,size_t row,int bgra,TotkLiveSource source) {
    const char *root=getenv("DM_DUO_RELAY");
    if(!root || !*root || !source.valid || !pixels || !width || !height || width>8192 || height>8192 ||
       row<width*4 || !isfinite(source.width) || !isfinite(source.height) || !isfinite(source.scale) ||
       source.width<0 || source.height<0 || source.scale<0 || !isfinite(source.aspect) || source.aspect<=0 ||
       fabs(((double)width/height)/source.aspect-1)>.002)return;
    for(unsigned i=0;i<4;i++)if(!isfinite(source.safe_insets[i]) || source.safe_insets[i]<0)return;
    @autoreleasepool {
        static NSObject *lock;static NSString *session;static uint64_t images;
        static dispatch_once_t once;
        dispatch_once(&once,^{lock=[NSObject new];session=NSUUID.UUID.UUIDString;});
        @synchronized(lock) {
            CGColorSpaceRef color=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
            CGBitmapInfo bitmap=bgra?(kCGBitmapByteOrder32Little|kCGImageAlphaNoneSkipFirst):
                (kCGBitmapByteOrder32Big|kCGImageAlphaNoneSkipLast);
            CGContextRef context=CGBitmapContextCreate((void *)pixels,width,height,8,row,color,bitmap);
            CGImageRef image=context?CGBitmapContextCreateImage(context):NULL;
            NSMutableData *jpeg=[NSMutableData new];
            CGImageDestinationRef destination=image?CGImageDestinationCreateWithData((__bridge CFMutableDataRef)jpeg,
                (__bridge CFStringRef)UTTypeJPEG.identifier,1,NULL):NULL;
            int encoded=0;
            if(destination){CGImageDestinationAddImage(destination,image,(__bridge CFDictionaryRef)@{(__bridge NSString *)kCGImageDestinationLossyCompressionQuality:@.88});encoded=CGImageDestinationFinalize(destination);}
            if(destination)CFRelease(destination);if(image)CGImageRelease(image);if(context)CGContextRelease(context);if(color)CGColorSpaceRelease(color);
            if(!encoded || !jpeg.length || jpeg.length>UINT32_MAX)return;
            unsigned char digest[CC_SHA256_DIGEST_LENGTH];CC_SHA256(jpeg.bytes,(CC_LONG)jpeg.length,digest);
            char hex[CC_SHA256_DIGEST_LENGTH*2+1];for(unsigned i=0;i<sizeof digest;i++)snprintf(hex+2*i,3,"%02x",digest[i]);
            NSString *directory=@(root),*frame=[directory stringByAppendingPathComponent:@"frame.jpg"];
            NSError *error=nil;
            if(![jpeg writeToFile:frame options:NSDataWritingAtomic error:&error])return;
            BOOL paired=source.paired && !source.fallback;
            NSDictionary *interval=@{@"enabled":@(paired),@"aspect":@(source.aspect),@"generation":@(source.generation)};
            NSDictionary *manifest=@{
                @"schema":@1,@"game":@"Tears of the Kingdom",@"session":session,@"pid":@(getpid()),
                @"relay_images":@(++images),@"backend_presents":@(source.frame),@"source_frame":@(source.frame),
                @"width":@(width),@"height":@(height),@"source_aspect":@(source.aspect),
                @"source_geometry_generation":@(source.generation),@"source_requested_generation":@(source.requested_generation),
                @"source_safe_insets":@[@(source.safe_insets[0]),@(source.safe_insets[1]),@(source.safe_insets[2]),@(source.safe_insets[3])],
                @"frame_sha256":@(hex),@"camera":interval,@"hud":interval,
                @"scope":@"Actual native Mac game pixels after production composition; Duo Simulator preview, not native Simulator execution",
                @"diagnostics":@{@"source_valid":@(source.valid),@"source_fallback":@(source.fallback),
                    @"source_scene_points":@[@(source.width),@(source.height)],@"source_scale":@(source.scale),
                    @"bound_interval_hud_adjustments":@(source.hud_adjustments),@"bound_interval_hud_misses":@(source.hud_misses),
                    @"camera_callback_count_available":@NO,@"camera_callback_count":NSNull.null,
                    @"backend_counter_semantics":@"Completed guest source intervals, not displayed FPS"}};
            NSData *json=[NSJSONSerialization dataWithJSONObject:manifest options:NSJSONWritingPrettyPrinted error:&error];
            if(json)[json writeToFile:[directory stringByAppendingPathComponent:@"backend.json"] options:NSDataWritingAtomic error:&error];
        }
    }
}
