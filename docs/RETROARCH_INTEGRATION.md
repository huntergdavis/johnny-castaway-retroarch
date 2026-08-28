# RetroArch menu integration

Menu integration is a release gate for every platform, not a post-port task.

## Implemented now

- **Main Menu / Load Core**: core name, version, license, authors, and `.map|.001`
  content extensions are provided by the core and `.info` metadata.
- **Quick Menu / Core Options / Story**: Initial Screen selects Intro, day/night
  island, office, Suzy beach, or ending and applies immediately.
- **Quick Menu / Core Options / Video**: Display Source switches between original
  user data and the built-in diagnostic frame and applies immediately.
- **Quick Menu / Core Options / Audio**: Enable and Volume control the deterministic
  eight-voice mixer immediately. Mixer phase/configuration is included in save states;
  actual sound cues land with the ADS/TTM runtime and owned WAV loading.
- **Quick Menu / Controls / Port 1**: the port declares a RetroPad; Start restarts
  the current core timeline.
- **Quick Menu / State**: serialize/unserialize is implemented for deterministic
  timeline state. State coverage expands with each engine subsystem.
- Modern frontends receive categorized Core Options v2. Older frontends receive
  equivalent legacy variables, preserving menu support on console builds.

## Add only when functional

The following option groups are required before the story-runtime milestone is done,
but they must not appear as nonfunctional menu entries:

- **Story**: normal cycle vs scene explorer, initial day/scene, deterministic seed,
  real/simulated calendar, holiday override, tide/raft stage, and playback speed.
- **Audio**: ambience and voice/mixer policy after multiple authentic policies exist.
- **Accessibility**: captions, scene descriptions, high contrast, reduced flashing,
  and caption size/background.
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
