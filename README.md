# Johnny Castaway libretro

A portable libretro core for running Johnny Castaway from user-supplied original
`RESOURCE.MAP` and `RESOURCE.001` files.

## Status

Milestone 0 and the first content/rendering slice are complete: the repository builds
a loadable libretro core, validates the original resource pair, decodes palette and SCR
resources, and displays selectable authentic screens or live ADS scenes through a
deterministic 640x480 XRGB8888 software framebuffer. Categorized Core Options v2,
legacy menu fallback, RetroPad input, audio, and versioned chapter-runtime save-state
round trips are working.

The repository now contains bounded ADS/TTM parsers and a resumable 50 Hz script VM,
an indexed sprite compositor, the deterministic story/walking runtime, and an eight-voice
sound mixer. It also includes the PS1 port's audited 79-caption/63-chapter catalogs,
36-holiday calendar with an asset-free automatic/forced preview overlay, and embedded
CC0 ocean ambience. TTM events now
drive a bounded indexed renderer for resources, layers, sprites,
primitives, saved zones, and frame composition. A bounded content-backed runtime
loads one ADS plus its declared TTM resources and drives the renderer at 50 Hz. That
runtime is connected to the libretro adapter: all 63 chapters can be selected as live
graphical previews, captions have complete presentation options, and the embedded ocean
loop has enable/volume controls. A Holiday Overlay Core Option uses the local date or
forces any of all 36 catalog entries, rendering its title/date without original artwork.
High-level day/story cycling, island/walk integration, sprite-faithful holiday decorations,
original sound-effect extraction, preview navigation controls, and save-state coverage
for the future director/island runtime remain. This is therefore not yet the complete
screensaver experience.

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

For a local browser test using a real RetroArch Web frontend:

```sh
./scripts/build-web-player.sh
./scripts/serve-web.sh --open
```

The browser page accepts the user's `.MAP` and `.001` together and keeps both in
an in-memory filesystem. See `docs/WEB_TESTING.md` for requirements, privacy details,
the exact pinned RetroArch revisions, automated checks, and known limitations.

List the staged cross targets with:

```sh
./scripts/build-target.sh --list
```

A target appearing in that list means the build mapping exists, not that it has
already passed its compiler and RetroArch runtime gates. The live status is in
`docs/PORTING_PLAN.md`.

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

The `switch`, `wii`, `gamecube`, and `wiiu` aliases use devkitPro and produce static
core archives for the corresponding statically linked RetroArch frontend. Run them from
a configured devkitPro shell (`DEVKITPRO`/`DEVKITA64` for Switch or `DEVKITPPC` for the
PowerPC consoles). Their current compiler/runtime validation status is recorded in the
porting plan.

## Data and copyright

No Sierra/Dynamix game data, artwork, or audio is distributed here. Users must provide
their own original files. The only bundled audio is the separately licensed CC0 ocean
ambience documented in `docs/licenses/BigSoundBank-0266-CC0.md`. The engine and core
code are GPLv3; the original game data remains the property of its rights holders.

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
- `scripts/build-apple-universal.sh`: verified x86_64/arm64 macOS merger
- `scripts/build-web-player.sh`, `scripts/serve-web.sh`: pinned local RetroArch Web test
- `web/`: local two-file launcher and Web-specific license notice
- `docs/`: architecture decisions and staged port plan
- `docs/RETROARCH_INTEGRATION.md`: menu/options acceptance checklist
- `docs/WEB_TESTING.md`: browser build, local server, and test workflow
- `docs/PROVENANCE.md`: exact upstream revisions and file-by-file reuse record
- `CREDITS.md`: original creators and open-source lineage
