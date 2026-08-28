# Browser testing

The project includes a local-only RetroArch Web workflow. It links the core's
Emscripten archive into the official RetroArch frontend, packages a small set
of official menu assets, and serves it from `127.0.0.1`.

## Fast path

Requirements: `emcc`, `emmake`, `git`, `make`, `python3`, and `zip`.

```sh
./scripts/build-web-player.sh
./scripts/serve-web.sh --open
```

Then:

1. Choose `RESOURCE.MAP` and `RESOURCE.001` together.
2. Select **Start Johnny**.
3. Select **RetroArch menu** to inspect core options, remaps, video settings,
   audio settings, and runtime information.
4. Stop the server with Ctrl-C.

The first build compiles RetroArch and its Emscripten system libraries, so it
can take several minutes. The cache and generated frontend live under
`build/web-player/`; later builds are incremental.

To use more build jobs or a different port:

```sh
JOBS=8 ./scripts/build-web-player.sh
./scripts/serve-web.sh --port 9000 --open
```

## What works

- `build/emscripten/johnny_castaway_libretro_emscripten.bc` is linked into a
  real RetroArch Web `johnny_castaway_libretro.js` and `.wasm` pair.
- Both resource files are selected at once and normalized to
  `/home/web_user/retroarch/userdata/content/RESOURCE.MAP` and
  `RESOURCE.001` in BrowserFS. This satisfies the core's full-path and sibling
  archive lookup behavior.
- The content launches directly, without requiring navigation through
  RetroArch's content browser.
- The RetroArch menu remains available through the page button, including all
  options registered by the core.
- The server supplies `application/wasm`, COOP, COEP, and CORP headers, so the
  same server is suitable if a future core build enables WebAssembly threads.

The existing `.bc` file alone cannot be opened in a browser and cannot be
dynamically added to <https://web.libretro.com/>. Emscripten cores are
statically linked into a per-core RetroArch `.js/.wasm` application. The build
script automates that official process.

## Privacy and original data

The server binds to `127.0.0.1` by default. The custom page has no CDN,
analytics, or other network dependencies. JavaScript `File.arrayBuffer()`
reads the two selected files directly into the browser's in-memory filesystem;
the HTTP server receives only requests for the player, assets, JavaScript, and
WebAssembly. Refreshing or closing the page discards the game data.

Do not put original game files under `web/`, `build/web-player/dist/`, or any
other served directory. `tools/check_web_dist.py` fails if either original
resource filename is found in the generated distribution.

Binding to another interface with `--bind 0.0.0.0` exposes the player to the
local network and is intentionally not the default.

## Automated checks

Validate an assembled distribution without starting a browser:

```sh
python3 tools/check_web_dist.py build/web-player/dist
```

Smoke-test the server from another terminal:

```sh
./scripts/serve-web.sh --port 8080
curl -fsSI http://127.0.0.1:8080/johnny_castaway_libretro.wasm
curl -fsS http://127.0.0.1:8080/ >/dev/null
```

The checker verifies required files, WebAssembly magic, ZIP contents, local
HTML dependencies, and absence of original Johnny resource archives. A full
runtime test still requires a browser and the user's own lawful data files.

## Reproducibility, credits, and licenses

The default revisions are pinned:

- RetroArch `96a1b1a9cf3f9166affcfd7df4323aa58d5c281a`
- retroarch-assets `73106363e14e34c08a5854b4cfbc29f184e3b783`

Every generated distribution contains `BUILD-PROVENANCE.txt`, the project, CC0 ocean, and
upstream license texts, and `WEB_PLAYER_NOTICE.md`. The notice identifies the
exact upstream files from which the launcher and build process were derived.

Primary sources:

- [RetroArch Web Player README](https://github.com/libretro/RetroArch/blob/96a1b1a9cf3f9166affcfd7df4323aa58d5c281a/pkg/emscripten/README.md)
- [RetroArch Web Player loader](https://github.com/libretro/RetroArch/blob/96a1b1a9cf3f9166affcfd7df4323aa58d5c281a/pkg/emscripten/libretro/libretro.js)
- [RetroArch Emscripten Makefile](https://github.com/libretro/RetroArch/blob/96a1b1a9cf3f9166affcfd7df4323aa58d5c281a/Makefile.emscripten)
- [RetroArch GPL-3.0 license](https://github.com/libretro/RetroArch/blob/96a1b1a9cf3f9166affcfd7df4323aa58d5c281a/COPYING)
- [retroarch-assets CC BY 4.0 license](https://github.com/libretro/retroarch-assets/blob/73106363e14e34c08a5854b4cfbc29f184e3b783/COPYING)
- [BrowserFS MIT notice](https://github.com/jvilk/BrowserFS/blob/76fd5122fcf3ad6bff3315550aafb041cfb6a72e/license.md)

The required `deja` recall query found no prior web-testing implementation to
reuse; the provenance above records the primary sources used for this work.
