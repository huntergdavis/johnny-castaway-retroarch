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

For a private development machine, stage a user-owned pair once and the page will
start it automatically on every refresh:

```sh
./scripts/stage-local-web-content.sh /path/to/johnny-data
./scripts/serve-web.sh --bind 0.0.0.0 --port 8000
```

The generated, ignored files live in `build/web-player/dist/local-content/`.
Rebuilding the Web Player removes them, after which the staging command may be run
again. If the source directory also contains the optional original
`sound0.wav` through `sound24.wav` siblings, the staging command copies the
available files (IDs 11 and 13 are intentionally absent) and the launcher
installs them beside the content before starting the core. Missing sound files
do not block startup. The file picker remains available whenever no staged pair
is present.

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
- Locally staged, optional `sound<ID>.wav` files are discovered and installed
  beside the resource pair. They remain user-supplied data and are never part
  of the generated Web distribution.
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

The optional staging command changes that privacy boundary: it copies the
original pair and any available original sound-effect WAVs into the served
directory so the browser can fetch and start them automatically. Those files
are ignored by Git and omitted from generated builds and CI artifacts. The MAP
and archive are also rejected by `tools/check_web_dist.py`; do not treat that
checker as a substitute for removing all staged user data. Anyone who can reach
the server can fetch the staged files, so use staging only on a trusted private
network, never a public host. Remove `build/web-player/dist/local-content/` or
rebuild the player before sharing it.

The original-audio provenance and distribution boundary are documented in
[`OPTIONAL_ORIGINAL_AUDIO.md`](OPTIONAL_ORIGINAL_AUDIO.md).

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
HTML dependencies, and absence of original Johnny resource archives.

### Automated real-browser test

After building the distribution, run the optional Firefox smoke test:

```sh
python3 tools/web_smoke_test.py
```

The script uses the W3C WebDriver protocol directly, so no Python browser
package is required. It needs Firefox and geckodriver; on a headless Linux
machine it also needs `xvfb-run` and a software-capable Mesa installation. If
those browser tools are absent the default command reports `SKIP` and exits
successfully. Use this in an environment where the browser is mandatory:

```sh
python3 tools/web_smoke_test.py --require-browser
```

The test serves `build/web-player/dist/` on a random loopback port, starts a
real Firefox process, loads the JS and WebAssembly runtime, selects two
embedded synthetic files, explicitly selects the fixture-backed `fishing1`
chapter, and asserts that RetroArch:

- registers the core's version 2 option set;
- indexes all five synthetic resources;
- reports the core's 640x480 geometry;
- enables the menu button without a page error or unhandled rejection; and
- renders stable, distinct gameplay, Quick Menu, selected Core Options, Core
  Options category, and Story top/end screenshots after deterministic keyboard
  navigation.

Evidence is written to `build/web-smoke/`: `result.json`,
`geckodriver.log`, `game.png`, `menu.png`, `core-options-selected.png`,
`core-options.png`, `story-options-top.png`, and `story-options-bottom.png`.
The embedded 61-byte MAP and 1,126-byte archive are exact, checksum-verified outputs of
`make_synthetic_content()` in `tests/test_libretro.c`. They contain only the
project’s tiny generated palette, screens, and scripts. The automated test
does not find, read, copy, or require original Johnny Castaway data.

For the local authentic release gate, explicitly point the same runner at a
user-owned source directory instead of staging or embedding those files:

```sh
python3 tools/web_smoke_test.py \
  --content-dir /path/to/johnny-data \
  --artifacts build/web-smoke-authentic-options \
  --require-browser --timeout 90
```

This leaves Automatic Story active, navigates to its Calendar control, selects
Simulated Calendar, and captures the expanded Story menu at its top and bottom.
The source files are uploaded only to the test browser’s in-memory filesystem;
all screenshots, logs, and result metadata remain in ignored `build/` output.

After using `scripts/stage-local-web-content.sh`, test the page's automatic
server-local path without a browser file upload:

```sh
python3 tools/web_smoke_test.py \
  --staged-local-content \
  --artifacts build/web-smoke-staged-local \
  --require-browser --timeout 180
```

This mode permits only the resource pair and the 23 supported WAV names beneath
`local-content/`, validates an isolated copy of the distribution with that private
directory excluded, and then requires Firefox to fetch and start the staged pair plus
all 23 WAVs automatically. It does not relax `tools/check_web_dist.py`: run that checker
only on a pristine distribution, and remove or rebuild away `local-content/` before
publishing or sharing the Web package.

Firefox's native headless mode did not expose a usable WebGL context in the
development environment. When no display is already set, the runner therefore
uses Xvfb and Mesa software rendering automatically. `--no-xvfb` is available
for browser installations whose native headless WebGL is known to work.

## Reproducibility, credits, and licenses

The default revisions are pinned:

- RetroArch `96a1b1a9cf3f9166affcfd7df4323aa58d5c281a`
- retroarch-assets `73106363e14e34c08a5854b4cfbc29f184e3b783`

Every generated distribution contains `BUILD-PROVENANCE.txt`, `CREDITS.md`,
`docs/PROVENANCE.md`, `docs/THIRD_PARTY_NOTICES.md`, the project license, CC0 ocean,
and upstream license texts, and `WEB_PLAYER_NOTICE.md`. Together they record
the complete community lineage and the exact upstream files from which the
launcher and build process were derived.

Primary sources:

- [RetroArch Web Player README](https://github.com/libretro/RetroArch/blob/96a1b1a9cf3f9166affcfd7df4323aa58d5c281a/pkg/emscripten/README.md)
- [RetroArch Web Player loader](https://github.com/libretro/RetroArch/blob/96a1b1a9cf3f9166affcfd7df4323aa58d5c281a/pkg/emscripten/libretro/libretro.js)
- [RetroArch Emscripten Makefile](https://github.com/libretro/RetroArch/blob/96a1b1a9cf3f9166affcfd7df4323aa58d5c281a/Makefile.emscripten)
- [RetroArch GPL-3.0 license](https://github.com/libretro/RetroArch/blob/96a1b1a9cf3f9166affcfd7df4323aa58d5c281a/COPYING)
- [retroarch-assets CC BY 4.0 license](https://github.com/libretro/retroarch-assets/blob/73106363e14e34c08a5854b4cfbc29f184e3b783/COPYING)
- [BrowserFS MIT notice](https://github.com/jvilk/BrowserFS/blob/76fd5122fcf3ad6bff3315550aafb041cfb6a72e/license.md)
- [Mozilla geckodriver usage](https://firefox-source-docs.mozilla.org/testing/geckodriver/Usage.html)
- [W3C WebDriver Recommendation](https://www.w3.org/TR/webdriver2/)

The required `deja` recall queries found no prior web-testing implementation
or Snap Firefox/Xvfb fix to reuse. The test reuses the repository's synthetic
fixture format from `tests/test_libretro.c`; the provenance above records the
external primary sources used for the browser protocol and frontend.
