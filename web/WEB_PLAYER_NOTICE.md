# Web Player credits, licenses, and provenance

This generated browser test is a RetroArch Web frontend linked with the
Johnny Castaway libretro core. It does not contain Sierra/Dynamix game data.

## RetroArch

- Project: <https://github.com/libretro/RetroArch>
- Revision validated here: `96a1b1a9cf3f9166affcfd7df4323aa58d5c281a`
- License: GNU General Public License version 3
- Build recipe derived from:
  [`pkg/emscripten/README.md`](https://github.com/libretro/RetroArch/blob/96a1b1a9cf3f9166affcfd7df4323aa58d5c281a/pkg/emscripten/README.md)
- `web/jc-web-player.js` derives its Emscripten module initialization,
  BrowserFS mount layout, fake static-core path, and command bridge from:
  [`pkg/emscripten/libretro/libretro.js`](https://github.com/libretro/RetroArch/blob/96a1b1a9cf3f9166affcfd7df4323aa58d5c281a/pkg/emscripten/libretro/libretro.js)
- The generated `johnny_castaway_libretro.js` and
  `johnny_castaway_libretro.wasm` are produced by RetroArch's
  `Makefile.emscripten`. The complete GPL text is copied into generated builds
  as `licenses/RetroArch-GPL-3.0`.

The local launcher is intentionally smaller than the stock Web Player UI. It
keeps the same RetroArch runtime and exposes a direct two-file content picker,
plus menu, reset, and fullscreen commands.

## RetroArch assets

- Project: <https://github.com/libretro/retroarch-assets>
- Revision validated here: `73106363e14e34c08a5854b4cfbc29f184e3b783`
- Material used: `ozone/`, `pkg/`, and `sounds/`
- License: Creative Commons Attribution 4.0 International
- Attribution: the Libretro/RetroArch contributors listed by the upstream
  project

The complete CC BY 4.0 text is copied into generated builds as
`licenses/retroarch-assets-CC-BY-4.0`.

## BrowserFS

- Project: <https://github.com/jvilk/BrowserFS>
- Vendored browser build copied verbatim from RetroArch's pinned revision:
  `pkg/emscripten/libretro/browserfs.min.js`
- SHA-256 at that revision:
  `a2a2b38cd567dc20cd024e681df55f34f42174c692f553f8350dae171c2b875b`
- License: MIT; copyright 2013–2023 John Vilk and other BrowserFS contributors

The required notice is included as `licenses/BrowserFS-license.md`.

## Johnny Castaway data

Johnny Castaway and its original art, scripts, sound, and resource archives
remain the property of their respective rights holders. No original
`RESOURCE.MAP` or `RESOURCE.001` is committed, copied into the generated web
distribution, or uploaded by the local server. Users must provide their own
lawfully obtained files.
