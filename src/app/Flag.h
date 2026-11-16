// Player flag image. iOS: ios/flag.m (UIImage + photo picker). Desktop: stub.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Decodes a PNG/JPEG into RGBA (row 0 = top, transparency on white),
// scaled to fit within maxSide. Returns malloc'ed pixels or NULL; free with free().
unsigned char* fc_flag_decode(const char* path, int maxSide, int* w, int* h);

// Opens the photo library picker. The chosen image is saved as PNG to savePath.
// Returns 0 when there is no picker on this platform.
int fc_flag_pick(const char* savePath);

// Returns 1 once after the picker saved a new flag.
int fc_flag_picked(void);

#ifdef __cplusplus
}
#endif
