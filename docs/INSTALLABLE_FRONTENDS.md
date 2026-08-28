# Installable console frontends

The generated Switch, Nintendo 3DS, GameCube, Wii, Wii U, PlayStation Vita/TV, and
PlayStation 2 packages link the validated Johnny Castaway core into RetroArch revision
`96a1b1a9cf3f9166affcfd7df4323aa58d5c281a`. They are standalone frontend
executables, unlike the `.a` files under `build/console`, which are link inputs only.

None of these packages contains Sierra/Dynamix data. Supply a legally owned
`RESOURCE.MAP` and matching `RESOURCE.001` at runtime. Optional supported
`sound<ID>.wav` siblings remain user-owned data and are never part of the build or CI
artifact.

## Install

### Nintendo Switch

Extract `johnny-castaway-switch-frontend.zip` to the SD-card root. It installs:

```text
switch/JohnnyCastaway/JohnnyCastaway.nro
```

Launch the NRO through a libnx-compatible Homebrew Menu environment, then browse to
the owned `RESOURCE.MAP`. The embedded NACP identity is “Johnny Castaway,” author
“Johnny Castaway contributors,” and the exact core `display_version`.

### Nintendo 3DS

`johnny-castaway-3ds-frontend.zip` contains both supported installation forms:

```text
3ds/JohnnyCastaway/JohnnyCastaway.3dsx
3ds/JohnnyCastaway/JohnnyCastaway.smdh
cia/JohnnyCastaway.cia
```

Copy the `3ds/` tree to the SD-card root for Homebrew Launcher use, or install the CIA
with a lawful custom-firmware package manager. The deterministic application identity
is product code `CTR-H-JCAST`, unique ID `0x4A430`, title “Johnny Castaway,” and the
numeric `display_version` split into CIA major/minor/micro fields. The builder passes
all version fields explicitly; it never uses the pinned makefile's random `shuf`
defaults.

### Nintendo GameCube

Extract `johnny-castaway-gamecube-frontend.zip` to an SD card and launch:

```text
apps/JohnnyCastaway/boot.dol
```

Browse to the DOL in Swiss or an equivalent lawful GameCube loader. This frontend uses
the pinned makefile's supported current-libogc/default-startup configuration:
`EXTERNAL_LIBOGC=1 HAVE_THREADS=0 GX_PTHREAD_LEGACY=0 BIG_STACK=0`. GameCube has no
salamander/core switching; the Johnny core is statically linked directly into the DOL.
Browse to the legally owned resource pair after launch.

### Nintendo Wii

Extract `johnny-castaway-wii-frontend.zip` to an SD-card root. The Homebrew Channel
layout is:

```text
apps/JohnnyCastaway/boot.dol
apps/JohnnyCastaway/meta.xml
```

Launch Johnny Castaway from the Homebrew Channel and browse to the owned resource
pair on SD or USB. This is deliberately a statically linked single-core frontend:
the GPLv2-only upstream Wii app booter and RetroArch re-exec/core-switch path are not
compiled into this GPLv3 package. Normal RGUI menus, the Johnny Core Options, content
browsing, GX video/audio/input, configuration, save states, and the linked core remain.
The exact external-libogc build flags are `EXTERNAL_LIBOGC=1 HAVE_RARCH_EXEC=0
HAVE_THREADS=0 HAVE_NETWORKING=0 HAVE_CHEEVOS=0 HAVE_WIIUSB_HID=0
GX_PTHREAD_LEGACY=0 BIG_STACK=0`, with legacy socket/achievements defines removed.
Networking, RetroAchievements, and thread-dependent USB HID are therefore unavailable;
Wiimote, Classic Controller, and GameCube-pad input use the retained GX joypad path.
The tracked compatibility patch and its SHA-256 ship in the package and provenance.
`icon.png` is intentionally omitted because no owned/open 128×48 Johnny-specific Wii
icon is available; Homebrew Channel accepts it as optional metadata.

### Nintendo Wii U

Extract `johnny-castaway-wiiu-frontend.zip` to an SD-card root. It installs the
Homebrew Launcher application at:

```text
wiiu/apps/JohnnyCastaway/JohnnyCastaway.rpx
wiiu/apps/JohnnyCastaway/meta.xml
```

Launch it through a lawful Wii U Homebrew Launcher environment and browse to the
owned resource pair. Pinned wut `elf2rpl` creates the explicit uncompressed
`retroarch_wiiu.large.rpx` target; the package is larger than a compressed RPX but has
no extra build-tool dependency. The audit ELF is checked for the linked Johnny core
and current Story/Core Option strings.
The deterministic metadata supplies the title, version, author, and description.
`icon.png` is intentionally omitted because this repository does not contain an
owned/open 256×96 Johnny-specific icon; Homebrew Launcher can display the app without
that optional artwork.

### PlayStation Vita/TV

Install `JohnnyCastaway.vpk` from `johnny-castaway-vita-frontend.zip` with the normal
Vita homebrew package installer. Its unique title ID is `JCASTAWAY` and its title is
“Johnny Castaway,” so it does not overwrite an upstream RetroArch installation using
title ID `RETROARCH`. Browse to the owned resource pair after launch.

### PlayStation 2

Extract `johnny-castaway-ps2-frontend.zip` together and launch:

```text
JohnnyCastaway/retroarch_ps2.elf
```

Use FreeMCBoot/uLaunchELF, an equivalent lawful homebrew environment, or an emulator.
Keep the supplied `cores`, `info`, and `retroarch` directories beside the ELF. Put the
owned `RESOURCE.MAP`, `RESOURCE.001`, and any optional supported WAV siblings together
under `JohnnyCastaway/retroarch/`. The core is statically linked; `cores/` is retained
because the pinned frontend expects that directory layout.

## Reproduce

Build all seven frontends from strictly validated current core archives:

```sh
./scripts/build-console-cores.sh switch 3ds gamecube wii wiiu vita ps2
INSTALLABLE_FRONTEND_CORE_DIR=build/console \
  ./scripts/build-installable-frontends.sh --all
```

Use `--pull` to resolve the immutable image digests or `--offline` to require the
pinned RetroArch commit, core manifests, images, and PS2 make cache to be present
locally; build containers always run with networking disabled. Individual target names
are accepted. Publishable builds require a clean Git tree. `ALLOW_DIRTY=1` exists only
for local development, records `dirty-developer-mode`, and deliberately skips the
clean-commit legal-byte gate so its packages cannot be treated as release evidence.
Outputs are placed below:

```text
build/installable-frontends/out/switch/
build/installable-frontends/out/3ds/
build/installable-frontends/out/gamecube/
build/installable-frontends/out/wii/
build/installable-frontends/out/wiiu/
build/installable-frontends/out/vita/
build/installable-frontends/out/ps2/
```

Each target directory contains the install ZIP, raw executable/package, an audit ELF
where useful, exact build provenance, and `SHA256SUMS`. ZIP member order, timestamps,
permissions, and compression are normalized to `SOURCE_DATE_EPOCH`. Vita's VPK is
also normalized. The 3DS metadata fields are fixed explicitly. Core archives use
deterministic `ar rcsD` headers, then pass strict member/machine/exact-symbol validation;
their per-target `BUILD-PROVENANCE.txt` manifests bind the observed SHA-256 to the
Johnny commit, version, clean/dirty state, platform, and immutable SDK image. The
frontend requires that exact manifest and includes it as `CORE-BUILD-PROVENANCE.txt`.

The validator checks NRO/3DSX/SMDH/CIA/DOL/RPX/VPK/ELF format identities, platform
metadata, loaded executable sections/segments, embedded Johnny and current Story/Core
Option strings, install layout, byte-identical packaging, legal/provenance files,
deterministic ZIP timestamps, install-ZIP member names, and member names inside nested
Vita VPK packages for absence of original data. Wii additionally requires the linked
single-core identity, exact patch bytes/hash, no-reexec flags, PowerPC audit ELF, and
Homebrew Channel metadata.

These checks prove compilation, frontend linkage, package structure, metadata, and
core identity. Real device or emulator execution remains required for boot, content
browsing, video, audio, input, menu/Core Options, performance, persistence, and save
states on each platform.
