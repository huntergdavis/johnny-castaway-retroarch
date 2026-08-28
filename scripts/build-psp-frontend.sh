#!/usr/bin/env sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
build_base=${PSP_FRONTEND_BUILD_DIR:-build/psp-frontend}
jobs=${JOBS:-}

retroarch_url=https://github.com/libretro/RetroArch.git
retroarch_commit=96a1b1a9cf3f9166affcfd7df4323aa58d5c281a
pspdev_image='ghcr.io/pspdev/pspdev@sha256:c9f1e60e8635d4df5ea246981b7473cbf48a9cf8457c1735f787821a684957f2'

case "$build_base" in
    build|build/*) ;;
    *)
        printf 'PSP_FRONTEND_BUILD_DIR must stay under build/\n' >&2
        exit 2
        ;;
esac

if [ -z "$jobs" ]; then
    jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1')
fi

command -v docker >/dev/null 2>&1 || {
    printf 'Docker is required for the PSP frontend build\n' >&2
    exit 1
}
command -v git >/dev/null 2>&1 || {
    printf 'Git is required for the pinned RetroArch checkout\n' >&2
    exit 1
}
command -v grep >/dev/null 2>&1 || {
    printf 'grep is required for embedded-string validation\n' >&2
    exit 1
}
command -v od >/dev/null 2>&1 || {
    printf 'od is required for PBP header validation\n' >&2
    exit 1
}
command -v sha256sum >/dev/null 2>&1 || {
    printf 'sha256sum is required for artifact reporting\n' >&2
    exit 1
}
docker info >/dev/null 2>&1 || {
    printf 'Docker daemon is unavailable\n' >&2
    exit 1
}

checkout="$root/$build_base/RetroArch"
artifact_dir="$root/$build_base/out"
core_archive="$root/$build_base/core/psp1/johnny_castaway_libretro_psp1.a"

mkdir -p "$root/$build_base"
if [ -e "$checkout" ] && [ ! -d "$checkout/.git" ]; then
    printf '%s exists but is not a Git checkout\n' "$checkout" >&2
    exit 1
fi
if [ ! -d "$checkout/.git" ]; then
    git init -q "$checkout"
    git -C "$checkout" remote add origin "$retroarch_url"
else
    checkout_origin=$(git -C "$checkout" config --get remote.origin.url || true)
    if [ "$checkout_origin" != "$retroarch_url" ]; then
        printf 'unexpected RetroArch origin in %s: %s\n' \
            "$checkout" "$checkout_origin" >&2
        exit 1
    fi
    if [ -n "$(git -C "$checkout" status --porcelain --untracked-files=no)" ]; then
        printf 'tracked changes found in generated RetroArch checkout: %s\n' \
            "$checkout" >&2
        exit 1
    fi
fi

printf 'Fetching RetroArch %s\n' "$retroarch_commit"
git -C "$checkout" fetch --depth 1 origin "$retroarch_commit"
git -C "$checkout" checkout --detach "$retroarch_commit"
if [ "$(git -C "$checkout" rev-parse HEAD)" != "$retroarch_commit" ]; then
    printf 'RetroArch checkout did not resolve to the pinned commit\n' >&2
    exit 1
fi

printf 'Building and validating the strict PSP Johnny Castaway core\n'
CONSOLE_BUILD_DIR="$build_base/core" JOBS="$jobs" \
    "$root/scripts/build-console-cores.sh" psp

if [ ! -s "$core_archive" ]; then
    printf 'validated PSP core archive is missing: %s\n' "$core_archive" >&2
    exit 1
fi

if ! docker image inspect "$pspdev_image" >/dev/null 2>&1; then
    docker pull "$pspdev_image"
fi

printf 'Cleaning the generated PSP frontend checkout\n'
docker run --rm --user "$(id -u):$(id -g)" \
    -v "$checkout:/src" -w /src "$pspdev_image" \
    make -f Makefile.psp1 HAVE_KERNEL_PRX=0 clean

cp "$core_archive" "$checkout/libretro_psp1.a"

printf 'Building the pinned PSP RetroArch frontend with HAVE_KERNEL_PRX=0\n'
docker run --rm --user "$(id -u):$(id -g)" \
    -v "$checkout:/src" -w /src "$pspdev_image" \
    make -j"$jobs" -f Makefile.psp1 HAVE_KERNEL_PRX=0

elf="$checkout/retroarchpsp.elf"
pbp="$checkout/EBOOT.PBP"
if [ ! -s "$elf" ] || [ ! -s "$pbp" ]; then
    printf 'PSP frontend did not produce non-empty ELF and EBOOT.PBP artifacts\n' >&2
    exit 1
fi

elf_header=$(docker run --rm --user "$(id -u):$(id -g)" \
    -v "$checkout:/src:ro" "$pspdev_image" \
    /usr/local/pspdev/bin/psp-readelf -h /src/retroarchpsp.elf)
printf '%s\n' "$elf_header" | grep -F 'Class:                             ELF32' >/dev/null
printf '%s\n' "$elf_header" | grep -F 'little endian' >/dev/null
printf '%s\n' "$elf_header" | grep -F 'Type:                              EXEC (Executable file)' >/dev/null
printf '%s\n' "$elf_header" | grep -F 'Machine:                           MIPS R3000' >/dev/null

pbp_magic=$(od -An -tx1 -N8 "$pbp" | tr -d ' \n')
if [ "$pbp_magic" != 0050425000000100 ]; then
    printf 'unexpected EBOOT.PBP header: %s\n' "$pbp_magic" >&2
    exit 1
fi

for expected in \
    'Johnny Castaway' \
    'johnny_castaway_initial_screen' \
    'johnny_castaway_holiday_overlay' \
    'Closed Captions; disabled|enabled'
do
    if ! LC_ALL=C grep -aF "$expected" "$elf" >/dev/null; then
        printf 'PSP ELF is missing embedded Johnny string: %s\n' "$expected" >&2
        exit 1
    fi
done

mkdir -p "$artifact_dir"
cp "$elf" "$artifact_dir/retroarchpsp.elf"
cp "$pbp" "$artifact_dir/EBOOT.PBP"

printf 'PSP frontend validation passed at RetroArch %s\n' "$retroarch_commit"
printf 'Artifacts:\n'
sha256sum "$artifact_dir/retroarchpsp.elf" "$artifact_dir/EBOOT.PBP"
