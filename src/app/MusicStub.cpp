// Desktop/test builds: no music (the iOS version is ios/music.m).
#include "SDL_platform.h"
#include "Music.h"

#if !defined(__IPHONEOS__)
int fc_music_load(const char* path) {
    (void)path;
    return 0;
}
void fc_music_set_playing(int playing) { (void)playing; }
#endif
