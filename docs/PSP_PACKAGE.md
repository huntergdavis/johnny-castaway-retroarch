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
```

Launch Johnny Castaway from the PSP game menu. Use RetroArch's content browser to
open a legally owned `RESOURCE.MAP`; its matching `RESOURCE.001` must be in the same
directory. Optional supported `sound<ID>.wav` files also stay beside that resource
pair. Game data is not included in this package.

The package root also contains the GPL license, credits, provenance, third-party
notices, core metadata, and build provenance. `SHA256SUMS` in the surrounding Actions
artifact verifies the prepared install ZIP, raw `EBOOT.PBP`, and unstripped
`retroarchpsp.elf` audit binary.

## Reproduce and validate

The workflow uses the same PSP core already validated by the eight-console archive
build, then links it into pinned RetroArch with the immutable PSPDEV image:

```sh
PSP_CORE_ARCHIVE=build/console/psp1/johnny_castaway_libretro_psp1.a \
  ./scripts/build-psp-frontend.sh
```

Without `PSP_CORE_ARCHIVE`, the script first builds and validates the PSP core itself.
The script requires a MIPS R3000 ELF32 executable, the PBP header, embedded Johnny
Castaway/Core Option strings, a byte-identical packaged PBP, complete legal files,
and no original game-data filenames. It emits:

```text
build/psp-frontend/out/johnny-castaway-psp-frontend.zip
build/psp-frontend/out/EBOOT.PBP
build/psp-frontend/out/retroarchpsp.elf
build/psp-frontend/out/BUILD-PROVENANCE.txt
build/psp-frontend/out/README-PSP.md
build/psp-frontend/out/SHA256SUMS
```

This proves the complete frontend link and package format. A real PSP or PPSSPP run
is still required to validate boot, content browsing, input, audio, performance, and
save-state behavior on the emulated or physical platform.
