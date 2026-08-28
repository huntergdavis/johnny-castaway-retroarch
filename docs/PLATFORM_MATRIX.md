# RetroArch platform matrix

This inventory answers “which RetroArch platforms should this core target?” without
equating a Makefile branch with support. A target advances through **Core build**,
**Frontend link**, **Frontend run**, and **Regression** independently.

Snapshot reviewed on 2026-08-27:

- RetroArch `34c069f44f419b708c5362f96c54f959bc182dbe`
- libretro-super `bc2b5463fe7e45a36295db54f74bc56b950dec4b`
- libretro documentation `7a0833c074b31d5438dce6ef6a94eea7f5805ec6`
- live [libretro buildbot nightly index](https://buildbot.libretro.com/nightly/)

The project does not add a platform merely because RetroArch names it. A public
toolchain, a current core artifact contract, and a verifiable build are required.

## Current coverage

| RetroArch/buildbot family | Upstream core form | Johnny target(s) | Evidence now |
|---|---|---|---|
| Linux x86_64 | dynamic `.so` | `linux_x86_64` | Strict tests/build and exact ELF ABI pass; real RetroArch runs generated content in CI and authentic Automatic Story locally with non-blank 640x480 screenshot evidence |
| Linux AArch64 and ARM hard-float | dynamic `.so` | `linux_aarch64`, `linux_armv7` | Real GCC cross-builds, target-machine checks, VFPv3-D16 hard-float verification, and exact ELF ABI pass |
| Linux x86 and ARMv7 NEON | dynamic `.so` | `linux_x86`, `linux_armv7_neon` | Real strict builds, i386/NEONv1/VFP-register machine checks, and exact ELF ABI pass |
| Windows MinGW x64/x86 | dynamic `.dll` | `mingw_x86_64`, `mingw_x86` | Real cross-builds, PE machine checks, and exact exports pass |
| Windows MSVC desktop/UWP | dynamic `.dll` | not yet | Source recipes exist; modern UWP ARM64 naming and Visual Studio CI must be resolved first |
| Android arm64/armv7/x86_64/x86 | dynamic `.so` | all four `android_*` targets | Real NDK r29/API 21 strict builds and exact ABI checks pass |
| macOS | dynamic `.dylib` | x86_64, arm64, universal | Real Xcode CI builds, exact exports, and universal verification pass |
| iOS/tvOS | dynamic `.dylib` | arm64 device plus arm64/x86_64 simulators | Real Xcode CI builds and exact Mach-O checks pass |
| Emscripten | static `.bc` linked into frontend | `emscripten` | Pinned RetroArch JS/WASM build and real-Firefox synthetic plus authentic auto-load/menu runs pass |
| Nintendo Switch | static `.a` | `libnx` | Real pinned devkitA64 strict archive and 25-entry-point check pass; frontend/device gates remain |
| Nintendo 3DS | static `.a` | `ctr` | Real pinned devkitARM strict archive and 25-entry-point check pass; frontend/device gates remain |
| GameCube/Wii/Wii U | static `.a` | `ngc`, `wii`, `wiiu` | Real pinned devkitPPC big-endian archives and 25-entry-point checks pass; frontend/device gates remain |
| PSP | static `.a` + installable `.PBP` | `psp1` | Real pinned PSPSDK MIPS archive and 25-entry-point check pass; pinned RetroArch links it into a verified `retroarchpsp.elf`/`EBOOT.PBP` and Memory Stick-ready release ZIP; PPSSPP/device run remains |
| PlayStation Vita/TV | static `.a` | `vita` | Real pinned VitaSDK ARM archive and 25-entry-point check pass; frontend/device gates remain |
| PlayStation 2 | static `.a` | `ps2` | Real pinned PS2DEV MIPS archive and 25-entry-point check pass; frontend/device gates remain |

Console archives are link inputs, not installable applications. RetroArch must link them
into platform packages such as `.nro`, `.3dsx`/`.cia`, `.dol`, `.rpx`, `.PBP`, `.vpk`,
or PS2 `.elf` files.

## Verified upstream gaps

| Family | Current upstream evidence | Decision |
|---|---|---|
| Dingux/OpenDingux | Live `mips32`, `mips32-odbeta`, `miyoo-arm32`, `retrofw-mips32`, and `rs90-mips32-odbeta` nightly directories | Add `gcw0`, `rs90`, `retrofw`, and `miyoo` after pinning the public sysroots and flavor flags |
| LG webOS | Live armv7a nightly directory and current RetroArch `Makefile.webos` | Add armv7 first, then arm64, using the webOSbrew native toolchain |
| BSD and Haiku | Generic Unix core contract; official Haiku guide | Validate native CI rather than inventing unnecessary libretro ABI branches |
| PS3 | libretro-super `psl1ght`, `ps3`, and `sncps3` recipes; no live nightly family | Prefer the public PSL1GHT toolchain; defer proprietary SDK paths |
| DOS | Current DJGPP core recipe and RetroArch `Makefile.dos` | Bounded static target after a pinned public DJGPP image is available |
| QNX/BlackBerry | Source recipe uses proprietary QNX `qcc`; no live nightly family | Defer until a lawful reproducible SDK/CI environment exists |
| Xbox/Xenon | Legacy XDK recipes plus an open libxenon path; no live nightly family | Consider libxenon only; do not add proprietary XDK CI |
| PS4 and README-only systems | RetroArch advertises more frontends than current public core recipes expose | Do not create speculative target mappings |

## Expansion order

1. Run the linked PSP package on device; complete Wii-family and other static frontend
   links and device tests for the archives that already compile.
2. Keep Linux x86 and ARMv7 NEON in normal CI alongside the other validated targets.
3. Add Dingux/OpenDingux and webOS from pinned public toolchains.
4. Validate BSD/Haiku native builds.
5. Add PSL1GHT PS3 and DOS when their full static frontend links can be reproduced.
6. Treat UWP, QNX, and legacy consoles as separate toolchain/legal projects.

## Primary sources

- [RetroArch platform list](https://github.com/libretro/RetroArch/blob/34c069f44f419b708c5362f96c54f959bc182dbe/README.md#platforms)
- [RetroArch platform makefiles](https://github.com/libretro/RetroArch/tree/34c069f44f419b708c5362f96c54f959bc182dbe)
- [libretro-super target configuration](https://github.com/libretro/libretro-super/blob/bc2b5463fe7e45a36295db54f74bc56b950dec4b/libretro-config.sh)
- [libretro-super build scripts](https://github.com/libretro/libretro-super/tree/bc2b5463fe7e45a36295db54f74bc56b950dec4b)
- [Official compilation guides](https://github.com/libretro/docs/tree/7a0833c074b31d5438dce6ef6a94eea7f5805ec6/docs/development/retroarch/compilation)
- [Linux nightlies](https://buildbot.libretro.com/nightly/linux/), [Nintendo nightlies](https://buildbot.libretro.com/nightly/nintendo/), [PlayStation nightlies](https://buildbot.libretro.com/nightly/playstation/), [Dingux nightlies](https://buildbot.libretro.com/nightly/dingux/), and [webOS nightlies](https://buildbot.libretro.com/nightly/webos/)

The required `deja` recall query found no prior platform inventory to reuse. This matrix
is derived from the pinned primary sources above and the builds recorded by this project.
