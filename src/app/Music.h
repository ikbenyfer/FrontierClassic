// Background music. iOS: AVAudioPlayer (ios/music.m). Desktop: silent stub.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Loads the song (looping). Returns 1 on success.
int fc_music_load(const char* path);
// 1 = play/resume, 0 = pause.
void fc_music_set_playing(int playing);

#ifdef __cplusplus
}
#endif
