# PSP frontend package

The `johnny-castaway-psp-frontend` Actions artifact is the PSP deliverable that can
be installed and launched. It is separate from
`johnny_castaway_libretro_psp1.a`, which is only a static link input for frontend
developers.

## Install

Extract `johnny-castaway-psp-frontend.zip` and copy its `PSP/` directory to the root
of a PSP Memory Stick. The executable is installed as:

```text
PSP/GAME/JohnnyCastaway/EBOOT.PBP
PSP/GAME/JohnnyCastaway/INFO/johnny_castaway_libretro.info
```

The second file is the matching core metadata at the `INFO` directory selected by
RetroArch's PSP frontend. It supplies the frontend's core name, supported content
extensions, and no-content/save-state declarations.

Launch Johnny Castaway from the PSP game menu. Use RetroArch's content browser to
open a legally owned `RESOURCE.MAP`; its matching `RESOURCE.001` must be in the same
directory. Optional supported `sound<ID>.wav` files also stay beside that resource
pair. Game data is not included in this package.

The package root also contains the GPL license, credits, provenance, third-party
notices, core metadata, frontend build provenance, and the exact validated static
core's `CORE-BUILD-PROVENANCE.txt`. The PSPSDK runtime license is copied verbatim from
`/usr/local/pspdev/psp/share/licenses/pspsdk/LICENSE` in the pinned PSPDEV image as
`docs/licenses/PSPSDK-LICENSE`; its expected SHA-256 is
`2a72b3d563b8e080dd2be9a963f44c8396ca615421833d3cffb6d126101c1c82`.
This is the relevant PSPSDK notice, not a claim that unrelated packages in the SDK
image are redistributed. `SHA256SUMS` in the surrounding Actions artifact verifies
the prepared install ZIP, raw `EBOOT.PBP`, unstripped `retroarchpsp.elf` audit binary,
and both provenance records.

## Reproduce and validate

The workflow uses the same PSP core already validated by the eight-console archive
build, then links it into pinned RetroArch with the immutable PSPDEV image:

```sh
PSP_CORE_ARCHIVE=build/console/psp1/johnny_castaway_libretro_psp1.a \
  ./scripts/build-psp-frontend.sh
```

Without `PSP_CORE_ARCHIVE`, the script first builds and validates the PSP core itself.
An overridden archive must have an adjacent `BUILD-PROVENANCE.txt` emitted by
`scripts/build-console-cores.sh`. The record must exactly bind target `psp`, platform
`psp1`, archive name and hash, current Johnny commit, version and tree state, core
metadata hash, immutable PSPDEV image, and deterministic `ar rcsD` metadata. The
frontend package preserves that record under the unambiguous
`CORE-BUILD-PROVENANCE.txt` name and binds its SHA-256 from the frontend provenance.

The normal mode may fetch the pinned RetroArch commit or pull the immutable PSPDEV
image only when a required local object is absent. `--pull` explicitly refreshes
both. After one connected preparation, `--offline` performs no Git fetch or image
pull and fails clearly if either pinned object is absent:

```sh
./scripts/build-psp-frontend.sh --pull
./scripts/build-psp-frontend.sh --offline
```

Each run resets the generated RetroArch checkout to the exact pinned commit and
removes ignored and untracked build residue before linking. Build containers have a
read-only root filesystem, no network, no Linux capabilities, no-new-privileges, a
temporary `/tmp`, and only the generated checkout mounted read/write.

Publishable builds refuse any tracked or untracked Johnny source-tree change. For a
local developer test of deliberate uncommitted work, opt in explicitly; the output is
marked `dirty-developer-mode`, and the release assembler's required `(clean)`
provenance check rejects it:

```sh
ALLOW_DIRTY=1 ./scripts/build-psp-frontend.sh --offline
```

The script captures the exact HEAD, `0.1.2` metadata version, metadata hash, and a
full tracked/untracked tree fingerprint before building and rejects any drift at the
end. It requires a MIPS R3000 ELF32 executable, the PBP header, the linked `0.1.2`
version, all 21 Core Option keys, a byte-identical packaged PBP, exact legal and
provenance files, and no original game-data filenames. The install ZIP is made with
`tools/make_deterministic_zip.py`; every member is a sorted, safe canonical path with
the `SOURCE_DATE_EPOCH` timestamp and Unix mode `0100644`, and the builder independently
checks the exact member set, modes, timestamps, and integrity. It emits:

```text
build/psp-frontend/out/johnny-castaway-psp-frontend.zip
build/psp-frontend/out/EBOOT.PBP
build/psp-frontend/out/retroarchpsp.elf
build/psp-frontend/out/BUILD-PROVENANCE.txt
build/psp-frontend/out/CORE-BUILD-PROVENANCE.txt
build/psp-frontend/out/README-PSP.md
build/psp-frontend/out/SHA256SUMS
```

This proves the complete frontend link and package format. The separate
`scripts/test-psp-ppsspp.sh` gate validates EBOOT boot, module startup, and active GE
and framebuffer calls in pinned PPSSPP. Real PSP hardware remains required to validate
rendered menus/gameplay, content browsing, controller input, audio, performance, and
save-state behavior on the physical platform.
