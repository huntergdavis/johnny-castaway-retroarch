# Credits

Johnny Castaway exists because of the original artists, designers, engineers, and the
community members who later documented its DGDS formats and behavior. This project is
an independent, unofficial libretro port and is not affiliated with or endorsed by
Sierra, Dynamix, or the current holders of their rights.

## Original production

*Screen Antics: Johnny Castaway* was developed by Jeff Tunnell Productions, a division
of Dynamix, and published by Sierra On-Line. Historical credits compiled by Wilson
Reborn identify:

- Jeff Tunnell — producer and project originator
- Shawn Bird — character design
- Chris Cole — lead designer
- Brian Hahn — art direction and visual gags
- Sherry Wheeler — animation

The Johnny Castaway names, characters, artwork, animation, and sounds remain property
of their respective rights holders. This repository distributes none of that content.

## Open-source engine lineage

- Jérémie Guillaume (`jno6809`) created
  [Johnny Reborn](https://github.com/jno6809/jc_reborn), the GPLv3 C/SDL2 gameplay
  blueprint used by this port. Its scene table, scheduler, walking, island behavior,
  and TTM/ADS work made a portable core practical.
- André Felício and the Wilson Reborn contributors created
  [Wilson Reborn](https://github.com/antigerme/wilson-reborn), the GPLv3-or-later Rust
  engine used here as the current behavioral and regression-test reference.
- Hunter Davis maintains this libretro port and the earlier
  [jc_reborn fork](https://github.com/huntergdavis/jc_reborn),
  [PlayStation port](https://github.com/huntergdavis/johnny-castaway-ps1), and
  [Dreamcast port](https://github.com/huntergdavis/johnny_dreamcastaway) whose memory,
  platform, and validation work informed this design.
- The PlayStation port's freshly authored closed-caption corpus, 63-scene explorer
  metadata, 36-holiday calendar, and ocean-ambience feature design were translated into
  portable catalogs for this core. Its centered, bottom-band caption presentation
  informed this port's independently drawn 5x7 software renderer. Its automatic/off/
  manual holiday menu and title/date preview behavior informed this core's independently
  written, asset-free holiday overlay; no `HOLIDAY.BMP` artwork was brought over. The ocean loop
  pipeline was introduced by Hunter
  Davis in PS1 commit `bb32de68aad33ecd1b8a7fbdc0a9278a35971238`, co-authored there
  with Claude Opus 4.7, and completed in commit
  `c194f1bf5716b460ba7276dcb8116b24a690c636`.
- The automatic story sequence, five software fade shapes, and persistent island/walk
  compositor were behaviorally derived from the PlayStation port's GPLv3 story,
  graphics, island, and walk implementations at revision
  `25c5d84593ac20cbee354eaab7779ab7397d6bbe`. The libretro implementations replace
  blocking/platform drawing with new deterministic, bounded C99 state machines; the
  exact copied or translated data tables remain identified separately in
  `docs/PROVENANCE.md`.
- The optional `sound0.wav` through `sound24.wav` sibling convention (with IDs 11 and
  13 absent) also follows that PlayStation revision. The portable VFS/stdio loader is
  new code. No original Sierra/Dynamix WAV is bundled, licensed, or redistributable by
  this project; users may supply those files from their own lawful copy.
- Ralph Caraveo (`deckarep`) created the 2026
  [Go/Raylib port](https://github.com/deckarep/Johnny-Castaway-2026-Public). It was
  evaluated for this core; its runtime/toolchain tradeoffs informed the C99 decision,
  but no Go source is copied here.

Johnny Reborn also explicitly credits the earlier research on which it depends:

- Hans Milling (`nivs1978`) —
  [Johnny Castaway Open Source / JCOS](https://github.com/nivs1978/Johnny-Castaway-Open-Source),
  pioneering resource decoding and TTM/ADS research
- Alexandre Fontoura (`xesf`) — [castaway](https://github.com/xesf/castaway) and
  `dgds-viewer`, web implementations and DGDS inspection tools
- Guido — xBaK research that helped decode TTM and ADS commands
- Vasco Costa and ScummVM contributors — the ScummVM DGDS engine and format research
- Jeff Tunnell; Kevin and Liam Ryan; Jaap; and Gregori — historical engine/resource and
  decompression information acknowledged by JCOS and Johnny Reborn
- Maria Bare and later curators of [johnny-castaway.com](https://johnny-castaway.com/)
  for preserving the community behavior catalog
- The Sierra Chest archive for historical screenshots and behavior references

## libretro

Thanks to the RetroArch and libretro contributors for the frontend-independent API,
Core Options specifications, reference headers, and platform ecosystem. The pinned
dependency and its license are recorded in `docs/THIRD_PARTY_NOTICES.md`.

The local browser test additionally uses the official RetroArch Emscripten frontend,
RetroArch menu assets, and BrowserFS. John Vilk and the BrowserFS contributors created
the in-browser filesystem used by the official frontend. Exact revisions and notices
ship with the generated player and are recorded below.

The reproducible console builds use the public toolchain work of the devkitPro,
PSPDEV/PSPSDK, VitaSDK, and PS2DEV contributors. Their pinned build-only container
sources and immutable image digests are recorded in `docs/PROVENANCE.md` and
`docs/CONSOLE_BUILDS.md`; those SDKs and Docker images are not copied into this
repository or distributed as part of the core.

## Additional open media and algorithms

- BigSoundBank sound 0266, [“Sea: Waves”](https://bigsoundbank.com/sea-waves-s0266.html),
  is the CC0/public-domain recording from which the PS1 port made its seamless ocean
  loop. The exact PS1-derived VAG is embedded in this core; its checksum and dedicated
  redistribution notice are in `docs/licenses/BigSoundBank-0266-CC0.md`.
- The expanded holiday calendar uses the public Meeus/Jones/Butcher Gregorian Easter
  calculation, following the attribution in the PlayStation port. Hunter Davis authored
  the referenced PS1 holiday work in commits `d45de0f08`, `cc2ef4d69`, and `0a3ef05ec`.

Exact repository revisions and the distinction between copied data, translated code,
behavioral references, and independently written code are recorded in
`docs/PROVENANCE.md`.
