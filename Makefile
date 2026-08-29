TARGET_NAME := johnny_castaway
platform ?= unix
BUILD_DIR := build/$(platform)

SOURCES := src/jc_ads.c src/jc_audio.c src/jc_bmp.c src/jc_caption_render.c \
           src/jc_captions.c \
           src/jc_chapters.c src/jc_compositor.c src/jc_content.c src/jc_core.c \
           src/jc_decompress.c src/jc_director.c src/jc_extras.c src/jc_fade.c \
           src/jc_holiday_overlay.c src/jc_island_walk.c \
           src/jc_ocean.c src/jc_palette.c src/jc_path.c \
           src/jc_resource_map.c src/jc_rng.c src/jc_runtime.c \
           src/jc_scr.c src/jc_script_vm.c src/jc_sfx.c src/jc_story_player.c \
           src/jc_surface.c src/jc_ttm.c \
           src/jc_ttm_renderer.c src/jc_vag.c src/jc_walk.c src/jc_wav.c \
           src/libretro_core.c
OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SOURCES))
INCLUDES := -Iinclude -Iexternal/libretro-common/include
WARNINGS := -Wall -Wextra -Wpedantic
COMMON_CFLAGS := -std=c99 $(WARNINGS) $(INCLUDES) -fvisibility=hidden
OPTFLAGS ?= -O2
STATIC_LINKING := 0

ifeq ($(platform),unix)
  detected_os := $(shell uname -s)
  ifeq ($(detected_os),Darwin)
    override platform := osx
  else
    override platform := linux_x86_64
  endif
  BUILD_DIR := build/$(platform)
  OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SOURCES))
endif

ifeq ($(platform),linux_x86_64)
  CC := gcc
  TARGET := $(BUILD_DIR)/$(TARGET_NAME)_libretro.so
  PLATFORM_CFLAGS := -fPIC
  SHARED := -shared
else ifeq ($(platform),linux_x86)
  CC := gcc
  TARGET := $(BUILD_DIR)/$(TARGET_NAME)_libretro.so
  PLATFORM_CFLAGS := -fPIC -m32
  LDFLAGS += -m32
  SHARED := -shared
else ifeq ($(platform),linux_aarch64)
  CC := aarch64-linux-gnu-gcc
  TARGET := $(BUILD_DIR)/$(TARGET_NAME)_libretro.so
  PLATFORM_CFLAGS := -fPIC
  SHARED := -shared
else ifeq ($(platform),linux_armv7)
  CC := arm-linux-gnueabihf-gcc
  TARGET := $(BUILD_DIR)/$(TARGET_NAME)_libretro.so
  PLATFORM_CFLAGS := -fPIC -march=armv7-a -mfpu=vfpv3-d16
  SHARED := -shared
else ifeq ($(platform),linux_armv7_neon)
  CC := arm-linux-gnueabihf-gcc
  TARGET := $(BUILD_DIR)/$(TARGET_NAME)_libretro.so
  PLATFORM_CFLAGS := -fPIC -march=armv7-a -mfpu=neon
  SHARED := -shared
else ifeq ($(platform),mingw_x86_64)
  CC := x86_64-w64-mingw32-gcc
  TARGET := $(BUILD_DIR)/$(TARGET_NAME)_libretro.dll
  PLATFORM_CFLAGS := -D_WIN32_WINNT=0x0601
  SHARED := -shared -static-libgcc
else ifeq ($(platform),mingw_x86)
  CC := i686-w64-mingw32-gcc
  TARGET := $(BUILD_DIR)/$(TARGET_NAME)_libretro.dll
  PLATFORM_CFLAGS := -D_WIN32_WINNT=0x0601
  SHARED := -shared -static-libgcc
else ifneq (,$(filter $(platform),osx osx_x86_64 osx_arm64))
  CC := clang
  TARGET := $(BUILD_DIR)/$(TARGET_NAME)_libretro.dylib
  PLATFORM_CFLAGS := -fPIC
  SHARED := -dynamiclib
  ifeq ($(platform),osx_x86_64)
    APPLE_SDKROOT ?= $(shell xcrun --sdk macosx --show-sdk-path 2>/dev/null)
    ifeq ($(strip $(APPLE_SDKROOT)),)
      $(error xcrun could not locate the macOS SDK; build osx_x86_64 on macOS with Xcode command-line tools)
    endif
    CC := xcrun --sdk macosx clang
    PLATFORM_CFLAGS += -arch x86_64 -isysroot $(APPLE_SDKROOT)
    LDFLAGS += -arch x86_64 -isysroot $(APPLE_SDKROOT)
  else ifeq ($(platform),osx_arm64)
    APPLE_SDKROOT ?= $(shell xcrun --sdk macosx --show-sdk-path 2>/dev/null)
    ifeq ($(strip $(APPLE_SDKROOT)),)
      $(error xcrun could not locate the macOS SDK; build osx_arm64 on macOS with Xcode command-line tools)
    endif
    CC := xcrun --sdk macosx clang
    PLATFORM_CFLAGS += -arch arm64 -isysroot $(APPLE_SDKROOT)
    LDFLAGS += -arch arm64 -isysroot $(APPLE_SDKROOT)
  endif
else ifneq (,$(filter $(platform),ios_arm64 ios_sim_arm64 ios_sim_x86_64))
  IOS_DEPLOYMENT_TARGET ?= 12.0
  ifeq ($(platform),ios_arm64)
    APPLE_SDK := iphoneos
    APPLE_ARCH := arm64
    APPLE_MIN_FLAG := -miphoneos-version-min=$(IOS_DEPLOYMENT_TARGET)
    TARGET := $(BUILD_DIR)/$(TARGET_NAME)_libretro_ios.dylib
  else
    APPLE_SDK := iphonesimulator
    APPLE_MIN_FLAG := -mios-simulator-version-min=$(IOS_DEPLOYMENT_TARGET)
    ifeq ($(platform),ios_sim_arm64)
      APPLE_ARCH := arm64
    else
      APPLE_ARCH := x86_64
    endif
    TARGET := $(BUILD_DIR)/$(TARGET_NAME)_libretro_ios_sim_$(APPLE_ARCH).dylib
  endif
  APPLE_SDKROOT ?= $(shell xcrun --sdk $(APPLE_SDK) --show-sdk-path 2>/dev/null)
  ifeq ($(strip $(APPLE_SDKROOT)),)
    $(error xcrun could not locate the $(APPLE_SDK) SDK; build $(platform) on macOS with Xcode)
  endif
  CC := xcrun --sdk $(APPLE_SDK) clang
  TARGET_FLAGS := -arch $(APPLE_ARCH) -isysroot $(APPLE_SDKROOT) $(APPLE_MIN_FLAG)
  PLATFORM_CFLAGS := -fPIC -DIOS=1 $(TARGET_FLAGS)
  LDFLAGS += $(TARGET_FLAGS)
  SHARED := -dynamiclib
else ifneq (,$(filter $(platform),tvos_arm64 tvos_sim_arm64 tvos_sim_x86_64))
  TVOS_DEPLOYMENT_TARGET ?= 12.0
  ifeq ($(platform),tvos_arm64)
    APPLE_SDK := appletvos
    APPLE_ARCH := arm64
    APPLE_MIN_FLAG := -mappletvos-version-min=$(TVOS_DEPLOYMENT_TARGET)
    TARGET := $(BUILD_DIR)/$(TARGET_NAME)_libretro_tvos.dylib
  else
    APPLE_SDK := appletvsimulator
    APPLE_MIN_FLAG := -mtvos-simulator-version-min=$(TVOS_DEPLOYMENT_TARGET)
    ifeq ($(platform),tvos_sim_arm64)
      APPLE_ARCH := arm64
    else
      APPLE_ARCH := x86_64
    endif
    TARGET := $(BUILD_DIR)/$(TARGET_NAME)_libretro_tvos_sim_$(APPLE_ARCH).dylib
  endif
  APPLE_SDKROOT ?= $(shell xcrun --sdk $(APPLE_SDK) --show-sdk-path 2>/dev/null)
  ifeq ($(strip $(APPLE_SDKROOT)),)
    $(error xcrun could not locate the $(APPLE_SDK) SDK; build $(platform) on macOS with Xcode)
  endif
  CC := xcrun --sdk $(APPLE_SDK) clang
  TARGET_FLAGS := -arch $(APPLE_ARCH) -isysroot $(APPLE_SDKROOT) $(APPLE_MIN_FLAG)
  PLATFORM_CFLAGS := -fPIC -DIOS=1 -DTVOS=1 $(TARGET_FLAGS)
  LDFLAGS += $(TARGET_FLAGS)
  SHARED := -dynamiclib
else ifeq ($(platform),emscripten)
  CC := emcc
  AR := emar
  TARGET := $(BUILD_DIR)/$(TARGET_NAME)_libretro_emscripten.bc
  PLATFORM_CFLAGS := -DEMSCRIPTEN
  STATIC_LINKING := 1
else ifneq (,$(filter $(platform),android_arm64 android_armv7 android_x86_64 android_x86))
  ANDROID_NDK_HOME ?= $(ANDROID_NDK_ROOT)
  ANDROID_API ?= 21
  detected_host := $(shell uname -s)
  ifeq ($(detected_host),Darwin)
    ANDROID_NDK_HOST_TAG ?= darwin-x86_64
  else
    ANDROID_NDK_HOST_TAG ?= linux-x86_64
  endif
  ifeq ($(strip $(ANDROID_NDK_HOME)),)
    $(error Set ANDROID_NDK_HOME or ANDROID_NDK_ROOT for $(platform))
  endif
  ANDROID_TOOLCHAIN := $(ANDROID_NDK_HOME)/toolchains/llvm/prebuilt/$(ANDROID_NDK_HOST_TAG)/bin
  ifeq ($(platform),android_arm64)
    ANDROID_TRIPLE := aarch64-linux-android
  else ifeq ($(platform),android_armv7)
    ANDROID_TRIPLE := armv7a-linux-androideabi
  else ifeq ($(platform),android_x86_64)
    ANDROID_TRIPLE := x86_64-linux-android
  else
    ANDROID_TRIPLE := i686-linux-android
  endif
  CC := $(ANDROID_TOOLCHAIN)/$(ANDROID_TRIPLE)$(ANDROID_API)-clang
  AR := $(ANDROID_TOOLCHAIN)/llvm-ar
  TARGET := $(BUILD_DIR)/$(TARGET_NAME)_libretro.so
  PLATFORM_CFLAGS := -fPIC -DANDROID
  SHARED := -shared
else ifeq ($(platform),psp1)
  CC := psp-gcc
  AR := psp-ar
  TARGET := $(BUILD_DIR)/$(TARGET_NAME)_libretro_psp1.a
  PLATFORM_CFLAGS := -G0 -DPSP
  STATIC_LINKING := 1
else ifeq ($(platform),vita)
  CC := arm-vita-eabi-gcc
  AR := arm-vita-eabi-ar
  TARGET := $(BUILD_DIR)/$(TARGET_NAME)_libretro_vita.a
  PLATFORM_CFLAGS := -DVITA
  STATIC_LINKING := 1
else ifeq ($(platform),ctr)
  CC := $(DEVKITARM)/bin/arm-none-eabi-gcc
  AR := $(DEVKITARM)/bin/arm-none-eabi-ar
  TARGET := $(BUILD_DIR)/$(TARGET_NAME)_libretro_ctr.a
  PLATFORM_CFLAGS := -D_3DS -DARM11 -march=armv6k -mtune=mpcore -mfloat-abi=hard
  STATIC_LINKING := 1
else ifneq (,$(filter $(platform),ngc wii wiiu))
  ifeq ($(strip $(DEVKITPPC)),)
    $(error Set DEVKITPPC for $(platform))
  endif
  CC := $(DEVKITPPC)/bin/powerpc-eabi-gcc
  AR := $(DEVKITPPC)/bin/powerpc-eabi-ar
  TARGET := $(BUILD_DIR)/$(TARGET_NAME)_libretro_$(platform).a
  PLATFORM_CFLAGS := -DGEKKO -mcpu=750 -meabi -mhard-float \
                     -ffunction-sections -fdata-sections -DMSB_FIRST
  ifeq ($(platform),ngc)
    PLATFORM_CFLAGS += -DHW_DOL -mrvl
  else ifeq ($(platform),wii)
    PLATFORM_CFLAGS += -DHW_RVL -mrvl
  else
    PLATFORM_CFLAGS += -DWIIU -DHW_RVL -D__wiiu__ -D__wut__
  endif
  STATIC_LINKING := 1
else ifeq ($(platform),libnx)
  ifeq ($(strip $(DEVKITPRO)),)
    $(error Set DEVKITPRO for libnx)
  endif
  DEVKITA64 ?= $(DEVKITPRO)/devkitA64
  LIBNX ?= $(DEVKITPRO)/libnx
  CC := $(DEVKITA64)/bin/aarch64-none-elf-gcc
  AR := $(DEVKITA64)/bin/aarch64-none-elf-ar
  TARGET := $(BUILD_DIR)/$(TARGET_NAME)_libretro_libnx.a
  PLATFORM_CFLAGS := -D__SWITCH__=1 -DSWITCH=1 -DHAVE_LIBNX=1 \
                     -U__linux__ -U__linux -fPIE -ffunction-sections \
                     -fdata-sections -march=armv8-a -mtune=cortex-a57 \
                     -mtp=soft -I$(LIBNX)/include -specs=$(LIBNX)/switch.specs
  STATIC_LINKING := 1
else ifeq ($(platform),ps2)
  CC := mips64r5900el-ps2-elf-gcc
  AR := mips64r5900el-ps2-elf-ar
  TARGET := $(BUILD_DIR)/$(TARGET_NAME)_libretro_ps2.a
  PLATFORM_CFLAGS := -DPS2 -G0
  STATIC_LINKING := 1
else
  $(error Unsupported platform '$(platform)'; see docs/PORTING_PLAN.md)
endif

override CFLAGS := $(OPTFLAGS) $(COMMON_CFLAGS) $(PLATFORM_CFLAGS) $(CFLAGS)

all: $(TARGET)

ARFLAGS := rcs

$(TARGET): $(OBJECTS)
	@mkdir -p $(dir $@)
ifeq ($(STATIC_LINKING),1)
	$(AR) $(ARFLAGS) $@ $(OBJECTS)
else
	$(CC) $(SHARED) -o $@ $(OBJECTS) $(LDFLAGS) $(LDLIBS)
endif

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

TEST_TARGET := build/tests/test_core
MAP_TEST_TARGET := build/tests/test_resource_map
GRAPHICS_TEST_TARGET := build/tests/test_graphics
BMP_TEST_TARGET := build/tests/test_bmp_compositor
DIRECTOR_TEST_TARGET := build/tests/test_director
STORY_PLAYER_TEST_TARGET := build/tests/test_story_player
FADE_TEST_TARGET := build/tests/test_fade
ISLAND_WALK_TEST_TARGET := build/tests/test_island_walk
PATH_TEST_TARGET := build/tests/test_path
WALK_TEST_TARGET := build/tests/test_walk
AUDIO_TEST_TARGET := build/tests/test_audio
SFX_TEST_TARGET := build/tests/test_sfx
SCRIPT_TEST_TARGET := build/tests/test_script_vm
EXTRAS_TEST_TARGET := build/tests/test_extras
CAPTION_RENDER_TEST_TARGET := build/tests/test_caption_render
HOLIDAY_OVERLAY_TEST_TARGET := build/tests/test_holiday_overlay
TTM_RENDERER_TEST_TARGET := build/tests/test_ttm_renderer
VAG_TEST_TARGET := build/tests/test_vag
OCEAN_TEST_TARGET := build/tests/test_ocean
RUNTIME_TEST_TARGET := build/tests/test_runtime
LIBRETRO_TEST_TARGET := build/tests/test_libretro
$(TEST_TARGET): src/jc_core.c tests/test_core.c include/jc_core.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c99 $(WARNINGS) -Iinclude -O2 -o $@ src/jc_core.c tests/test_core.c

$(MAP_TEST_TARGET): src/jc_resource_map.c tests/test_resource_map.c include/jc_resource_map.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c99 $(WARNINGS) -Iinclude -O2 -o $@ src/jc_resource_map.c tests/test_resource_map.c

$(GRAPHICS_TEST_TARGET): src/jc_decompress.c src/jc_scr.c src/jc_surface.c tests/test_graphics.c
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c99 $(WARNINGS) -Iinclude -O2 -o $@ \
		src/jc_decompress.c src/jc_scr.c src/jc_surface.c tests/test_graphics.c

$(BMP_TEST_TARGET): src/jc_bmp.c src/jc_compositor.c src/jc_decompress.c \
                    src/jc_surface.c tests/test_bmp_compositor.c
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c99 $(WARNINGS) -Iinclude -O2 -o $@ \
		src/jc_bmp.c src/jc_compositor.c src/jc_decompress.c \
		src/jc_surface.c tests/test_bmp_compositor.c

$(DIRECTOR_TEST_TARGET): src/jc_director.c src/jc_rng.c \
                         src/jc_story_data.inc tests/test_director.c
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c99 $(WARNINGS) -Iinclude -Isrc -O2 -o $@ \
		src/jc_director.c src/jc_rng.c tests/test_director.c

$(STORY_PLAYER_TEST_TARGET): src/jc_chapters.c src/jc_director.c src/jc_rng.c \
                             src/jc_story_player.c src/jc_story_data.inc \
                             tests/test_story_player.c
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c99 $(WARNINGS) -Iinclude -Isrc -O2 -o $@ \
		src/jc_chapters.c src/jc_director.c src/jc_rng.c \
		src/jc_story_player.c tests/test_story_player.c

$(FADE_TEST_TARGET): src/jc_fade.c tests/test_fade.c include/jc_fade.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c99 $(WARNINGS) -Iinclude -O2 -o $@ \
		src/jc_fade.c tests/test_fade.c

$(ISLAND_WALK_TEST_TARGET): src/jc_compositor.c src/jc_island_walk.c \
                            src/jc_path.c src/jc_path_data.inc src/jc_rng.c \
                            src/jc_surface.c src/jc_walk.c \
                            src/jc_walk_data.inc tests/test_island_walk.c
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c99 $(WARNINGS) -Iinclude -Isrc -O2 -o $@ \
		src/jc_compositor.c src/jc_island_walk.c src/jc_path.c src/jc_rng.c \
		src/jc_surface.c src/jc_walk.c tests/test_island_walk.c

$(PATH_TEST_TARGET): src/jc_path.c src/jc_path_data.inc src/jc_rng.c \
                     tests/test_path.c
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c99 $(WARNINGS) -Iinclude -Isrc -O2 -o $@ \
		src/jc_path.c src/jc_rng.c tests/test_path.c

$(WALK_TEST_TARGET): src/jc_path.c src/jc_path_data.inc src/jc_rng.c \
                     src/jc_walk.c src/jc_walk_data.inc tests/test_walk.c
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c99 $(WARNINGS) -Iinclude -Isrc -O2 -o $@ \
		src/jc_path.c src/jc_rng.c src/jc_walk.c tests/test_walk.c

$(AUDIO_TEST_TARGET): src/jc_audio.c src/jc_wav.c tests/test_audio.c
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c99 $(WARNINGS) -Iinclude -O2 -o $@ \
		src/jc_audio.c src/jc_wav.c tests/test_audio.c

$(SFX_TEST_TARGET): src/jc_audio.c src/jc_sfx.c src/jc_wav.c \
                    tests/test_sfx.c include/jc_sfx.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c99 $(WARNINGS) -Iinclude \
		-Iexternal/libretro-common/include -O2 -o $@ \
		src/jc_audio.c src/jc_sfx.c src/jc_wav.c tests/test_sfx.c

$(SCRIPT_TEST_TARGET): src/jc_ads.c src/jc_decompress.c src/jc_script_vm.c \
                       src/jc_ttm.c tests/test_script_vm.c
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c99 $(WARNINGS) -Iinclude -O2 -o $@ \
		src/jc_ads.c src/jc_decompress.c src/jc_script_vm.c src/jc_ttm.c \
		tests/test_script_vm.c

$(EXTRAS_TEST_TARGET): src/jc_captions.c src/jc_chapters.c src/jc_extras.c \
                       tests/test_extras.c
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c99 $(WARNINGS) -Iinclude -O2 -o $@ \
		src/jc_captions.c src/jc_chapters.c src/jc_extras.c tests/test_extras.c

$(CAPTION_RENDER_TEST_TARGET): src/jc_caption_render.c src/jc_captions.c \
                               tests/test_caption_render.c
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c99 $(WARNINGS) -Iinclude -O2 -o $@ \
		src/jc_caption_render.c src/jc_captions.c tests/test_caption_render.c

$(HOLIDAY_OVERLAY_TEST_TARGET): src/jc_bmp.c src/jc_decompress.c \
                                src/jc_extras.c src/jc_holiday_overlay.c \
                                src/jc_surface.c \
                                tests/test_holiday_overlay.c
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c99 $(WARNINGS) -Iinclude -O2 -o $@ \
		src/jc_bmp.c src/jc_decompress.c src/jc_extras.c \
		src/jc_holiday_overlay.c src/jc_surface.c \
		tests/test_holiday_overlay.c

$(TTM_RENDERER_TEST_TARGET): src/jc_ads.c src/jc_bmp.c src/jc_compositor.c \
                             src/jc_decompress.c src/jc_palette.c \
                             src/jc_scr.c src/jc_script_vm.c src/jc_surface.c \
                             src/jc_ttm.c src/jc_ttm_renderer.c \
                             tests/test_ttm_renderer.c
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c99 $(WARNINGS) -Iinclude -O2 -o $@ \
		src/jc_ads.c src/jc_bmp.c src/jc_compositor.c src/jc_decompress.c src/jc_palette.c \
		src/jc_scr.c src/jc_script_vm.c src/jc_surface.c src/jc_ttm.c \
		src/jc_ttm_renderer.c tests/test_ttm_renderer.c

$(VAG_TEST_TARGET): src/jc_vag.c tests/test_vag.c include/jc_vag.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c99 $(WARNINGS) -Iinclude -O2 -o $@ \
		src/jc_vag.c tests/test_vag.c

$(OCEAN_TEST_TARGET): src/jc_ocean.c src/jc_ocean_vag.inc src/jc_vag.c \
                      tests/test_ocean.c include/jc_ocean.h include/jc_vag.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c99 $(WARNINGS) -Iinclude -Isrc -O2 -o $@ \
		src/jc_ocean.c src/jc_vag.c tests/test_ocean.c

$(RUNTIME_TEST_TARGET): src/jc_ads.c src/jc_bmp.c src/jc_compositor.c \
                        src/jc_content.c src/jc_decompress.c src/jc_palette.c \
                        src/jc_resource_map.c src/jc_runtime.c src/jc_scr.c \
                        src/jc_script_vm.c src/jc_surface.c src/jc_ttm.c \
                        src/jc_ttm_renderer.c tests/test_runtime.c
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c99 $(WARNINGS) -Iinclude \
		-Iexternal/libretro-common/include -O2 -o $@ \
		src/jc_ads.c src/jc_bmp.c src/jc_compositor.c src/jc_content.c \
		src/jc_decompress.c src/jc_palette.c src/jc_resource_map.c \
		src/jc_runtime.c src/jc_scr.c src/jc_script_vm.c src/jc_surface.c \
		src/jc_ttm.c src/jc_ttm_renderer.c tests/test_runtime.c

$(LIBRETRO_TEST_TARGET): $(SOURCES) tests/test_libretro.c
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c99 $(WARNINGS) -DJC_LIBRETRO_TEST -Iinclude \
		-Iexternal/libretro-common/include -O2 -o $@ $(SOURCES) tests/test_libretro.c

HOST_CC ?= cc
test: $(TEST_TARGET) $(MAP_TEST_TARGET) $(GRAPHICS_TEST_TARGET) \
      $(BMP_TEST_TARGET) $(DIRECTOR_TEST_TARGET) $(PATH_TEST_TARGET) \
      $(STORY_PLAYER_TEST_TARGET) $(FADE_TEST_TARGET) \
      $(ISLAND_WALK_TEST_TARGET) \
      $(WALK_TEST_TARGET) $(AUDIO_TEST_TARGET) $(SFX_TEST_TARGET) \
      $(SCRIPT_TEST_TARGET) \
      $(EXTRAS_TEST_TARGET) $(CAPTION_RENDER_TEST_TARGET) \
      $(HOLIDAY_OVERLAY_TEST_TARGET) \
      $(TTM_RENDERER_TEST_TARGET) \
      $(VAG_TEST_TARGET) $(OCEAN_TEST_TARGET) $(RUNTIME_TEST_TARGET) \
      $(LIBRETRO_TEST_TARGET)
	./$(TEST_TARGET)
	./$(MAP_TEST_TARGET)
	./$(GRAPHICS_TEST_TARGET)
	./$(BMP_TEST_TARGET)
	./$(DIRECTOR_TEST_TARGET)
	./$(STORY_PLAYER_TEST_TARGET)
	./$(FADE_TEST_TARGET)
	./$(ISLAND_WALK_TEST_TARGET)
	./$(PATH_TEST_TARGET)
	./$(WALK_TEST_TARGET)
	./$(AUDIO_TEST_TARGET)
	./$(SFX_TEST_TARGET)
	./$(SCRIPT_TEST_TARGET)
	./$(EXTRAS_TEST_TARGET)
	./$(CAPTION_RENDER_TEST_TARGET)
	./$(HOLIDAY_OVERLAY_TEST_TARGET)
	./$(TTM_RENDERER_TEST_TARGET)
	./$(VAG_TEST_TARGET)
	./$(OCEAN_TEST_TARGET)
	./$(RUNTIME_TEST_TARGET)
	./$(LIBRETRO_TEST_TARGET)
	python3 tools/test_release_assembler.py

INSPECT_TARGET := build/tools/jc_inspect
$(INSPECT_TARGET): src/jc_content.c src/jc_resource_map.c tools/jc_inspect.c
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c99 $(WARNINGS) -Iinclude \
		-Iexternal/libretro-common/include -O2 -o $@ \
		src/jc_content.c src/jc_resource_map.c tools/jc_inspect.c

inspect: $(INSPECT_TARGET)

AUTHENTIC_TEST_TARGET := build/tools/check_all_chapters
AUTHENTIC_VISUAL_TEST_TARGET := build/tools/check_all_chapter_visuals
$(AUTHENTIC_TEST_TARGET): src/jc_ads.c src/jc_bmp.c src/jc_chapters.c \
                            src/jc_compositor.c src/jc_content.c \
                            src/jc_decompress.c src/jc_palette.c \
                            src/jc_resource_map.c src/jc_runtime.c src/jc_scr.c \
                            src/jc_script_vm.c src/jc_surface.c src/jc_ttm.c \
                            src/jc_ttm_renderer.c tools/check_all_chapters.c
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c99 $(WARNINGS) -Iinclude \
		-Iexternal/libretro-common/include -O2 -o $@ \
		src/jc_ads.c src/jc_bmp.c src/jc_chapters.c src/jc_compositor.c \
		src/jc_content.c src/jc_decompress.c src/jc_palette.c \
		src/jc_resource_map.c src/jc_runtime.c src/jc_scr.c \
		src/jc_script_vm.c src/jc_surface.c src/jc_ttm.c \
		src/jc_ttm_renderer.c tools/check_all_chapters.c

$(AUTHENTIC_VISUAL_TEST_TARGET): $(SOURCES) \
                                    tools/check_all_chapter_visuals.c
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c99 $(WARNINGS) -DJC_LIBRETRO_TEST -Iinclude \
		-Iexternal/libretro-common/include -O2 -o $@ $(SOURCES) \
		tools/check_all_chapter_visuals.c

authentic-visual-test: $(AUTHENTIC_VISUAL_TEST_TARGET)
	@test -n "$(CONTENT)" || \
		{ echo "Set CONTENT=/path/to/RESOURCE.MAP" >&2; exit 2; }
	./$(AUTHENTIC_VISUAL_TEST_TARGET) "$(CONTENT)" --csv \
		build/scene-visual-results.csv

authentic-test: $(AUTHENTIC_TEST_TARGET) $(AUTHENTIC_VISUAL_TEST_TARGET)
	@test -n "$(CONTENT)" || \
		{ echo "Set CONTENT=/path/to/RESOURCE.MAP" >&2; exit 2; }
	./$(AUTHENTIC_TEST_TARGET) "$(CONTENT)" --sound-trace \
		build/scene-sound-events.csv
	./$(AUTHENTIC_VISUAL_TEST_TARGET) "$(CONTENT)" --csv \
		build/scene-visual-results.csv

SOUND_BANK_TEST_TARGET := build/tools/check_sound_bank
$(SOUND_BANK_TEST_TARGET): src/jc_audio.c src/jc_sfx.c src/jc_wav.c \
                          tools/check_sound_bank.c
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c99 $(WARNINGS) -Iinclude \
		-Iexternal/libretro-common/include -O2 -o $@ \
		src/jc_audio.c src/jc_sfx.c src/jc_wav.c tools/check_sound_bank.c

sound-bank-test: $(SOUND_BANK_TEST_TARGET)
	@test -n "$(CONTENT)" || \
		{ echo "Set CONTENT=/path/to/RESOURCE.MAP" >&2; exit 2; }
	./$(SOUND_BANK_TEST_TARGET) "$(CONTENT)"

clean:
	rm -rf build

-include $(OBJECTS:.o=.d)

.PHONY: all authentic-test authentic-visual-test clean inspect sound-bank-test test
