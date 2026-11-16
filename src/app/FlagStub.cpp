// Desktop/test builds: flags come only from App::setFlagPixels (the iOS version is ios/flag.m).
#include <stddef.h>
#include "SDL_platform.h"
#include "Flag.h"

#if !defined(__IPHONEOS__)
unsigned char* fc_flag_decode(const char* path, int maxSide, int* w, int* h) {
    (void)path; (void)maxSide; (void)w; (void)h;
    return NULL;
}
int fc_flag_pick(const char* savePath) {
    (void)savePath;
    return 0;
}
int fc_flag_picked(void) { return 0; }
#endif
