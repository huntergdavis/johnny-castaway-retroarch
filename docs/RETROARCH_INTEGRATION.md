# RetroArch menu integration

Menu integration is a release gate for every platform, not a post-port task.

## Implemented now

- **Main Menu / Load Core**: core name, version, license, authors, and `.map|.001`
  content extensions are provided by the core and `.info` metadata.
- **Quick Menu / Core Options / Story**: Initial Screen selects Intro, day/night
  island, office, Suzy beach, or ending. Story Playback / Chapter defaults to Automatic,
  which plans and advances the original opening/intermediate/walk/final sequence across
  story days. It also exposes Static Screen plus all 63 audited PS1 Scene Explorer
  records; selecting a chapter starts its original ADS/tag and the first live-rendered
  frame acts as the graphical preview. Holiday Overlay
  provides Automatic (frontend device local date), Off, and 36 explicit force/preview
  values. A forced or date-matched holiday draws only its scene emblem, never a
  title/date banner. The 32 holidays added by the GPL PS1 port use their generated
  32x32 emblems; the four original-game frames are decoded only from the user's own
  required resource archive and remain excluded from the distribution.
  Deterministic Seed selects a reproducible
  automatic plan; Calendar selects the frontend system clock or simulated month/day/hour
  fields; Tide selects automatic/high/low; and Raft selects automatic/none/stage 1–5.
  Tide and raft choices alter the authentic island composition while scene-local
  `NORAFT` remains authoritative. Playback Speed advances the story, walks, fades, and
  caption lifetime at 1x–4x without changing the 44.1 kHz audio callback cadence. Story
  controls restart automatic playback when required and use dynamic visibility so
  irrelevant simulated-date and automatic-only fields stay out of the menu.
- **Quick Menu / Core Options / Video**: Display Source switches between original
  user data and the built-in diagnostic frame and applies immediately.
- **Quick Menu / Core Options / Audio**: master Enable/Volume and independent Ocean
  Ambience enable/volume controls apply immediately. The exact CC0 loop is decoded once,
  trimmed to its VAG loop markers, registered at the PS1 default 56% gain, and mixed by
  the deterministic eight-voice mixer. TTM sample events play any valid user-supplied
  `sound<ID>.wav` siblings loaded beside the resource pair; all 23 possible files are
  optional and absent/invalid files fail softly. No original sample is bundled.
- **Quick Menu / Core Options / Accessibility**: Closed Captions enable, size,
  background (none/box/full-width bar), background opacity, and top/center/bottom
  position apply immediately. Captions use the audited 79-entry PS1 catalog and a
  bounded software renderer.
- **Quick Menu / Controls / Port 1**: the port declares a RetroPad; Start restarts
  the current core timeline.
- **Quick Menu / State**: the versioned, pointer-free v2 envelope covers the base
  timeline, mixer (including ocean loop position), automatic plan/day/scene identity or
  selected chapter, deterministic ADS/TTM runtime position, renderer output, and active
  caption/timing. Automatic states additionally preserve the transition phase and
  effective tide/raft and the exact calendar inputs used to plan the story, then
  deterministically rebuild an in-progress island walk or fade. Loading rebuilds
  owned runtime resources from the indexed content archive and silently replays the
  bounded timeline before atomically accepting the state. Same-build legacy base-plus-
  mixer states remain loadable; they predate chapter/runtime/caption persistence.
- Modern frontends receive categorized Core Options v2. Older frontends receive
  equivalent legacy variables, preserving menu support on console builds.

## Add only when functional

The required story-runtime controls are implemented. The remaining possible option
groups below are optional and must not appear as nonfunctional menu entries:

- **Audio**: an optional voice-stealing policy selector only if multiple authentic
  policies are implemented. User-owned original SFX sibling loading already exists and
  deliberately has no enable menu separate from master audio.
- **Accessibility**: high contrast and reduced flashing after their rendering behavior
  exists.
- **Video**: crop/overscan policy, palette mode, aspect policy, and optional filters
  only where they belong in the core rather than frontend shaders.
- **Controls**: pause, next/previous scene in explorer mode, caption toggle, and
  analog-pointer behavior if any interactive mode uses it.
- **Advanced**: resource cache budget, deterministic diagnostics, trace logging, and
  compatibility workarounds, with platform-aware visibility.

## Per-target menu test

For every platform, verify option categories and labels, persistence after restart,
live-update behavior, controller remapping, successful save/load state, useful load
errors, and no desktop-only options on constrained targets.

Linux additionally has a bounded real-frontend execution gate:

```sh
./scripts/build-target.sh native
scripts/test-native-retroarch.sh --content /path/to/RESOURCE.MAP
```

It asserts Core Options v2 registration and persistence, VFS, XRGB8888, API/geometry,
content indexing, clean unload, and a non-blank 640x480 screenshot. CI repeats it with
the generated fixture and forced `fishing1`; authentic Automatic Story remains a local,
user-owned-data gate.

For a longer automatic traversal, add `--frames 1500 --expected-min-scenes 2`.
