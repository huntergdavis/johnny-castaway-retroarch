# Third-party notices

## GPL engine references

This project is licensed under the GNU General Public License version 3 or later. It
contains or adapts GPLv3/GPL-3.0-or-later material from Johnny Reborn and Wilson Reborn
as detailed in `PROVENANCE.md`. The complete GPLv3 text is in the repository-root
`LICENSE` file. Source files that retain a longer upstream copyright block keep it in
place in addition to SPDX identifiers used by newly written files.

Johnny Reborn copyright notices identify:

> Copyright (C) 2019 Jeremie GUILLAUME

Wilson Reborn is attributed to Wilson Reborn contributors; the reviewed Git history is
authored by André Felício and its workspace declares `GPL-3.0-or-later`.

Hunter Davis's PS1 audio backend was consulted at introducing commit
`1f97b08ca0f48e8a2f2b785acefc3a7fedab3342` in the GPLv3
`huntergdavis/jc_reborn` repository. The new mixer copies no platform backend code; the
design lineage is recorded because its eight-channel limit was deliberately retained.

The caption catalog, scene-explorer metadata, expanded holiday table, and ocean feature
metadata are translated from Hunter Davis's GPLv3/GPL-3.0-or-later PlayStation port at
revision `25c5d84593ac20cbee354eaab7779ab7397d6bbe`. That project states that its caption
text was freshly authored from scene content. The original Sierra/Dynamix media is not
included in these tables.

## CC0 ocean ambience source

The optional ocean design identifies BigSoundBank sound 0266, “Sea: Waves,” published
as CC0/public domain at <https://bigsoundbank.com/sea-waves-s0266.html>. The PS1 port's
derived `OCEAN.VAG` has SHA-256
`b9eeae5a7f42545ad7fe99701c248c07e8b4c0ad0ab17bb86420f36ea97259c2`.
This repository currently contains only its source/license/hash/playback metadata, not
the recording or derived VAG binary. If the asset is added later, this notice must ship
with it.

## libretro API header

`external/libretro-common` is a Git submodule pinned to revision
`09b55b683764d49ae1e640d7db5069931d6d2d40`. The only submodule file currently included
by this core is `include/libretro.h`. That file contains its own MIT license notice and
states:

> Copyright (C) 2010-2024 The RetroArch team

The complete permission and warranty text remains verbatim at the top of the pinned
header and must remain included in redistributions of that header.

## Local RetroArch Web test

The optional generated browser test links this core into RetroArch revision
`96a1b1a9cf3f9166affcfd7df4323aa58d5c281a`, licensed GPLv3. Its launcher/build
behavior follows that revision's `pkg/emscripten/README.md`,
`pkg/emscripten/libretro/libretro.js`, and `Makefile.emscripten`. Generated players
include the complete upstream GPL text.

The generated player packages `ozone/`, `pkg/`, and `sounds/` from retroarch-assets
revision `73106363e14e34c08a5854b4cfbc29f184e3b783`, licensed CC BY 4.0 and attributed
to the Libretro/RetroArch contributors. Its complete license is included in generated
players.

BrowserFS is copyright 2013–2023 John Vilk and other BrowserFS contributors and is
licensed under the MIT license. The build copies `browserfs.min.js` from the pinned
RetroArch tree (validated SHA-256
`a2a2b38cd567dc20cd024e681df55f34f42174c692f553f8350dae171c2b875b`) and ships the
notice stored at `web/licenses/BrowserFS-license.md`. The BrowserFS Emscripten adapter
also contains code under the MIT and University of Illinois/NCSA terms identified in
that notice.

## Evaluated but not incorporated

The reviewed snapshot of `deckarep/Johnny-Castaway-2026-Public` contained no detected
license file. It was used only to evaluate language/runtime suitability; no source was
copied, translated, or linked. The project is therefore not a dependency of this core.

## Original game content

Sierra/Dynamix content is not licensed under this repository's GPL. Users provide their
own original data at runtime. Building or distributing this core does not grant a right
to redistribute the original data, characters, artwork, animation, or audio.
