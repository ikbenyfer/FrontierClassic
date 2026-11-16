
/* ---- Frontier Classic: appended to SDL_config_iphoneos.h by the CI build ----
 * Only video/events/timers/threads/files are used. Everything else is off, so
 * nothing in the app touches audio, controllers, Bluetooth, sensors, Metal or
 * the network. */
#undef SDL_AUDIO_DRIVER_COREAUDIO
#undef SDL_AUDIO_DRIVER_DUMMY
#undef SDL_AUDIO_DRIVER_DISK
#ifndef SDL_AUDIO_DISABLED
#define SDL_AUDIO_DISABLED 1
#endif
#undef SDL_JOYSTICK_MFI
#undef SDL_JOYSTICK_VIRTUAL
#undef SDL_JOYSTICK_HIDAPI
#ifndef SDL_JOYSTICK_DISABLED
#define SDL_JOYSTICK_DISABLED 1
#endif
#ifndef SDL_HIDAPI_DISABLED
#define SDL_HIDAPI_DISABLED 1
#endif
#undef SDL_HAPTIC_DUMMY
#ifndef SDL_HAPTIC_DISABLED
#define SDL_HAPTIC_DISABLED 1
#endif
#undef SDL_SENSOR_COREMOTION
#undef SDL_SENSOR_DUMMY
#ifndef SDL_SENSOR_DISABLED
#define SDL_SENSOR_DISABLED 1
#endif
#undef SDL_VIDEO_RENDER_METAL
#undef SDL_VIDEO_RENDER_OGL_ES
#undef SDL_VIDEO_RENDER_OGL_ES2
#undef SDL_VIDEO_VULKAN
#undef SDL_VIDEO_METAL
#undef SDL_PLATFORM_SUPPORTS_METAL
#define SDL_PLATFORM_SUPPORTS_METAL 0
