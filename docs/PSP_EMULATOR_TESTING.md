# PSP emulator smoke testing

`scripts/test-psp-ppsspp.sh` boots the installable Johnny Castaway PSP ZIP in
the public PPSSPP SDL AppImage under Xvfb. It provides an authentic PSP
executable smoke test without requiring a modified console or private SDK.

The script downloads the official PPSSPP v1.20.4 x86_64 AppImage from the
[upstream release](https://github.com/hrydgard/ppsspp/releases/tag/v1.20.4)
and verifies its pinned SHA-256 before execution:

```text
661c098e6b7f7610171a57b7c533ce8bba6f2312b71e76d61e850461973eba21
```

PPSSPP is GPL-2.0 licensed. The test downloads it on demand into ignored
`build/` storage; the project does not redistribute the emulator. See the
[upstream license](https://github.com/hrydgard/ppsspp/blob/v1.20.4/LICENSE.TXT)
and [Linux build documentation](https://github.com/hrydgard/ppsspp/wiki/Build-instructions).

## Run it

Build the package, then run the smoke test:

```sh
./scripts/build-psp-frontend.sh
./scripts/test-psp-ppsspp.sh
```

To test a downloaded release package instead:

```sh
./scripts/test-psp-ppsspp.sh \
  build/release/v0.1.4/johnny-castaway-psp-frontend.zip
```

The host needs `curl`, `grep`, `sha256sum`, `timeout`, `unzip`, `xvfb-run`,
and the standard utilities used by the script. On Ubuntu, the emulator-only
packages are:

```sh
sudo apt-get install pulseaudio xvfb xauth
pulseaudio --start --exit-idle-time=-1
```

Each run uses new XDG config/data/cache directories beneath an ignored
`build/ppsspp-smoke.*` directory. It does not read or modify the user's
normal PPSSPP settings or memory stick. It first requests SDL's dummy audio
backend under both the SDL2 and SDL3 environment-variable names. The pinned
AppImage may still select its compiled PulseAudio backend, so headless CI also
starts a user-scoped PulseAudio daemon. Evidence is retained there after both
success and failure.

## What passing means

The check fails closed unless all of the following are true:

- the install ZIP is valid, has the expected EBOOT path, and has no original
  `RESOURCE.MAP`/`RESOURCE.001`, audio, ADS, TTM, BMP, SCR, or VAG files;
- the AppImage matches the pinned digest and reports PPSSPP v1.20.4;
- PPSSPP keeps the EBOOT alive for the 30-second observation window;
- PPSSPP logs an EBOOT boot, the `RetroArch` PSP module, GE display-list
  submission, and PSP framebuffer presentation; and
- the logs contain no host crash, illegal instruction, unhandled exception,
  or executable-load failure.

This is a boot/runtime smoke, not a pixel-perfect gameplay test. The Johnny
core requires the user's lawfully obtained original data and the release must
not contain that data. The same PPSSPP v1.20.4 environment renders a black
surface for Libretro's official RetroArch 1.22.2 PSP Salamander EBOOT, even
though its module, threads, GE lists, and framebuffers remain active. For that
reason a black PPSSPP capture is not a Johnny-specific failure and the test
does not assert pixels. The official PSP install guide likewise describes a
bundle containing the launcher, cores, and assets:
[Libretro PSP installation](https://docs.libretro.com/guides/install-psp/).

Real PSP hardware remains the release boundary for the rendered menu,
controller input, audio, data-file selection, and sustained gameplay. The
native RetroArch and Web tests cover Johnny's actual libretro frames and menu
options on environments where those behaviors can be automated.
