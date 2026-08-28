# Reproducible console core builds

The static Johnny Castaway cores for PSP, Vita, PlayStation 2, Nintendo 3DS,
GameCube, Wii, Wii U, and Switch can be built without installing console SDKs on
the host. The dedicated script uses official project images pinned by immutable
repository digest:

```sh
./scripts/build-console-cores.sh --pull --all
```

After the immutable images and hash-pinned PS2 make APK are cached, reproduce without
network access using `./scripts/build-console-cores.sh --offline --all`. Every build
container runs read-only with networking disabled. Publishable builds require a clean
tree; `ALLOW_DIRTY=1` is a labeled local-development escape hatch only.

Docker, Python 3, and host GNU `ar`, `nm`, and `readelf` are required. `--pull`
refreshes the local cache for the exact digests; it does not follow a mutable tag.
Run `./scripts/build-console-cores.sh --list` to list individual target names.
`JOBS` and the repository-relative `CONSOLE_BUILD_DIR` may be overridden.

## Pinned toolchains

These images were resolved and tested on 2026-08-27. The digest selects the amd64
manifest itself, not only its mutable tag or config object.

| Targets | Image | Compiler in tested image |
|---|---|---|
| Switch | `devkitpro/devkita64@sha256:82575ea78651b530b2e232bb3799cfd1fe331514e053d5f724bb4b28191fb79d` | devkitA64 GCC 15.2.0, binutils 2.45.1 |
| Nintendo 3DS | `devkitpro/devkitarm@sha256:15b79ce75822c289538d8153da5fa7aafe5e6adc32ad8a575a197beca0f0761b` | devkitARM GCC 16.1.0, binutils 2.46.0.20260210 |
| GameCube, Wii, Wii U | `devkitpro/devkitppc@sha256:4c919aa26151dd43d88ca28c922d1fe2409579a8ba60ef56517baf1abdfb1a48` | devkitPPC GCC 16.1.0, binutils 2.46.0.20260210 |
| PSP | `ghcr.io/pspdev/pspdev@sha256:c9f1e60e8635d4df5ea246981b7473cbf48a9cf8457c1735f787821a684957f2` | PSP GCC 15.2.0, binutils 2.44 |
| Vita | `vitasdk/vitasdk@sha256:9506538924a2f7d6e2505f919f3db285ceb297de5f57c211e5f60afa4b85ce85` | Vita GCC 15.2.0 |
| PlayStation 2 | `ps2dev/ps2dev@sha256:29f42ffaadc62d2615db4a8c22df933579e31e8f8004546dd84629314802d789` | PS2 GCC 15.2.0; hash-pinned Alpine GNU Make 4.4.1 binary mounted read-only |

The source Dockerfiles and installation policy are maintained by
[devkitPro](https://github.com/devkitPro/docker),
[PSPDEV](https://github.com/pspdev/pspdev),
[VitaSDK](https://github.com/VitaSDK/docker), and
[PS2DEV](https://github.com/ps2dev/ps2dev). RetroArch's static naming and compiler conventions
were checked against the official
[FCEUmm core Makefile](https://github.com/libretro/libretro-fceumm/blob/master/Makefile.libretro),
[Switch guide](https://github.com/libretro/docs/blob/master/docs/development/retroarch/compilation/switch-libnx.md),
and [RetroArch source](https://github.com/libretro/RetroArch).

## Validated outputs

All eight rows compiled with C99, `-Wall -Wextra -Wpedantic -Werror`. The validator
opens every archive member, verifies a single expected ELF architecture throughout,
and requires exactly one definition of each of the 25 libretro API symbols with no
unexpected or undefined `retro_*` symbol.

| Script target | Make platform | Output | ELF machine | 2026-08-27 result |
|---|---|---|---|---|
| `switch` | `libnx` | `build/console/libnx/johnny_castaway_libretro_libnx.a` | AArch64 | strict build and archive validation pass |
| `3ds` | `ctr` | `build/console/ctr/johnny_castaway_libretro_ctr.a` | ARM | strict build and archive validation pass |
| `gamecube` | `ngc` | `build/console/ngc/johnny_castaway_libretro_ngc.a` | PowerPC | strict build and archive validation pass |
| `wii` | `wii` | `build/console/wii/johnny_castaway_libretro_wii.a` | PowerPC | strict build and archive validation pass |
| `wiiu` | `wiiu` | `build/console/wiiu/johnny_castaway_libretro_wiiu.a` | PowerPC | strict build and archive validation pass |
| `psp` | `psp1` | `build/console/psp1/johnny_castaway_libretro_psp1.a` | MIPS R3000 | strict build and archive validation pass |
| `vita` | `vita` | `build/console/vita/johnny_castaway_libretro_vita.a` | ARM | strict build and archive validation pass |
| `ps2` | `ps2` | `build/console/ps2/johnny_castaway_libretro_ps2.a` | MIPS R3000 | strict build and archive validation pass |

These are static core archives, not standalone console applications. A statically
linked RetroArch build normally copies/renames the selected archive to the frontend's
expected name, for example `libretro_psp1.a` or `libretro_wii.a`. Switch, 3DS,
GameCube, Wii, Wii U, Vita, PS2, and PSP now also have pinned installable frontend builders; see
`INSTALLABLE_FRONTENDS.md` and `PSP_PACKAGE.md`.

## Installable frontend packages

`scripts/build-installable-frontends.sh` requires a current archive with deterministic
zero timestamp/UID/GID `ar rcsD` headers and strict member/machine/exact-symbol
validation for each selected target. It records the observed archive SHA-256 after
validation and requires the adjacent commit/version/image-bound
`BUILD-PROVENANCE.txt` manifest, creates fresh isolated worktrees at RetroArch
`96a1b1a9cf3f9166affcfd7df4323aa58d5c281a`, and emits:

| Target | Frontend output | Deterministic identity |
|---|---|---|
| Switch | NRO plus SD-root install ZIP | title `Johnny Castaway`, core version |
| Nintendo 3DS | 3DSX, SMDH, CIA, and install ZIP | product `CTR-H-JCAST`, ID `0x4A430`, explicit numeric version |
| GameCube | DOL plus Swiss-friendly install ZIP | current external libogc, default startup, statically linked Johnny core |
| Wii | DOL plus Homebrew Channel install ZIP | current external libogc, statically linked Johnny core, tracked GPLv3 single-core/no-reexec patch |
| Wii U | uncompressed RPX plus Homebrew Launcher install ZIP | pinned wut `elf2rpl` explicit large-RPX target |
| Vita | normalized VPK and install ZIP | title ID `JCASTAWAY`, title `Johnny Castaway` |
| PlayStation 2 | ELF and directory-layout install ZIP | static core with adjacent `cores`, `info`, and `retroarch` directories |

The independent validator checks package headers/layout, embedded Johnny and current
Story/Core Option markers, legal/provenance files, deterministic ZIP metadata,
byte-identical packaged executables, and absence of original game data. Device/emulator
execution is still a separate gate.

## Frontend link boundary

The core archives above are proven consumable ELF archives, but a frontend compile is
a separate compatibility gate. Link experiments used official RetroArch revision
`96a1b1a9cf3f9166affcfd7df4323aa58d5c281a` in generated, untracked build directories:

- PSP's default `Makefile.psp1` expects an optional prebuilt
  `bootstrap/psp1/kernel_functions.o` absent from a clean checkout. Its supported
  `HAVE_KERNEL_PRX=0` configuration successfully linked this core into a MIPS
  `retroarchpsp.elf` and packaged `EBOOT.PBP`. The ELF contains the Johnny Castaway
  option/content strings. Pinned PPSSPP v1.20.4 completes the 30-second module, GE,
  and framebuffer boot smoke; physical-hardware rendering, input, audio, gameplay,
  performance, and save states remain release gates.
- Wii's bundled libogc callback declaration conflicts with the frontend callback.
  A generated-checkout-only callback compatibility shim passes that point, then the
  bundled libfat fails against current newlib because `struct stat` no longer has
  `st_spare1`, `st_spare2`, or `st_spare3`. Using current external libogc instead
  passes the callback ABI but fails because this pinned frontend still includes the
  removed `ogc/lwp_threads.h` API.

No RetroArch source workaround is carried by this repository. A successful core
archive build is not represented as a device/frontend run: real RetroArch packaging,
content loading, performance, memory pressure, input, audio, and save-state behavior
remain per-device release gates.
