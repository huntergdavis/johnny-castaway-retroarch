#!/usr/bin/env sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
build_base=${CONSOLE_BUILD_DIR:-build/console}
jobs=${JOBS:-}
warnings=${WARNINGS:--Wall -Wextra -Wpedantic -Werror}
pull=0
targets=

devkita64_image='devkitpro/devkita64@sha256:82575ea78651b530b2e232bb3799cfd1fe331514e053d5f724bb4b28191fb79d'
devkitarm_image='devkitpro/devkitarm@sha256:15b79ce75822c289538d8153da5fa7aafe5e6adc32ad8a575a197beca0f0761b'
devkitppc_image='devkitpro/devkitppc@sha256:4c919aa26151dd43d88ca28c922d1fe2409579a8ba60ef56517baf1abdfb1a48'
pspdev_image='ghcr.io/pspdev/pspdev@sha256:c9f1e60e8635d4df5ea246981b7473cbf48a9cf8457c1735f787821a684957f2'
vitasdk_image='vitasdk/vitasdk@sha256:9506538924a2f7d6e2505f919f3db285ceb297de5f57c211e5f60afa4b85ce85'
ps2dev_image='ps2dev/ps2dev@sha256:29f42ffaadc62d2615db4a8c22df933579e31e8f8004546dd84629314802d789'

usage()
{
    printf 'usage: %s [--pull] [--all|switch|3ds|gamecube|wii|wiiu|psp|vita|ps2 ...]\n' "$0"
}

for argument in "$@"; do
    case "$argument" in
        --pull) pull=1 ;;
        --all) targets='switch 3ds gamecube wii wiiu psp vita ps2' ;;
        --list)
            printf '%s\n' switch 3ds gamecube wii wiiu psp vita ps2
            exit 0
            ;;
        -*) usage >&2; exit 2 ;;
        *) targets="$targets $argument" ;;
    esac
done

if [ -z "$targets" ]; then
    targets='switch 3ds gamecube wii wiiu psp vita ps2'
fi
if [ -z "$jobs" ]; then
    jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1')
fi
case "$build_base" in
    /*|../*|*/../*|*/..) printf 'CONSOLE_BUILD_DIR must stay inside the repository\n' >&2; exit 2 ;;
esac

command -v docker >/dev/null 2>&1 || {
    printf 'Docker is required for reproducible console builds\n' >&2
    exit 1
}
command -v python3 >/dev/null 2>&1 || {
    printf 'Python 3 is required for archive validation\n' >&2
    exit 1
}
docker info >/dev/null 2>&1 || {
    printf 'Docker daemon is unavailable\n' >&2
    exit 1
}

ensure_image()
{
    image=$1
    if [ "$pull" -eq 1 ] || ! docker image inspect "$image" >/dev/null 2>&1; then
        docker pull "$image"
    fi
}

build_target()
{
    target=$1
    case "$target" in
        switch)
            image=$devkita64_image
            platform=libnx
            archive=johnny_castaway_libretro_libnx.a
            machine=AArch64
            ;;
        3ds)
            image=$devkitarm_image
            platform=ctr
            archive=johnny_castaway_libretro_ctr.a
            machine=ARM
            ;;
        gamecube)
            image=$devkitppc_image
            platform=ngc
            archive=johnny_castaway_libretro_ngc.a
            machine=PowerPC
            ;;
        wii)
            image=$devkitppc_image
            platform=wii
            archive=johnny_castaway_libretro_wii.a
            machine=PowerPC
            ;;
        wiiu)
            image=$devkitppc_image
            platform=wiiu
            archive=johnny_castaway_libretro_wiiu.a
            machine=PowerPC
            ;;
        psp)
            image=$pspdev_image
            platform=psp1
            archive=johnny_castaway_libretro_psp1.a
            machine='MIPS R3000'
            ;;
        vita)
            image=$vitasdk_image
            platform=vita
            archive=johnny_castaway_libretro_vita.a
            machine=ARM
            ;;
        ps2)
            image=$ps2dev_image
            platform=ps2
            archive=johnny_castaway_libretro_ps2.a
            machine='MIPS R3000'
            ;;
        *)
            printf 'unknown console target: %s\n' "$target" >&2
            usage >&2
            exit 2
            ;;
    esac

    ensure_image "$image"
    output_dir="$build_base/$platform"
    printf 'Building %s with %s\n' "$target" "$image"
    if [ "$target" = ps2 ]; then
        docker run --rm \
            -e JC_BUILD_DIR="$output_dir" -e JC_GID="$(id -g)" \
            -e JC_JOBS="$jobs" -e JC_PLATFORM="$platform" \
            -e JC_UID="$(id -u)" -e JC_WARNINGS="$warnings" \
            -v "$root:/src" -w /src "$image" sh -lc '
                apk add --no-cache make >/dev/null
                export PATH="/usr/local/ps2dev/ee/bin:/usr/local/ps2dev/bin:$PATH"
                make -j"$JC_JOBS" platform="$JC_PLATFORM" \
                    BUILD_DIR="$JC_BUILD_DIR" WARNINGS="$JC_WARNINGS"
                chown -R "$JC_UID:$JC_GID" "$JC_BUILD_DIR"
            '
    else
        docker run --rm --user "$(id -u):$(id -g)" \
            -v "$root:/src" -w /src "$image" \
            make -j"$jobs" platform="$platform" BUILD_DIR="$output_dir" \
            WARNINGS="$warnings"
    fi
    python3 "$root/tools/check_console_archive.py" \
        --machine "$machine" "$root/$output_dir/$archive"
}

for target in $targets; do
    build_target "$target"
done
