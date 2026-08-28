#!/usr/bin/env sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu

root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
build_base=${CONSOLE_BUILD_DIR:-build/console}
jobs=${JOBS:-}
warnings=${WARNINGS:--Wall -Wextra -Wpedantic -Werror}
pull=0
offline=0
targets=
allow_dirty=${ALLOW_DIRTY:-0}

devkita64_image='devkitpro/devkita64@sha256:82575ea78651b530b2e232bb3799cfd1fe331514e053d5f724bb4b28191fb79d'
devkitarm_image='devkitpro/devkitarm@sha256:15b79ce75822c289538d8153da5fa7aafe5e6adc32ad8a575a197beca0f0761b'
devkitppc_image='devkitpro/devkitppc@sha256:4c919aa26151dd43d88ca28c922d1fe2409579a8ba60ef56517baf1abdfb1a48'
pspdev_image='ghcr.io/pspdev/pspdev@sha256:c9f1e60e8635d4df5ea246981b7473cbf48a9cf8457c1735f787821a684957f2'
vitasdk_image='vitasdk/vitasdk@sha256:9506538924a2f7d6e2505f919f3db285ceb297de5f57c211e5f60afa4b85ce85'
ps2dev_image='ps2dev/ps2dev@sha256:29f42ffaadc62d2615db4a8c22df933579e31e8f8004546dd84629314802d789'

usage()
{
    printf 'usage: %s [--pull|--offline] [--all|switch|3ds|gamecube|wii|wiiu|psp|vita|ps2 ...]\n' "$0"
}

for argument in "$@"; do
    case "$argument" in
        --pull) pull=1 ;;
        --offline) offline=1 ;;
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
if [ "$pull" -eq 1 ] && [ "$offline" -eq 1 ]; then
    printf '%s\n' '--pull and --offline are mutually exclusive' >&2
    exit 2
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

case "$allow_dirty" in 0|1) ;; *) printf 'ALLOW_DIRTY must be 0 or 1\n' >&2; exit 2 ;; esac
project_commit=$(git -C "$root" rev-parse HEAD)
project_version=$(sed -n 's/^display_version = "\(.*\)"$/\1/p' \
    "$root/johnny_castaway_libretro.info")
case "$project_version" in
    [0-9]*.[0-9]*.[0-9]*) ;;
    *) printf 'display_version must be a numeric three-part version\n' >&2; exit 1 ;;
esac
info_sha=$(sha256sum "$root/johnny_castaway_libretro.info" | sed 's/[[:space:]].*$//')
tree_status=$(git -C "$root" status --porcelain --untracked-files=all)
tree_state=clean
if [ -n "$tree_status" ]; then
    if [ "$allow_dirty" -ne 1 ]; then
        printf 'refusing publishable console build from a dirty tree; use ALLOW_DIRTY=1 only for local development\n' >&2
        exit 1
    fi
    tree_state=dirty-developer-mode
fi
project_fingerprint()
{
    {
        git -C "$root" status --porcelain --untracked-files=all
        git -C "$root" ls-files --cached --others --exclude-standard |
        while IFS= read -r path; do
            if [ -f "$root/$path" ]; then sha256sum "$root/$path"; else printf 'missing %s\n' "$path"; fi
        done
    } | sha256sum | sed 's/[[:space:]].*$//'
}
source_fingerprint=$(project_fingerprint)

ensure_image()
{
    image=$1
    if docker image inspect "$image" >/dev/null 2>&1; then
        if [ "$pull" -eq 1 ]; then docker pull "$image"; fi
    elif [ "$offline" -eq 1 ]; then
        printf 'required image is absent in offline mode: %s\n' "$image" >&2
        exit 1
    else
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
    case "$output_dir" in
        build/*) ;;
        *) printf 'resolved console output must stay below build/: %s\n' "$output_dir" >&2; exit 2 ;;
    esac
    target_build_dir="$root/${output_dir:?}"
    rm -rf -- "${target_build_dir:?}"
    printf 'Building %s with %s\n' "$target" "$image"
    if [ "$target" = ps2 ]; then
        ps2_make=$(OFFLINE="$offline" "$root/scripts/prepare-pinned-ps2-make.sh")
        docker run --rm --read-only --network none --security-opt no-new-privileges \
            --cap-drop ALL --tmpfs /tmp --user "$(id -u):$(id -g)" \
            -e JC_BUILD_DIR="$output_dir" \
            -e JC_JOBS="$jobs" -e JC_PLATFORM="$platform" \
            -e JC_WARNINGS="$warnings" \
            -v "$ps2_make:/tool/make:ro" -v "$root:/src" -w /src "$image" sh -lc '
                export PATH="/tool:/usr/local/ps2dev/ee/bin:/usr/local/ps2dev/bin:$PATH"
                make -j"$JC_JOBS" platform="$JC_PLATFORM" \
                    ARFLAGS=rcsD BUILD_DIR="$JC_BUILD_DIR" \
                    WARNINGS="$JC_WARNINGS"
            '
    else
        docker run --rm --read-only --network none --security-opt no-new-privileges \
            --cap-drop ALL --tmpfs /tmp --user "$(id -u):$(id -g)" \
            -e JC_BUILD_DIR="$output_dir" -e JC_JOBS="$jobs" \
            -e JC_PLATFORM="$platform" -e JC_WARNINGS="$warnings" \
            -v "$root:/src" -w /src "$image" \
            sh -c '
                make -j"$JC_JOBS" platform="$JC_PLATFORM" ARFLAGS=rcsD \
                    BUILD_DIR="$JC_BUILD_DIR" WARNINGS="$JC_WARNINGS"
            '
    fi
    python3 "$root/tools/check_console_archive.py" \
        --machine "$machine" "$root/$output_dir/$archive"
    if [ "$(project_fingerprint)" != "$source_fingerprint" ] || \
       [ "$(sha256sum "$root/johnny_castaway_libretro.info" | sed 's/[[:space:]].*$//')" != "$info_sha" ] || \
       [ "$(git -C "$root" rev-parse HEAD)" != "$project_commit" ]; then
        printf 'project source changed during console build\n' >&2
        exit 1
    fi
    archive_sha=$(sha256sum "$root/$output_dir/$archive" | sed 's/[[:space:]].*$//')
    {
        printf 'Johnny Castaway console core provenance\n'
        printf 'Target: %s\nPlatform: %s\nArchive: %s\n' "$target" "$platform" "$archive"
        printf 'Johnny Castaway commit: %s\n' "$project_commit"
        printf 'Frontend version: %s\nTree state: %s\n' "$project_version" "$tree_state"
        printf 'Core metadata SHA-256: %s\n' "$info_sha"
        printf 'SDK image: %s\nArchive SHA-256: %s\n' "$image" "$archive_sha"
        printf 'Archive metadata: deterministic ar rcsD timestamp=0 uid=0 gid=0\n'
    } >"$root/$output_dir/BUILD-PROVENANCE.txt"
}

for target in $targets; do
    build_target "$target"
done
