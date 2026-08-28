# Optional original sound effects

The core can use user-supplied `sound0.wav` through `sound24.wav` files placed
beside `RESOURCE.MAP` or `RESOURCE.001`. IDs 11 and 13 do not exist in the
original data set and are intentionally skipped. Every file is optional; the
screensaver continues without an effect when its sample is unavailable or
invalid.

## Provenance

The sibling filename convention, 25-slot range, and missing IDs were derived
from Hunter Davis's public
[`huntergdavis/Johnny-Castaway-PS1`](https://github.com/huntergdavis/Johnny-Castaway-PS1)
repository at revision `25c5d84593ac20cbee354eaab7779ab7397d6bbe`,
particularly:

- `src/host/sound.c`, which opens `sound%d.wav` for IDs 0 through 24; and
- `src/platform/ps1/sound_ps1.c`, which documents that SOUND11 and SOUND13 are
  absent from the original game data.

That PS1 repository is GPL-3.0-or-later. The portable loader and Web staging
code in this repository are new GPL-3.0-or-later code based on the documented
interface above. A required `deja` recall query found no prior SFX/Web loader
implementation to reuse.

## Ownership and distribution boundary

No original sound-effect WAV is part of this source repository, generated
release packages, or CI artifacts. Users must supply their own files from a
lawful copy. The optional staging command copies them only into the ignored
`build/web-player/dist/local-content/` test directory.

Staging makes those files downloadable from the development server. Keep the
server on a trusted private network and remove `local-content/` (or rebuild the
Web Player) before sharing the distribution. This project does not grant any
rights to redistribute the original Sierra audio.

## Web integration contract

`scripts/stage-local-web-content.sh` normalizes the exact lowercase sibling
names and clears stale numbered WAVs from earlier staging runs. The Web
launcher fetches the 23 possible files opportunistically, writes each one to
`/home/web_user/retroarch/userdata/content/sound<ID>.wav`, and then starts
RetroArch with `RESOURCE.MAP` in that same directory. A missing or failed
optional WAV request never prevents the content pair from starting.
