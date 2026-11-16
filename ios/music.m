// Background music on iOS with AVAudioPlayer (MP3 is decoded in hardware on the A5).
#import <AVFoundation/AVFoundation.h>
#include "../src/app/Music.h"

static AVAudioPlayer* g_player = nil;

int fc_music_load(const char* path) {
    @autoreleasepool {
        // "Ambient": follows the mute switch and lets other audio keep playing.
        [[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryAmbient error:nil];
        [[AVAudioSession sharedInstance] setActive:YES error:nil];
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];
        NSError* err = nil;
        g_player = [[AVAudioPlayer alloc] initWithContentsOfURL:url error:&err];
        if (!g_player) {
            NSLog(@"music: %@", err);
            return 0;
        }
        g_player.numberOfLoops = -1;  // repeat forever
        g_player.volume = 0.7f;
        [g_player prepareToPlay];
        return 1;
    }
}

void fc_music_set_playing(int playing) {
    if (!g_player) return;
    if (playing && !g_player.playing) [g_player play];
    else if (!playing && g_player.playing) [g_player pause];
}
