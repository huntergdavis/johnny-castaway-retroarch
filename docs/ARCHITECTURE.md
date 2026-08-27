# Architecture

## Frame ownership

RetroArch owns the event loop. One call to `retro_run()` polls input, advances the
50 Hz core clock once, submits one 640x480 XRGB8888 framebuffer, and submits 882
stereo samples at 44.1 kHz. The engine may keep a scene instruction dormant for
multiple ticks; the frontend call must never sleep or spin.

## Planned layers

```text
RetroArch callbacks
        |
libretro adapter (video, audio, input, options, VFS, save states)
        |
portable Johnny engine (director, ADS, TTM, walk, island)
        |
DGDS resources + software raster + deterministic mixer
```

The libretro adapter owns paths and callbacks. The engine accepts explicit inputs and
returns pixels/audio/state. File access goes through a small storage interface backed
by libretro VFS, with stdio only as a tested fallback.

## Rendering migration

SDL surfaces will not cross into the engine. The replacement surface contains width,
height, pitch, clip rectangle, format, transparency index, and owned/borrowed pixel
storage. The first renderer uses indexed pixels for layers and expands the composited
frame to XRGB8888. This preserves PS1-era memory wins while satisfying modern frontends.

Every raster primitive gets a host unit test and selected scenes get framebuffer hashes
and image comparisons against the C and Rust references.

## Audio migration

Original 11025 Hz mono effects are decoded from user data or optional external WAVs,
mixed deterministically, converted to signed stereo, and resampled to the declared
44.1 kHz stream. The core never opens an audio device.

## State and time

All story selection, random state, calendar override, resource-cache metadata needed
for correctness, ADS/TTM program counters, walk state, and active audio voices must be
serializable. Wall-clock behavior is optional and injected; deterministic mode is the
default for tests and save states.
