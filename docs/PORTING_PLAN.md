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

Status: complete. Content pairing, VFS/stdio access, safe map parsing, lazy entry reads,
real archive validation, bounded ADS/TTM parsing, and the resumable 50 Hz script clock
are implemented and sanitizer-tested.

- Accept `RESOURCE.MAP` as content and find the sibling `RESOURCE.001` safely.
- Add libretro VFS with stdio fallback and exact error messages through the log API.
- Port the MAP/volume parser and decompressor with fixtures that contain no copyrighted
  data.
- Replace blocking SDL tick waits with a resumable 50 Hz engine state machine.

### M2 — Software raster

Status: indexed surfaces, clipped transparent/flipped blits, stored/RLE/LZW decode,
palette/SCR/BMP decode, ordered layered composition, and final XRGB expansion are complete
and sanitizer-tested. TTM events now drive SCR/PAL/BMP loading, clips, sprites, core
primitives, background snapshots, saved zones, per-thread layers, and ordered frame
composition. Content-backed runtime loading and frontend framebuffer handoff are now
complete. Remaining work is fades and less-common/dump-only image operations.

- Implement indexed surface allocation, clipping, fill, line, circle, blit, color key,
  horizontal flip, saved zones, palette expansion, and final compositing.
- Port SCR/BMP decode and validate it against reference hashes.
- Reuse the PS1 branch's indexed and low-memory strategies where they remain faithful.

### M3 — Story runtime

Status: the bounded ADS/TTM parsers, callback-driven multi-thread VM, deterministic
director, corrected weighted path data, and nonblocking walk animation are complete.
The VM-to-render callback bridge, authentic archive binding, one-ADS chapter starts,
and live libretro framebuffer handoff are complete and tested with synthetic bytecode.
Island/director assembly, original audio sample loading, whole-runtime save-state
aggregation, and deterministic real-data trace comparison remain.

- Port TTM, ADS, director/story selection, island state, pathfinding, and walking.
- Compare deterministic scene traces with Wilson Reborn and the PS1 host harness.
- Add core options for deterministic seed, calendar override, speed, and captions.

### M4 — Audio and frontend completeness

Status: an allocation-free WAV parser and deterministic eight-voice 11025-to-44100 Hz
stereo mixer are complete. The mixer runs every `retro_run()`, mute/volume are live Core
Options v2 plus legacy variables, and mixer phase is saved. The audited PS1 caption,
chapter, 36-holiday, and CC0 ambience foundations are complete. The exact licensed VAG
is embedded and decoded, the mixer loops it at independent gain, and TTM sample cues are
dispatched. All 63 chapters start as live-rendered Core Option previews, and caption
presentation has functional menu controls. Original sample loading/executable extraction,
holiday sprite visuals, explorer controller navigation, and full engine state remain.

- Port sound triggers and deterministic multi-voice mixing.
- Add core options, reset semantics, robust save states, controller descriptors, and
  frontend messages.
- Complete and test every menu surface in `RETROARCH_INTEGRATION.md`; never expose a
  placeholder option that does nothing.
- Add content-free test ROM fixtures and automated libretro ABI smoke loading.
  **Synthetic MAP/archive fixture and mock frontend test complete; real RetroArch loading
  remains a per-target gate.**

### M5 — Platforms, one compiler at a time

Each row advances independently through Build/Load/Run/Regress.

| Wave | Make platform | Output | Current state |
|---|---|---|---|
| 0 | `linux_x86_64` | `.so` | build and mock-frontend validation pass; RetroArch load/run pending |
| 1 | `mingw_x86_64` | `.dll` | cross-build and ABI exports validated; frontend run pending |
| 1 | `mingw_x86` | `.dll` | build mapping added; compiler unavailable locally |
| 1 | `osx`, `osx_x86_64`, `osx_arm64` | `.dylib` | per-architecture mappings and verified-`lipo` universal script added; real Xcode build/frontend run pending |
| 1 | `linux_aarch64`, `linux_armv7` | `.so` | build mapping added |
| 2 | Android arm64/armv7/x86_64/x86 | `.so` | NDK r22+ mappings and compiler dry-runs pass; real NDK builds/frontend runs pending |
| 2 | iOS/tvOS arm64 device + arm64/x86_64 simulators | `.dylib` | Xcode SDK/deployment mappings and compiler dry-runs pass; real Xcode builds/frontend runs pending |
| 2 | `emscripten` | `.bc` static core + RetroArch `.js/.wasm` | archive and pinned real-frontend link/dist/HTTP smoke pass; interactive lawful-data run pending |
| 3 | `psp1`, `vita`, `ctr`, `ps2` | static `.a` | initial compiler mapping added |
| 3 | Switch, Wii, GameCube, Wii U | static `.a` | devkitPro mappings and compiler dry-runs pass; real toolchain/frontend runs pending |
| 4 | PS3, Xbox-family, Haiku/BSD/webOS and other buildbot targets | varies | inventory pending |

Dreamcast and original PlayStation ports remain architectural and regression references.
They become libretro build targets only where a maintained RetroArch frontend/toolchain
contract exists.

## Today’s critical path

1. Core shell, content pairing, lazy resource I/O, and first DGDS frame. **Done.**
2. Bounded BMP/ADS/TTM, director/path/walk, and deterministic mixer foundations. **Done.**
3. Wire ADS resources to TTM slots and instruction events to the compositor/mixer.
   **Done for selectable one-ADS chapters; director transitions and original SFX remain.**
4. Port feasible PS1 additions: captions, 36-holiday calendar, scene explorer previews,
   and the CC0 ocean ambience loop with full attribution. **Captions, chapter previews,
   calendar data, and ambience are done; holiday sprite presentation remains.**
5. Browser RetroArch build/link/dist/HTTP harness. **Done; interactive lawful-data run
   remains.** Complete native RetroArch smoke, then expand targets one validated
   compiler/frontend at a time.

The project intentionally adds targets sequentially. A giant untested Makefile is not
considered platform support.
