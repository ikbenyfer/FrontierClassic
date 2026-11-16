// Small iOS-only helpers for the Linux (Theos) toolchain.
#import <Foundation/Foundation.h>
#include <stdint.h>

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

// SDL 2.0.22 calls this even when HIDAPI is disabled. We use no Bluetooth: do nothing.
void hid_ble_scan(int active) { (void)active; }
