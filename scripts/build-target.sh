#!/usr/bin/env sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu

target="${1:-native}"
jobs="${JOBS:-}"
if [ -z "$jobs" ]; then
    jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1')
fi

case "$target" in
    --list)
        printf '%s\n' native linux-x86_64 linux-x86 linux-aarch64 \
            linux-armv7 linux-armv7-neon \
            windows-x64 windows-x86 macos macos-x86_64 macos-arm64 \
            macos-universal ios-arm64 ios-sim-arm64 ios-sim-x86_64 \
            tvos-arm64 tvos-sim-arm64 tvos-sim-x86_64 web android-arm64 \
            android-armv7 android-x86_64 android-x86 switch wii gamecube \
            wiiu psp vita 3ds ps2
        exit 0
        ;;
    native) platform=unix ;;
    linux-x86_64) platform=linux_x86_64 ;;
    linux-x86) platform=linux_x86 ;;
    linux-aarch64) platform=linux_aarch64 ;;
    linux-armv7) platform=linux_armv7 ;;
    linux-armv7-neon) platform=linux_armv7_neon ;;
    windows-x64) platform=mingw_x86_64 ;;
    windows-x86) platform=mingw_x86 ;;
    macos) platform=osx ;;
    macos-x86_64) platform=osx_x86_64 ;;
    macos-arm64) platform=osx_arm64 ;;
    macos-universal)
        exec "$(dirname "$0")/build-apple-universal.sh"
        ;;
    ios-arm64) platform=ios_arm64 ;;
    ios-sim-arm64) platform=ios_sim_arm64 ;;
    ios-sim-x86_64) platform=ios_sim_x86_64 ;;
    tvos-arm64) platform=tvos_arm64 ;;
    tvos-sim-arm64) platform=tvos_sim_arm64 ;;
    tvos-sim-x86_64) platform=tvos_sim_x86_64 ;;
    web) platform=emscripten ;;
    android-arm64) platform=android_arm64 ;;
    android-armv7) platform=android_armv7 ;;
    android-x86_64) platform=android_x86_64 ;;
    android-x86) platform=android_x86 ;;
    switch) platform=libnx ;;
    wii) platform=wii ;;
    gamecube) platform=ngc ;;
    wiiu) platform=wiiu ;;
    psp) platform=psp1 ;;
    vita) platform=vita ;;
    3ds) platform=ctr ;;
    ps2) platform=ps2 ;;
    *)
        printf 'unknown target: %s\n' "$target" >&2
        printf 'run %s --list\n' "$0" >&2
        exit 2
        ;;
esac

exec make -j"$jobs" platform="$platform"
