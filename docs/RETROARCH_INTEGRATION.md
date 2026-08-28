# RetroArch menu integration

Menu integration is a release gate for every platform, not a post-port task.

## Implemented now

- **Main Menu / Load Core**: core name, version, license, authors, and `.map|.001`
  content extensions are provided by the core and `.info` metadata.
- **Quick Menu / Core Options / Story**: Initial Screen selects Intro, day/night
  island, office, Suzy beach, or ending. Chapter exposes Static Screen plus all 63
  audited PS1 scene-explorer records; selecting a chapter starts its original ADS/tag
  and the first live-rendered frame acts as the graphical preview. Holiday Overlay
  provides Automatic (frontend device local date), Off, and 36 explicit force/preview
  values. A forced or date-matched holiday immediately draws an asset-free title/date
  band, opposite top-positioned captions. These options all apply immediately.
- **Quick Menu / Core Options / Video**: Display Source switches between original
  user data and the built-in diagnostic frame and applies immediately.
- **Quick Menu / Core Options / Audio**: master Enable/Volume and independent Ocean
  Ambience enable/volume controls apply immediately. The exact CC0 loop is decoded once,
  trimmed to its VAG loop markers, registered at the PS1 default 56% gain, and mixed by
  the deterministic eight-voice mixer. TTM sample events are dispatched; original SFX
  remain silent until owned sample extraction/loading is implemented.
- **Quick Menu / Core Options / Accessibility**: Closed Captions enable, size,
  background (none/box/full-width bar), background opacity, and top/center/bottom
  position apply immediately. Captions use the audited 79-entry PS1 catalog and a
  bounded software renderer.
- **Quick Menu / Controls / Port 1**: the port declares a RetroPad; Start restarts
  the current core timeline.
- **Quick Menu / State**: the versioned, pointer-free v2 envelope covers the base
  timeline, mixer (including ocean loop position), selected chapter, deterministic
  ADS/TTM runtime position, renderer output, and active caption/timing. Loading rebuilds
  owned runtime resources from the indexed content archive and silently replays the
  bounded timeline before atomically accepting the state. Same-build legacy base-plus-
  mixer states remain loadable; they predate chapter/runtime/caption persistence.
- Modern frontends receive categorized Core Options v2. Older frontends receive
  equivalent legacy variables, preserving menu support on console builds.

## Add only when functional

The following option groups are required before the story-runtime milestone is done,
but they must not appear as nonfunctional menu entries:

- **Story**: normal cycle, deterministic seed, simulated calendar override,
  tide/raft stage, and playback speed. Automatic local-date and explicit holiday
  override presentation are implemented.
- **Audio**: original SFX sample extraction and voice policy after multiple authentic
  policies exist.
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
