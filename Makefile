# Theos makefile - Frontier Classic (iPad 2, iOS 9.3, armv7)
# iphone:clang:<SDK>:<minimum iOS>. SDK 14.5 still contains armv7.
TARGET := iphone:clang:14.5:9.3
ARCHS := armv7

# SDL 2.0.22 source, unpacked by the CI workflow (see .github/workflows/build.yml)
SDL_DIR ?= third_party/SDL2

include $(THEOS)/makefiles/common.mk

APPLICATION_NAME = FrontierClassic

# Only the SDL parts this game needs: video (UIKit + OpenGL ES), events, timers,
# threads, files. No audio, joystick, haptics, sensors, Metal or Vulkan.
SDL_SRC := $(wildcard $(SDL_DIR)/src/*.c) \
	$(wildcard $(SDL_DIR)/src/atomic/*.c) \
	$(wildcard $(SDL_DIR)/src/audio/*.c) \
	$(wildcard $(SDL_DIR)/src/cpuinfo/*.c) \
	$(wildcard $(SDL_DIR)/src/dynapi/*.c) \
	$(wildcard $(SDL_DIR)/src/events/*.c) \
	$(wildcard $(SDL_DIR)/src/file/*.c) \
	$(wildcard $(SDL_DIR)/src/file/cocoa/*.m) \
	$(wildcard $(SDL_DIR)/src/filesystem/cocoa/*.m) \
	$(wildcard $(SDL_DIR)/src/haptic/*.c) \
	$(wildcard $(SDL_DIR)/src/haptic/dummy/*.c) \
	$(SDL_DIR)/src/hidapi/SDL_hidapi.c \
	$(wildcard $(SDL_DIR)/src/joystick/*.c) \
	$(wildcard $(SDL_DIR)/src/joystick/dummy/*.c) \
	$(wildcard $(SDL_DIR)/src/libm/*.c) \
	$(wildcard $(SDL_DIR)/src/loadso/dlopen/*.c) \
	$(wildcard $(SDL_DIR)/src/locale/*.c) \
	$(wildcard $(SDL_DIR)/src/locale/macosx/*.m) \
	$(wildcard $(SDL_DIR)/src/main/uikit/*.c) \
	$(wildcard $(SDL_DIR)/src/misc/*.c) \
	$(wildcard $(SDL_DIR)/src/misc/dummy/*.c) \
	$(wildcard $(SDL_DIR)/src/power/*.c) \
	$(wildcard $(SDL_DIR)/src/power/uikit/*.m) \
	$(wildcard $(SDL_DIR)/src/render/*.c) \
	$(wildcard $(SDL_DIR)/src/render/software/*.c) \
	$(wildcard $(SDL_DIR)/src/sensor/*.c) \
	$(wildcard $(SDL_DIR)/src/sensor/dummy/*.c) \
	$(wildcard $(SDL_DIR)/src/stdlib/*.c) \
	$(wildcard $(SDL_DIR)/src/thread/*.c) \
	$(wildcard $(SDL_DIR)/src/thread/pthread/*.c) \
	$(wildcard $(SDL_DIR)/src/timer/*.c) \
	$(wildcard $(SDL_DIR)/src/timer/unix/*.c) \
	$(wildcard $(SDL_DIR)/src/video/*.c) \
	$(wildcard $(SDL_DIR)/src/video/dummy/*.c) \
	$(wildcard $(SDL_DIR)/src/video/yuv2rgb/*.c) \
	$(wildcard $(SDL_DIR)/src/video/uikit/*.m)
SDL_SRC := $(filter-out %SDL_uikitmetalview.m %SDL_uikitvulkan.m,$(SDL_SRC))

FrontierClassic_FILES = $(wildcard src/core/*.cpp) $(wildcard src/net/*.cpp) $(wildcard src/app/*.cpp) ios/ios_shims.m ios/music.m ios/flag.m $(SDL_SRC)
FrontierClassic_FRAMEWORKS = UIKit Foundation CoreFoundation CoreGraphics QuartzCore OpenGLES AVFoundation
FrontierClassic_WEAK_FRAMEWORKS = GameController
FrontierClassic_CFLAGS = -O2 -I$(SDL_DIR)/include -DGLES_SILENCE_DEPRECATION -Wno-error -Wno-deprecated-declarations -Wno-unused-parameter
FrontierClassic_CCFLAGS = -std=c++11
FrontierClassic_OBJCFLAGS = -fobjc-arc
FrontierClassic_LDFLAGS = -lc++

include $(THEOS_MAKE_PATH)/application.mk
