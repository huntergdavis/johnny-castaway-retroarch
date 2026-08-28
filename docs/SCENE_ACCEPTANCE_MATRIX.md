# PS1-derived scene acceptance matrix

This repository has exactly the same 63 selectable scenes as Hunter Davis's
GPL-3.0-or-later PS1 port. There are no additional PS1 scenes to add. The
machine-readable companion, `scene_acceptance_matrix.csv`, expands those 63
scenes into 126 forced high/low-tide acceptance rows and adds the six shared
static initial-screen states.

This is an acceptance oracle, not permission to redistribute Sierra game
data. The matrix contains names, hashes, event IDs, and counts only. It does
not contain screenshots, thumbnails, scripts, samples, or decoded pixels.

## Evidence and precedence

The behavior/rendering oracle is the local checkout
`/home/hunter/workspace/jc_reborn`, whose `origin` is
`huntergdavis/Johnny-Castaway-PS1.git`, at exact revision
`25c5d84593ac20cbee354eaab7779ab7397d6bbe`:

- `docs/ps1/scene-status.md` is the primary human signoff ledger. It has 63/63
  rows with both Visuals and SFX checked and says the bar is pixel-perfect
  visuals plus synchronized SFX across applicable variants.
- `src/pause_menu/scene_explorer_data.h` is generated from the status ledger,
  per-scene site metadata, and FG2 headers. It has exactly the same 63 slugs,
  every `validated` bit is 1, and its frame counts match this repository's
  `src/jc_chapters.c`.
- Tracked `regtest-results/comparison-63/compare.json` reports 63 `MATCH`
  verdicts. `regtest-results/sweep-63/summary.txt` reports 63 passes and no
  failures. `regtest-results/validated-63/validation.json` also reports 63
  passes.
- `scripts/build-scene-explorer-thumbnails.py` selects the 70th-percentile
  scene-play capture, resizes the 640x448 DuckStation image to 320x208, adds
  PS1 menu chrome to 320x240, and encodes RGB555 `SX*.SCR`. There is no
  `scripts/scene-explorer-overrides.json` at the oracle revision. The local
  `regtest-references/<ADS>-<TAG>` images are user-owned/untracked evidence and
  are referenced by ID only; they are not copied here.
- `docs/ps1/visual-detection-spec.md` provides the palette and screen
  signatures used by the executable class gates. In particular, palette
  index 0 is `#a800a8`; OCEAN is blue/cyan dominated, NIGHT is dark-blue
  dominated, JOFFICE is black/gray, SUZBEACH is black with ocean colors, and
  THEEND is black with a unique bright-red region.
- `src/foreground_pilot/runtime_memory.c.inc` routes `johnny1` and `johnny6`
  through a full-screen black setup, `suzy1` and `suzy2` through
  `SUZBEACH.SCR`, and the other 59 scenes through the island compositor.
- `src/host/sound.c` and `src/platform/ps1/sound_ps1.c` establish that script
  sample IDs are 0 through 24. IDs 11 and 13 are legal script calls but their
  assets do not exist, so playback is the expected silent no-op.

Historical completion commits include
`86b2a62d503e17bea6e3d0a9cad5afe0687dee11` (63-scene validation),
`ba9d86bfede74ef79d24ca35356dab3546a48846`,
`ac483232d504715cf9ac54fd708cb579a96d0c54`, and
`eb3b3d6909460f72748e17f35091312691a5637f` (Scene Explorer metadata and
presentation). Scene-specific thumbnail corrections include
`d51e6326eab92ed8206fbe88f0fa576ae5b5af33` (`johnny1`),
`3e08621e2c14d937d9d04772d1f1295c84a3fa74` (`johnny6`),
`163c28eebafee13df6535b4606fa572d5b5fe619` (`suzy1`), and
`807e2643385dab00b161f52a862818e7a9a8aaa8` (`suzy2`).

Required `deja` recall was run before deriving this matrix. The broad query
had no detailed matrix match; narrower `PS1 scene explorer` and capture-
acceptance queries recovered prior PS1-path and Scene Explorer context. The
catalog, routing, thresholds, and assertions in this document were then
derived directly from the exact repository paths above rather than copied
from a prior agent answer.

The PS1 `reference-classification/classification.json` is not used as a
semantic scene oracle: its capture window includes an island prelude, so it
classifies all 63 records as containing island/Johnny even for the four
scene-specific backgrounds.

## Executable native contract

Run both the VM/audio-event sweep and production libretro visual sweep with
user-owned data:

```sh
make authentic-test CONTENT=/absolute/path/to/RESOURCE.MAP
```

The combined command writes ordered sound metadata to
`build/scene-sound-events.csv` and production visual signatures to
`build/scene-visual-results.csv`. Both paths are ignored build evidence and
contain no sample bytes, screenshots, or decoded pixels.

Run only the production visual sweep with:

```sh
make authentic-visual-test CONTENT=/absolute/path/to/RESOURCE.MAP
```

`tools/check_all_chapters.c` completes every ADS/tag at a bounded 20,000 VM
ticks and can emit ordered `PLAY_SAMPLE` events with sample ID, 1-based VM
tick, and current frame ordinal. Missing arguments and IDs 25 or greater are
failures. Legal absent IDs 11/13 are labeled and counted as `known_silent`,
not misreported as corrupt scripts.

`tools/check_all_chapter_visuals.c` is a minimal libretro frontend linked to
the production core. It selects all 63 fixed chapters at forced high tide and
again at forced low tide. It starts at 1x for the immediate composed frame,
then uses 4x playback while examining every presented frame. For every row it
requires:

- a loaded scene, true runtime-finished state, no runtime error, and at most
  20,000 VM ticks;
- at least 1,024 meaningful nonblack/non-key pixels and at least two distinct
  full-frame hashes (the allowance for short/static story content still sees
  the production island-wave phase when applicable);
- no horizontal run of eight exact `0x00a800a8` key pixels and no four-
  connected key component of 64 pixels;
- a class signature: blue/cyan majority for island scenes; black plus red for
  `johnny1`; black plus gray for `johnny6`; black plus ocean colors for Suzy;
- recorded start, middle, and final hashes plus maximum key and meaningful-
  pixel metrics in `build/scene-visual-results.csv`;
- different high/low signatures for all 59 island scenes and identical
  signatures for the four non-island scenes, where tide is intentionally not
  an input.

The core's high/low override is a forced diagnostic control for every island
scene. `JC_SCENE_LOW_TIDE` in `src/jc_story_data.inc` describes whether the
automatic director may choose low tide; it does not make the explicit low-
tide menu selection inapplicable. The non-island rows intentionally do not
vary because their black/Suzy backgrounds have no island shoreline or wave
layer.

## Scene classes and cleanup

| Rows | PS1 setup class | Expected libretro presentation |
|---|---|---|
| 59 island scenes | OCEAN00/01/02 or NIGHT, then BACKGRND island/raft/shore/waves | Deterministic day baseline uses OCEAN00; night uses NIGHT; high/low shoreline and waves differ |
| `johnny1` | black full-screen foreground route | Finale composite/THEEND behavior; no generic island or wave overlay |
| `johnny6` | black full-screen foreground route | JOFFICE office/daydream behavior; no ocean/island background |
| `suzy1`, `suzy2` | SUZBEACH scene-specific route | SUZBEACH composite; no generic island base |

The tool transitions through the static screen mode before every row. The
next immediate frame must satisfy its own class signature, which red-teams
stale island/ending/Suzy layers during phase cleanup. It does not accept a
single total-magenta percentage as proof; exact-key runs and connected
components are checked on every presented frame.

The deterministic day limitation is deliberate and visible: the libretro
port currently chooses `OCEAN00.SCR`, while the original engine can select
OCEAN00, OCEAN01, or OCEAN02. OCEAN01/02 parity remains future variant work,
not a completed claim.

## Ending-specific behavior

The PS1 ledger defines `johnny1` as the Day 11 finale: frog clock, sunset
silhouette, plane overhead, Johnny parachuting down, and the final “The End”
title card. Acceptance therefore requires multiple frame hashes, no generic
island/wave overlay, a black-backed full-screen presentation, a substantial
bright-red phase, clean final state, and high/low invariance. A future golden
phase oracle should bind the frog-clock, sunset, plane, parachute, and title
beats independently rather than treating a final THEEND frame as the whole
scene.

The Web release gate binds the last of those phases explicitly:
`tools/web_smoke_test.py --chapter johnny1 --test-late-ending` waits for the
black/red title signature and requires four clean stable frames without a
persistent frog-clock region, blank canvas, renderer-key block, or purple lower
band. It complements rather than replaces the all-scene temporal-motion matrix;
the intermediate sunset/plane/parachute beats still need a future golden phase
oracle.

`johnny6` is the office-desk/daydream inverse-castaway gag. The PS1 oracle
explicitly says no ocean/island background is painted. Its acceptance class
is black/gray office, multiple phases, no generic waves, clean transition,
and high/low invariance.

## Shared initial screens

The six screen-mode records are not extra scenes and do not change the 63
catalog entries:

| Core value | Resource | Required signature |
|---|---|---|
| `intro` | `INTRO.SCR` | mostly black with cyan/blue title colors |
| `island_day` | `ISLAND2.SCR` | static island/ocean signature; distinct from runtime OCEAN00 composition |
| `island_night` | `NIGHT.SCR` | dark blue/black, almost no bright blue |
| `office` | `JOFFICE.SCR` | black majority plus gray office pixels |
| `suzy_beach` | `SUZBEACH.SCR` | black majority plus ocean colors |
| `ending` | `THEEND.SCR` | black majority plus more than 5% bright red |

The production-core authentic visual tool executes all six rows, enforces the
signatures above, rejects key runs/components, and requires six distinct
full-frame hashes. The ordinary synthetic libretro unit test still has
dedicated pixel hashes only for `intro` and `island_day`.

## Web acceptance still required

`tools/web_smoke_test.py --chapter <slug> --content-dir <dir>` can launch any
of the 63 fixed chapters, and its current frame-quality gate rejects a
material four-connected renderer-key block (at least 256 pixels and at least
16x4 bounds). It does not yet execute a tracked all-63 matrix. The Web follow-
up should shard the CSV rows, use the native tick counts to choose an adaptive
window instead of a fixed six seconds, record start/middle/final PNG hashes,
apply the same class and phase-cleanup assertions, and preserve the current
audio-cadence checks.

The PS1 thumbnail is an expected-preview locator, not a committed golden
bitmap. Where the user has local `regtest-references/<ADS>-<TAG>` captures, a
separate opt-in comparator can normalize the PS1 640x448 viewport and compare
semantic phases. Those copyrighted/user-owned images must remain untracked.

This document does **not** claim Web audio cadence/choppiness is fixed, does
not claim OCEAN01/02 coverage, and does not elevate completion plus frame
diversity to pixel-perfect parity with the PS1 signoff. It records exactly
what is executable now and the stronger oracle needed next.
