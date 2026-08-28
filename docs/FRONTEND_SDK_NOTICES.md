# Console frontend SDK and runtime notices

The installable console packages are GPLv3 combined works built from Johnny Castaway
and pinned [RetroArch](https://github.com/libretro/RetroArch) revision
`96a1b1a9cf3f9166affcfd7df4323aa58d5c281a`. Generated packages include the complete
RetroArch/project GPLv3 text, project credits, provenance, third-party notices, and the
license trees supplied by the relevant immutable SDK image where available.

The SDK images are build inputs and are not redistributed. Some SDK and port libraries
are statically linked by the official RetroArch platform makefiles, so their upstream
terms still apply to the resulting executable:

- Switch uses
  `devkitpro/devkita64@sha256:82575ea78651b530b2e232bb3799cfd1fe331514e053d5f724bb4b28191fb79d`,
  [switchbrew/libnx](https://github.com/switchbrew/libnx/tree/7644c9b26099aa2d2145bc72a21ee24190e92085)
  v4.12.0 (ISC), and the Switch
  portlibs named by pinned `Makefile.libnx`. The image's complete
  `/opt/devkitpro/portlibs/switch/licenses` tree is copied under the collision-safe
  `sdk/switch-portlibs/` package directory.
- Nintendo 3DS uses
  `devkitpro/devkitarm@sha256:15b79ce75822c289538d8153da5fa7aafe5e6adc32ad8a575a197beca0f0761b`,
  [devkitPro/libctru](https://github.com/devkitPro/libctru/tree/36fe1ada5b7ebe53ba4decda36d764a55f8fefb6)
  v2.7.0 (Zlib), and the banner/makerom
  tools already shipped by the pinned RetroArch source. The image's complete
  `/opt/devkitpro/portlibs/3ds/licenses` tree is copied under `sdk/3ds-portlibs/`.
- GameCube uses
  `devkitpro/devkitppc@sha256:4c919aa26151dd43d88ca28c922d1fe2409579a8ba60ef56517baf1abdfb1a48`
  and current [devkitPro/libogc](https://github.com/devkitPro/libogc) through the
  pinned frontend's supported external-libogc mode. `/opt/devkitpro/libogc/LICENSE`
  and the `portlibs/gamecube/licenses`, `portlibs/gamecube/share/licenses`, and
  `portlibs/ppc/licenses` trees are copied under distinct `sdk/` subdirectories.
- Wii uses the same immutable devkitPPC image and current
  [devkitPro/libogc](https://github.com/devkitPro/libogc). The pinned RetroArch
  revision is altered by the packaged `retroarch-wii-single-core.patch`: re-exec and
  the GPLv2-only `wii/app_booter` are omitted, while the statically linked Johnny core,
  RGUI, content browser, and GX runtime remain. The patch is GPLv3-or-later, its exact
  SHA-256 is recorded in build provenance, and its bytes ship under `docs/patches/`.
  `/opt/devkitpro/libogc/LICENSE`, `portlibs/wii/licenses`, and
  `portlibs/ppc/licenses` are copied under distinct `sdk/` subdirectories. The pinned
  image has no `portlibs/wii/share/licenses` directory.
  Networking, RetroAchievements, and thread-dependent Wii USB HID are explicitly
  disabled to avoid legacy newlib/libogc interfaces; these optional exclusions are
  recorded verbatim in package provenance.
- Wii U uses the same immutable devkitPPC image and the public
  [devkitPro/wut](https://github.com/devkitPro/wut/tree/2016e429c16dab366d902455b43f21ccb520e23d)
  v1.9.1 (Zlib) runtime and `elf2rpl` selected by
  pinned `Makefile.wiiu`. The explicit uncompressed large-RPX target needs no external
  compressor. The `portlibs/wiiu/licenses`, `portlibs/wiiu/share/licenses`, and
  `portlibs/ppc/licenses` trees are copied under distinct `sdk/` subdirectories.
- Vita uses
  `vitasdk/vitasdk@sha256:9506538924a2f7d6e2505f919f3db285ceb297de5f57c211e5f60afa4b85ce85`,
  [VitaSDK](https://github.com/vitasdk),
  [vitaGL](https://github.com/Rinnegatamante/vitaGL), and
  [vitaShaRK](https://github.com/Rinnegatamante/vitaShaRK). The latter two declare
  LGPL-3.0; their corresponding source is public at those links. This packaged ledger
  supplies the explicit vitaGL/vitaShaRK notice and links because their license texts
  are absent from the image tree. Available Vita SDK runtime license files are copied
  from `arm-vita-eabi/share/licenses` under `sdk/vita-runtime/`.
- PlayStation 2 uses
  `ps2dev/ps2dev@sha256:29f42ffaadc62d2615db4a8c22df933579e31e8f8004546dd84629314802d789`
  and the public [PS2DEV](https://github.com/ps2dev/ps2dev),
  [PS2SDK](https://github.com/ps2dev/ps2sdk), and
  [gsKit](https://github.com/ps2dev/gsKit) projects selected by pinned
  `Makefile.ps2`. The PS2SDK `LICENSE`, `ps2sdk/ports/share/licenses`, and every
  `LICENSE*`/`COPYING*` file below `ps2sdk/ports/share/doc` are copied under separate
  `sdk/ps2sdk/`, `sdk/ps2-port-licenses/`, and `sdk/ps2-port-docs/` directories.

The immutable image digest is the executable build-environment identity. Container
source revisions and compiler versions are recorded in `PROVENANCE.md` and
`CONSOLE_BUILDS.md`. No SDK source, container layer, original Johnny data, or
third-party media is copied into this Git repository.

Exact libnx, libctru, and wut notices are committed under `docs/licenses/frontend/`
and copied into every install package because the pinned images do not expose all
three texts through their portlib license trees.
