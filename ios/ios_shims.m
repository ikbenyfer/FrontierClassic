// Small iOS-only helpers for the Linux (Theos) toolchain.
#import <Foundation/Foundation.h>
#include <stdint.h>
#include <string.h>

// clang emits calls to this for `@available(iOS x, *)` checks (used inside SDL).
// It normally lives in compiler-rt, which this toolchain does not ship.
// NSProcessInfo.operatingSystemVersion exists since iOS 8.
int32_t __isOSVersionAtLeast(int32_t major, int32_t minor, int32_t subminor) {
    static NSOperatingSystemVersion v;
    static int loaded = 0;
    if (!loaded) {
        v = [[NSProcessInfo processInfo] operatingSystemVersion];
        loaded = 1;
    }
    if (v.majorVersion != major) return v.majorVersion > major;
    if (v.minorVersion != minor) return v.minorVersion > minor;
    return v.patchVersion >= subminor;
}

// Folder for save games. Uses Apple's own API for the app's Documents folder.
// - Installed as .ipa (AppSync): sandboxed, Documents is
//   /var/mobile/Containers/Data/Application/<UUID>/Documents (made by iOS,
//   backed up by iTunes/iCloud device backup, visible in iTunes file sharing).
// - Installed as .deb into /Applications: not sandboxed, NSDocumentDirectory is
//   the shared /var/mobile/Documents. Then use our own subfolder so we never
//   mix files with other apps.
// The folder is created if missing. Returns a malloc'ed path ending in '/'.
char* fc_ios_save_dir(void) {
    @autoreleasepool {
        NSArray* paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
        NSString* docs = paths.count ? paths[0] : [NSHomeDirectory() stringByAppendingPathComponent:@"Documents"];
        BOOL sandboxed = [docs rangeOfString:@"/Containers/Data/Application/"].location != NSNotFound;
        NSString* dir = sandboxed ? docs : [docs stringByAppendingPathComponent:@"FrontierClassic"];
        [[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
        return strdup([[dir stringByAppendingString:@"/"] fileSystemRepresentation]);
    }
}

// SDL 2.0.22 calls this even when HIDAPI is disabled. We use no Bluetooth: do nothing.
void hid_ble_scan(int active) { (void)active; }
