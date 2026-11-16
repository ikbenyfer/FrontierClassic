// Player flag on iOS: decode with UIImage, choose with UIImagePickerController.
// Everything stays on the iPad: the photo is read from the local photo library
// and saved as Documents/flag.png.
#import <UIKit/UIKit.h>
#include <stdlib.h>
#include <string.h>
#include "../src/app/Flag.h"

static UIImage* fc_scaled(UIImage* img, int maxSide) {
    CGSize s = img.size;
    if (s.width < 1 || s.height < 1) return nil;
    CGFloat k = MIN(1.0, maxSide / MAX(s.width, s.height));
    CGSize d = CGSizeMake(MAX(1, floor(s.width * k)), MAX(1, floor(s.height * k)));
    // opaque white background (transparent PNG parts become white); also applies the photo's rotation
    UIGraphicsBeginImageContextWithOptions(d, YES, 1.0);
    [[UIColor whiteColor] setFill];
    UIRectFill(CGRectMake(0, 0, d.width, d.height));
    [img drawInRect:CGRectMake(0, 0, d.width, d.height)];
    UIImage* out = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    return out;
}

unsigned char* fc_flag_decode(const char* path, int maxSide, int* w, int* h) {
    @autoreleasepool {
        UIImage* img = [UIImage imageWithContentsOfFile:[NSString stringWithUTF8String:path]];
        if (!img) return NULL;
        UIImage* s = fc_scaled(img, maxSide);
        if (!s) return NULL;
        CGImageRef cg = s.CGImage;
        int W = (int)CGImageGetWidth(cg), H = (int)CGImageGetHeight(cg);
        unsigned char* px = (unsigned char*)calloc((size_t)W * H, 4);
        if (!px) return NULL;
        CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
        CGContextRef ctx = CGBitmapContextCreate(px, W, H, 8, W * 4, cs,
                                                 kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
        CGColorSpaceRelease(cs);
        if (!ctx) { free(px); return NULL; }
        CGContextDrawImage(ctx, CGRectMake(0, 0, W, H), cg);  // memory row 0 = top of the image
        CGContextRelease(ctx);
        *w = W;
        *h = H;
        return px;
    }
}

static int g_picked = 0;

@interface FCFlagPicker : NSObject <UIImagePickerControllerDelegate, UINavigationControllerDelegate>
@property (nonatomic, copy) NSString* savePath;
@end

@implementation FCFlagPicker
- (void)imagePickerController:(UIImagePickerController*)picker didFinishPickingMediaWithInfo:(NSDictionary*)info {
    UIImage* img = info[UIImagePickerControllerOriginalImage];
    if (img) {
        UIImage* s = fc_scaled(img, 256);
        NSData* png = s ? UIImagePNGRepresentation(s) : nil;
        if (png && [png writeToFile:self.savePath atomically:YES]) g_picked = 1;
    }
    [picker dismissViewControllerAnimated:YES completion:nil];
}
- (void)imagePickerControllerDidCancel:(UIImagePickerController*)picker {
    [picker dismissViewControllerAnimated:YES completion:nil];
}
@end

static FCFlagPicker* g_delegate = nil;  // keeps the delegate alive

int fc_flag_pick(const char* savePath) {
    if (![UIImagePickerController isSourceTypeAvailable:UIImagePickerControllerSourceTypePhotoLibrary]) return 0;
    UIViewController* root = [UIApplication sharedApplication].keyWindow.rootViewController;
    while (root.presentedViewController) root = root.presentedViewController;
    if (!root) return 0;
    if (!g_delegate) g_delegate = [[FCFlagPicker alloc] init];
    g_delegate.savePath = [NSString stringWithUTF8String:savePath];
    UIImagePickerController* picker = [[UIImagePickerController alloc] init];
    picker.sourceType = UIImagePickerControllerSourceTypePhotoLibrary;
    picker.delegate = g_delegate;
    // on iPad the photo library must be shown in a popover
    picker.modalPresentationStyle = UIModalPresentationPopover;
    UIPopoverPresentationController* pop = picker.popoverPresentationController;
    pop.sourceView = root.view;
    CGRect b = root.view.bounds;
    pop.sourceRect = CGRectMake(CGRectGetMidX(b), CGRectGetMidY(b), 1, 1);
    pop.permittedArrowDirections = 0;
    [root presentViewController:picker animated:YES completion:nil];
    return 1;
}

int fc_flag_picked(void) {
    int r = g_picked;
    g_picked = 0;
    return r;
}
