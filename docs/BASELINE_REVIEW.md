# Baseline review

Reviewed 2026-08-27 before implementation.

## Decision

Use portable C99 for the shipping core. Migrate the engine from the C family in small,
testable slices, incorporating the PS1 branch's memory work. Treat Wilson Reborn's
Rust engine as the main behavioral oracle for timing, DGDS parsing, scene direction,
and regression cases.

This is a synthesis, not a blind fork:

- **huntergdavis/jc_reborn** supplies the known engine lineage, platform seams,
  low-memory branches, and embedded lessons.
- **huntergdavis/johnny-castaway-ps1** supplies the most recent memory accounting,
  lazy resource loading, indexed surfaces, audio inventory, and visual test tooling.
- **huntergdavis/johnny_dreamcastaway** confirms the three platform seams
  (graphics, sound, events) and exposes older SDL assumptions to remove.
- **antigerme/wilson-reborn** has the cleanest modern engine/app separation, explicit
  frame pacing, a CPU framebuffer, broad tests, and current DGDS correctness work.
- **deckarep/Johnny-Castaway-2026-Public** is easy to cross-compile for Go-supported
  desktop targets, but Raylib plus the Go runtime/cgo boundary sharply reduces the
  set of legacy libretro toolchains it can reach.
- **jno6809/jc_reborn** remains the upstream historical gameplay blueprint.

## Why not ship the Rust engine directly?

Rust is an attractive desktop/mobile/Web base, but several common libretro static-core
targets depend on mature C cross toolchains with no equally routine Rust target. A
Rust staticlib would also add allocator, panic-runtime, target JSON, and ABI work before
the first frame. We still reuse its architecture and test knowledge.

## Why not ship the Go engine directly?

The Go port depends on Raylib and a Go runtime. Desktop cross-compilation is convenient,
but c-shared/c-archive support, runtime size, and legacy-console availability are worse
fits for a broad libretro target matrix.

## Reuse record

The initial repository reuses these established findings:

1. Software rendering at 640x480 is the correct portable frontend boundary.
2. Graphics, audio, input/time, and storage must be host adapters; engine code must
   never open a window, sleep, or own an audio device.
3. Original pacing is tick-driven; `retro_run()` must not busy-wait.
4. Indexed/low-memory surfaces and lazy resources from the PS1 work should remain
   available for small targets.
5. Original resources are bring-your-own and must not be committed.

Sources:

- https://github.com/huntergdavis/jc_reborn
- https://github.com/huntergdavis/johnny-castaway-ps1
- https://github.com/huntergdavis/johnny_dreamcastaway
- https://github.com/antigerme/wilson-reborn
- https://github.com/deckarep/Johnny-Castaway-2026-Public
- https://github.com/jno6809/jc_reborn
