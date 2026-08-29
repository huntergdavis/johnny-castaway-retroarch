# Johnny Castaway libretro

A portable libretro core for running Johnny Castaway from user-supplied original
`RESOURCE.MAP` and `RESOURCE.001` files.

## Status

The `0.1.3` development core builds as a loadable libretro core,
validates the original resource pair, decodes the original content, and implements the full
automatic story or any of 63 selectable live chapter previews through a deterministic
640x480 XRGB8888 software framebuffer. Categorized Core Options v2, legacy menu
fallback, RetroPad input, audio, and versioned chapter-runtime save-state round trips
are working. Host, Web, and cross-compilation release gates are documented below;
physical-device execution remains a platform-specific validation step rather than a
claim made by this repository. On 2026-08-28, the clean `02e8ddb` Web candidate passed
three consecutive authentic-data Firefox runs with changing Automatic Story and water
frames, no material renderer-key block, an explicit blocked-autoplay/Enable Audio
round trip, and gap-free gameplay audio. The three strict five-second audio windows
queued/ended 512/466, 501/471, and 510/478 pinned 10 ms buffers, scheduled 0.997405,
0.995173, and 1.002248 times wall duration, and recorded zero positive gaps. Stable
RetroArch menus intentionally paused the core and produced exact all-zero audio
windows. A separate ordinary staged-local full-options run also passed. These results
close the previously reported Web renderer/audio blocker for this exact candidate;
device-specific execution gates remain separate work. The authentic Web chapter audit
has also exercised every catalog entry: 62 chapters passed the generic strict window,
and short `stand16` passed a reset-scoped early-motion window that proves five changing
frames, local actor motion, gap-free audio, and clean WebGL before its terminal hold.

The repository now contains bounded ADS/TTM parsers and a resumable 50 Hz script VM,
an indexed sprite compositor, the deterministic story/walking runtime, and an eight-voice
sound mixer. It also includes the PS1 port's audited 79-caption/63-chapter catalogs,
36-holiday calendar with an asset-free automatic/forced preview overlay, and embedded
CC0 ocean ambience. TTM events now
drive a bounded indexed renderer for resources, layers, sprites,
primitives, saved zones, and frame composition. A bounded content-backed runtime
loads one ADS plus its declared TTM resources and drives the renderer at 50 Hz. That
runtime is connected to the libretro adapter: Automatic mode plans and advances the
original opening/intermediate/walk/final story sequence, while all 63 chapters can be
selected individually as live graphical previews. Story options expose reproducible
seed presets, a system or simulated calendar, 1x–4x playback, and automatic or forced
tide/raft presentation with authentic island sprites. Captions have complete
presentation options, optional user-supplied `sound0.wav`–`sound24.wav` effects are loaded from beside
the resource pair (IDs 11/13 are absent), and the embedded ocean loop has enable/volume
controls. A Holiday Overlay Core Option uses the local date or forces any of all 36
catalog entries, rendering its title/date without original artwork. Tested portable
modules also implement the PS1-derived five-shape fades and persistent clean-island
walking/tree occlusion. Automatic playback uses both at scene boundaries, including
left-island offsets and tree occlusion, and versioned save states deterministically
restore active walks and fades. Every one of the 63 authentic chapters completes in
the host runtime regression suite. Sprite-faithful holiday decorations are deliberately
not distributed because they require original artwork; the asset-free holiday overlay
and live chapter previews provide the feasible portable equivalents.

## Why C

The shipping core is C99. This keeps the libretro ABI direct, avoids SDL/Raylib/window
dependencies, and preserves access to legacy console toolchains. The engine migration
will start from the proven C ports while using the 2026 Rust implementation as a
behavior and regression-test reference. See `docs/BASELINE_REVIEW.md`.

## Build

Clone recursively, then run:

```sh
./scripts/build-target.sh native
make test
```

The native core is written to `build/linux_x86_64/johnny_castaway_libretro.so`
on Linux or the corresponding platform directory on macOS.

With native RetroArch and ImageMagick installed, exercise the actual frontend against
your user-owned data for a bounded 180 frames and retain a screenshot/log under ignored
`build/` output:

```sh
scripts/test-native-retroarch.sh --content /path/to/RESOURCE.MAP
```

When no display is available, the script uses `xvfb-run`. Normal CI runs this same gate
with the repository's generated five-resource fixture and a live `fishing1` chapter.

For a local browser test using a real RetroArch Web frontend:

```sh
./scripts/build-web-player.sh
./scripts/serve-web.sh --open
```

The browser page accepts the user's `.MAP` and `.001` together and keeps both in
an in-memory filesystem. See `docs/WEB_TESTING.md` for requirements, privacy details,
the exact pinned RetroArch revisions, automated checks, and known limitations.
For automatic local startup, stage a private user-owned pair (and optional sibling
WAVs) into ignored build output with `scripts/stage-local-web-content.sh`; never expose
that server publicly or include the staged files in a release.

List the staged cross targets with:

```sh
./scripts/build-target.sh --list
```

A target appearing in that list means the build mapping exists, not that it has
already passed its compiler and RetroArch runtime gates. The evidence-based target
inventory and live status are in `docs/PLATFORM_MATRIX.md` and `docs/PORTING_PLAN.md`.

Android builds use the NDK r22-or-newer LLVM toolchain layout and build one ABI at a
time. Set `ANDROID_NDK_HOME` (or `ANDROID_NDK_ROOT`), then select the ABI:

```sh
ANDROID_NDK_HOME=/path/to/android-ndk ./scripts/build-target.sh android-arm64
ANDROID_NDK_HOME=/path/to/android-ndk ./scripts/build-target.sh android-armv7
ANDROID_NDK_HOME=/path/to/android-ndk ./scripts/build-target.sh android-x86_64
ANDROID_NDK_HOME=/path/to/android-ndk ./scripts/build-target.sh android-x86
```

`ANDROID_API` defaults to 21. `ANDROID_NDK_HOST_TAG` is inferred for Linux and macOS
and can be overridden for another NDK host package.

Apple builds require macOS with Xcode command-line tools. The device and simulator
aliases each produce the dynamically loaded `.dylib` module expected by current
RetroArch Apple packaging:

```sh
./scripts/build-target.sh macos-universal
./scripts/build-target.sh ios-arm64
./scripts/build-target.sh ios-sim-arm64       # or ios-sim-x86_64
./scripts/build-target.sh tvos-arm64
./scripts/build-target.sh tvos-sim-arm64      # or tvos-sim-x86_64
```

`IOS_DEPLOYMENT_TARGET` and `TVOS_DEPLOYMENT_TARGET` default to 12.0 and can be
overridden. The universal macOS command builds separate x86_64 and arm64 slices and
verifies the merged binary with Xcode's `lipo`.

Eight console cores can be reproduced with pinned official SDK containers:

```sh
./scripts/build-console-cores.sh --pull --all
```

This builds and validates static RetroArch core archives for PSP, Vita, PlayStation 2,
Nintendo 3DS, GameCube, Wii, Wii U, and Switch. Static `.a` files are frontend link
inputs, not installable console applications. Build pinned Switch NRO, Nintendo
3DSX/SMDH/CIA, GameCube DOL, Wii Homebrew Channel DOL, Wii U RPX, Vita VPK,
and PS2 ELF install packages with:

```sh
./scripts/build-installable-frontends.sh --all
```

The unified builder emits platform-native packages and SD-card layouts. It fixes
platform identities, validates the embedded core and current Story/Core Options,
normalizes package metadata, ships legal/provenance records, and rejects original
content. PSP separately ships a Memory Stick-ready ZIP containing
`PSP/GAME/JohnnyCastaway/EBOOT.PBP`; pinned PPSSPP boots it for 30 seconds and verifies
module, GE, and framebuffer activity. Hardware rendering, input, audio, and gameplay
remain the PSP device boundary. See `docs/INSTALLABLE_FRONTENDS.md`,
`docs/PSP_PACKAGE.md`, and
`docs/CONSOLE_BUILDS.md` for installation, exact image digests, and validation.

## Data and copyright

No Sierra/Dynamix game data, artwork, or audio is distributed here. Users must provide
their own original resource pair and, optionally, the supported sibling WAVs. The only
bundled audio is the separately licensed CC0 ocean ambience documented in
`docs/licenses/BigSoundBank-0266-CC0.md`. The engine and core code are GPLv3; the
original game data remains the property of its rights holders. See
`docs/OPTIONAL_ORIGINAL_AUDIO.md` before staging or sharing a Web build.

This port builds on years of community format research and several open-source Johnny
Castaway engines. See [CREDITS.md](CREDITS.md) for people and project history,
[docs/PROVENANCE.md](docs/PROVENANCE.md) for file-level derivation, and
[docs/THIRD_PARTY_NOTICES.md](docs/THIRD_PARTY_NOTICES.md) for dependency licenses and
notices.

## Repository layout

- `src/libretro_core.c`: libretro ABI and frontend callbacks
- `src/jc_core.c`: deterministic engine/framebuffer boundary
- `external/libretro-common`: pinned official libretro headers
- `tests/`: host-side deterministic tests
- `scripts/build-target.sh`: native and cross-build entry point
- `scripts/build-console-cores.sh`: pinned eight-platform console archive builder
- `scripts/build-installable-frontends.sh`: pinned Switch/3DS/GameCube/Wii/Wii U/Vita/PS2 frontend packages
- `scripts/build-psp-frontend.sh`: pinned PSP RetroArch EBOOT/install-package builder
- `scripts/build-apple-universal.sh`: verified x86_64/arm64 macOS merger
- `scripts/build-web-player.sh`, `scripts/serve-web.sh`: pinned local RetroArch Web test
- `scripts/test-native-retroarch.sh`: bounded real-frontend Linux execution gate
- `scripts/assemble-release.sh`: fail-closed assembly of exact successful CI artifacts
- `web/`: local two-file launcher and Web-specific license notice
- `docs/`: architecture decisions and staged port plan
- `docs/RETROARCH_INTEGRATION.md`: menu/options acceptance checklist
- `docs/WEB_TESTING.md`: browser build, local server, and test workflow
- `docs/RELEASING.md`: exact-SHA artifact assembly, audit, and publication boundary
- `docs/OPTIONAL_ORIGINAL_AUDIO.md`: user-supplied SFX provenance and distribution boundary
- `docs/CONSOLE_BUILDS.md`: reproducible console SDK images and packaging gates
- `docs/INSTALLABLE_FRONTENDS.md`: Switch/3DS/GameCube/Wii/Wii U/Vita/PS2 installation and validation
- `docs/FRONTEND_SDK_NOTICES.md`: runtime SDK/portlib source and license ledger
- `docs/PSP_PACKAGE.md`: PSP Memory Stick installation and validation boundary
- `docs/PLATFORM_MATRIX.md`: source-backed RetroArch platform inventory and coverage
- `docs/PROVENANCE.md`: exact upstream revisions and file-by-file reuse record
- `CREDITS.md`: original creators and open-source lineage
