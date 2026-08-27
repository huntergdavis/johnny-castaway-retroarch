# Porting plan

## Definition of done

A target is complete only after four separate gates:

1. **Build**: its documented toolchain produces the expected shared or static core.
2. **Load**: its RetroArch frontend loads the core and identifies content.
3. **Run**: original user-supplied data reaches Johnny's intro and multiple scenes with
   video, input, and audio.
4. **Regress**: deterministic traces/frame hashes pass and a soak run stays within the
   target memory budget.

## Work sequence

### M0 — Core shell (complete)

- New `main` Git repository and pinned official libretro headers.
- XRGB8888 software-video callback at 640x480 and 50 Hz.
- Audio batch timing, joypad reset, deterministic state serialization.
- Native unit test and common `platform=` Makefile shape.

### M1 — Content and engine clock

Status: content pairing, VFS/stdio access, safe map parsing, lazy entry reads, and real
archive validation are complete. The resumable ADS/TTM clock remains.

- Accept `RESOURCE.MAP` as content and find the sibling `RESOURCE.001` safely.
- Add libretro VFS with stdio fallback and exact error messages through the log API.
- Port the MAP/volume parser and decompressor with fixtures that contain no copyrighted
  data.
- Replace blocking SDL tick waits with a resumable 50 Hz engine state machine.

### M2 — Software raster

Status: indexed surfaces, clipped transparent/flipped blits, stored/RLE/LZW decode,
palette decode, SCR decode, and final XRGB expansion are complete and sanitizer-tested.
BMP sheets, remaining primitives, saved zones, and layered composition remain.

- Implement indexed surface allocation, clipping, fill, line, circle, blit, color key,
  horizontal flip, saved zones, palette expansion, and final compositing.
- Port SCR/BMP decode and validate it against reference hashes.
- Reuse the PS1 branch's indexed and low-memory strategies where they remain faithful.

### M3 — Story runtime

- Port TTM, ADS, director/story selection, island state, pathfinding, and walking.
- Compare deterministic scene traces with Wilson Reborn and the PS1 host harness.
- Add core options for deterministic seed, calendar override, speed, and captions.

### M4 — Audio and frontend completeness

- Port sound triggers and deterministic multi-voice mixing.
- Add core options, reset semantics, robust save states, controller descriptors, and
  frontend messages.
- Complete and test every menu surface in `RETROARCH_INTEGRATION.md`; never expose a
  placeholder option that does nothing.
- Add content-free test ROM fixtures and automated libretro ABI smoke loading.

### M5 — Platforms, one compiler at a time

Each row advances independently through Build/Load/Run/Regress.

| Wave | Make platform | Output | Current state |
|---|---|---|---|
| 0 | `linux_x86_64` | `.so` | implemented; local validation pending |
| 1 | `mingw_x86_64` | `.dll` | cross-build and 25 ABI exports validated |
| 1 | `mingw_x86` | `.dll` | build mapping added; compiler unavailable locally |
| 1 | `osx` (x86_64/arm64) | `.dylib` | build mapping added; universal script pending |
| 1 | `linux_aarch64`, `linux_armv7` | `.so` | build mapping added |
| 2 | Android arm64/armv7/x86_64/x86 | `.so` | NDK mapping pending |
| 2 | iOS/tvOS | static `.a` | toolchain mapping pending |
| 2 | `emscripten` | `.bc` static core | wasm objects and archive build validated |
| 3 | `psp1`, `vita`, `ctr`, `ps2` | static `.a` | initial compiler mapping added |
| 3 | Switch, Wii, GameCube, Wii U | static `.a` | devkitPro mapping pending |
| 4 | PS3, Xbox-family, Haiku/BSD/webOS and other buildbot targets | varies | inventory pending |

Dreamcast and original PlayStation ports remain architectural and regression references.
They become libretro build targets only where a maintained RetroArch frontend/toolchain
contract exists.

## Today’s critical path

1. Finish and validate M0.
2. Implement M1 content pairing and resource I/O.
3. Port the surface type plus enough SCR decoding to display a real background. **Done.**
4. Land a first DGDS-driven frame before expanding the platform matrix. **Done.**
5. Compile Windows x64 after native Linux; record missing toolchains rather than hiding
   failures.

The project intentionally adds targets sequentially. A giant untested Makefile is not
considered platform support.
