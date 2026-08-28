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
complete. The five PS1-derived fade masks are implemented and connected to automatic
final-scene boundaries. Authentic regression now runs every one of the 63 chapters to
completion; dump-only diagnostic events remain non-rendering by design.

- Implement indexed surface allocation, clipping, fill, line, circle, blit, color key,
  horizontal flip, saved zones, palette expansion, and final compositing.
- Port SCR/BMP decode and validate it against reference hashes.
- Reuse the PS1 branch's indexed and low-memory strategies where they remain faithful.

### M3 — Story runtime

Status: the bounded ADS/TTM parsers, callback-driven multi-thread VM, deterministic
director, corrected weighted path data, and nonblocking walk animation are complete.
The VM-to-render callback bridge, authentic archive binding, one-ADS chapter starts,
live libretro framebuffer handoff, and deterministic pointer-free save states for the
current one-ADS chapter mode are complete and tested with synthetic bytecode. Island/
director assembly now plans and advances automatic ADS scenes with reproducible
plan/day/index state, and the persistent-island walk compositor is connected to
automatic scene boundaries with the authentic left-island offsets and tree-cover
occlusion. Active walk/fade transitions are deterministically reconstructed by
versioned save states. Synthetic, focused authentic, and all-63-chapter real-data
regressions cover story rollover, walking, fades, runtime replay, and completion.
Functional Core Options now expose deterministic plan seed, system/simulated calendar,
automatic/forced tide and raft stage, and 1x–4x playback speed. New automatic states
preserve the exact planning calendar and effective tide/raft while retaining old v2
state compatibility.

- Port TTM, ADS, director/story selection, island state, pathfinding, and walking.
- Compare deterministic scene traces with Wilson Reborn and the PS1 host harness.
- Keep the implemented seed, calendar, tide/raft, speed, and caption Core Options in
  the per-target frontend regression gate.

### M4 — Audio and frontend completeness

Status: an allocation-free WAV parser and deterministic eight-voice 11025-to-44100 Hz
stereo mixer are complete. The mixer runs every `retro_run()`, mute/volume are live Core
Options v2 plus legacy variables, and mixer phase is saved. The audited PS1 caption,
chapter, 36-holiday, and CC0 ambience foundations are complete. The exact licensed VAG
is embedded and decoded, the mixer loops it at independent gain, and TTM sample cues are
dispatched. All 63 chapters start as live-rendered Core Option previews, and caption
presentation has functional menu controls. Automatic/off/36-force holiday menu values
drive an asset-free title/date overlay. Versioned chapter-mode save states preserve and
deterministically reconstruct runtime, renderer, caption, core, and audio state; the
automatic plan/day/scene identity is also represented without serializing pointers.
Optional user-supplied `sound0.wav` through `sound24.wav` siblings are now loaded through
VFS/stdio and dispatched by original TTM sample IDs, with missing/invalid files failing
softly. Active island/walk/fade transition state is covered. Sprite-faithful holiday
decorations remain intentionally asset-free, and chapter selection stays in the
RetroArch menu rather than adding a second controller-driven explorer UI.

- Port sound triggers and deterministic multi-voice mixing.
- Add core options, reset semantics, robust save states, controller descriptors, and
  frontend messages.
- Complete and test every menu surface in `RETROARCH_INTEGRATION.md`; never expose a
  placeholder option that does nothing.
- Add content-free test ROM fixtures and automated libretro ABI smoke loading.
  **Synthetic MAP/archive fixture, mock frontend, real Linux RetroArch fixture run, and
  authentic local Linux run are complete; real frontend loading remains a gate for the
  other targets.**

### M5 — Platforms, one compiler at a time

Each row advances independently through Build/Load/Run/Regress.

| Wave | Make platform | Output | Current state |
|---|---|---|---|
| 0 | `linux_x86_64` | `.so` | strict build/ABI and mock-frontend checks pass; real RetroArch 1.22.2 loads authentic data, registers Core Options v2/VFS/XRGB8888, runs Automatic Story, and captures non-blank 640x480 output; the same bounded frontend gate runs synthetic content in CI |
| 0 | `linux_x86` | `.so` | real GCC multilib strict build, i386 machine check, and exact 25-symbol ELF export check pass; frontend run pending |
| 1 | `mingw_x86_64` | `.dll` | cross-build and ABI exports validated; frontend run pending |
| 1 | `mingw_x86` | `.dll` | real MinGW i686 strict build and exact 25-symbol PE export check pass; frontend run pending |
| 1 | `osx`, `osx_x86_64`, `osx_arm64` | `.dylib` | real Xcode per-architecture builds, exact ABI exports, and verified-`lipo` universal output pass in GitHub `macos-15`; frontend run pending |
| 1 | `linux_aarch64`, `linux_armv7`, `linux_armv7_neon` | `.so` | real GCC cross-builds and exact 25-symbol ELF export checks pass; ARMv7 variants are verified VFPv3-D16 or NEONv1 hard-float; frontend runs pending |
| 2 | Android arm64/armv7/x86_64/x86 | `.so` | real NDK r29/API 21 strict builds, machine checks, and exact 25-symbol exports pass; Android frontend/device runs pending |
| 2 | iOS/tvOS arm64 device + arm64/x86_64 simulators | `.dylib` | real Xcode SDK builds and exact Mach-O ABI exports pass in GitHub `macos-15`; device/frontend runs pending |
| 2 | `emscripten` | `.bc` static core + RetroArch `.js/.wasm` | archive, pinned real-frontend link/dist, HTTP checks, and real-Firefox synthetic plus authentic 180-resource/23-sound auto-load runs pass; deterministic Core Options/Story/Simulated Calendar navigation is screenshot-validated |
| 3 | `psp1` | static `.a` + RetroArch `.elf`/`EBOOT.PBP` | real official-SDK archive/link/package checks pass; pinned PPSSPP v1.20.4 completes a 30-second boot with module, GE, and framebuffer activity; physical-hardware rendering, input, audio, gameplay, performance, and save states remain |
| 3 | `vita`, `ctr`, `ps2` | static `.a` + VPK/3DSX/CIA/ELF frontends | strict archives and all 25 entry points pass; pinned RetroArch frontend builders validate formats, fixed identity, current Core Options, provenance, and no-data packaging; device/emulator runs pending |
| 3 | Switch, Wii, GameCube, Wii U | static `.a` (Switch NRO, GameCube DOL, Wii U RPX) | all strict archives pass; Switch, GameCube, and Wii U have pinned frontend/install-package builders and validators; Wii frontend compatibility and all device runs remain |
| 4 | Dingux, webOS, UWP, Haiku/BSD, PSL1GHT PS3, DOS | varies | official live/source inventory complete; add in this priority order with a real public toolchain and artifact contract |

Dreamcast and original PlayStation ports remain architectural and regression references.
They become libretro build targets only where a maintained RetroArch frontend/toolchain
contract exists.

## Today’s critical path

1. Core shell, content pairing, lazy resource I/O, and first DGDS frame. **Done.**
2. Bounded BMP/ADS/TTM, director/path/walk, and deterministic mixer foundations. **Done.**
3. Wire ADS resources to TTM slots and instruction events to the compositor/mixer.
   **Done for selectable chapters and automatic ADS sequencing; optional user-owned
   original SFX loading and automatic island/walk/fade transitions are wired and
   save-state tested. All 63 authentic chapters complete.**
4. Port feasible PS1 additions: captions, 36-holiday calendar, scene explorer previews,
   and the CC0 ocean ambience loop with full attribution. **Captions, chapter previews,
   calendar data and simulation, automatic/forced asset-free holiday presentation,
   tide/raft island composition, ambience, and playback controls are done. Proprietary
   holiday sprite presentation is deliberately not distributed.**
5. Browser RetroArch build/link/dist/HTTP and real-Firefox synthetic-content harness.
   **Done; local original-data auto-loading and real RetroArch Core Options navigation
   are release gates for the final rebuilt distribution.** Continue expanding targets
   one validated compiler/frontend at a time without weakening existing gates.

The project intentionally adds targets sequentially. A giant untested Makefile is not
considered platform support.
