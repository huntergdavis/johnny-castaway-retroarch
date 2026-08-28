#!/usr/bin/env sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu

root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
build_base=${PSP_FRONTEND_BUILD_DIR:-build/psp-frontend}
core_archive_override=${PSP_CORE_ARCHIVE:-}
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
case "$build_base" in
    */../*|*/..)
        printf 'PSP_FRONTEND_BUILD_DIR must not contain parent traversal\n' >&2
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
command -v awk >/dev/null 2>&1 || {
    printf 'awk is required for PSP artifact validation\n' >&2
    exit 1
}
command -v python3 >/dev/null 2>&1 || {
    printf 'Python 3 is required for PSP core validation\n' >&2
    exit 1
}
command -v zip >/dev/null 2>&1 || {
    printf 'zip is required for the installable PSP package\n' >&2
    exit 1
}
command -v unzip >/dev/null 2>&1 || {
    printf 'unzip is required for PSP package validation\n' >&2
    exit 1
}
docker info >/dev/null 2>&1 || {
    printf 'Docker daemon is unavailable\n' >&2
    exit 1
}

checkout="$root/$build_base/RetroArch"
artifact_dir="$root/$build_base/out"
package_root="$root/$build_base/package-root"
if [ -n "$core_archive_override" ]; then
    case "$core_archive_override" in
        build|build/*) core_archive="$root/$core_archive_override" ;;
        *)
            printf 'PSP_CORE_ARCHIVE must stay under build/\n' >&2
            exit 2
            ;;
    esac
    case "$core_archive_override" in
        */../*|*/..)
            printf 'PSP_CORE_ARCHIVE must not contain parent traversal\n' >&2
            exit 2
            ;;
    esac
else
    core_archive="$root/$build_base/core/psp1/johnny_castaway_libretro_psp1.a"
fi

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

if [ -n "$core_archive_override" ]; then
    printf 'Validating the supplied strict PSP Johnny Castaway core\n'
    python3 "$root/tools/check_console_archive.py" --machine 'MIPS R3000' \
        "$core_archive"
else
    printf 'Building and validating the strict PSP Johnny Castaway core\n'
    CONSOLE_BUILD_DIR="$build_base/core" JOBS="$jobs" \
        "$root/scripts/build-console-cores.sh" psp
fi

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

rm -rf "$package_root"
mkdir -p "$artifact_dir" "$package_root/PSP/GAME/JohnnyCastaway" \
    "$package_root/docs/licenses"
rm -f "$artifact_dir/EBOOT.PBP" "$artifact_dir/retroarchpsp.elf" \
    "$artifact_dir/johnny-castaway-psp-frontend.zip" \
    "$artifact_dir/BUILD-PROVENANCE.txt" "$artifact_dir/README-PSP.md" \
    "$artifact_dir/SHA256SUMS"
cp "$elf" "$artifact_dir/retroarchpsp.elf"
cp "$pbp" "$artifact_dir/EBOOT.PBP"
cp "$pbp" "$package_root/PSP/GAME/JohnnyCastaway/EBOOT.PBP"
cp "$root/docs/PSP_PACKAGE.md" "$package_root/README-PSP.md"
cp "$root/LICENSE" "$root/CREDITS.md" "$root/johnny_castaway_libretro.info" \
    "$package_root/"
cp "$root/docs/CONSOLE_BUILDS.md" "$root/docs/PROVENANCE.md" \
    "$root/docs/THIRD_PARTY_NOTICES.md" "$package_root/docs/"
cp "$root/docs/licenses/BigSoundBank-0266-CC0.md" \
    "$package_root/docs/licenses/"

core_commit=$(git -C "$root" rev-parse HEAD)
core_tree_state=clean
if [ -n "$(git -C "$root" status --porcelain --untracked-files=no)" ]; then
    core_tree_state='tracked modifications present'
fi
core_archive_sha=$(sha256sum "$core_archive" | awk '{print $1}')
elf_sha=$(sha256sum "$elf" | awk '{print $1}')
pbp_sha=$(sha256sum "$pbp" | awk '{print $1}')
{
    printf 'Johnny Castaway PSP RetroArch package build provenance\n'
    printf 'Johnny Castaway commit: %s (%s)\n' "$core_commit" "$core_tree_state"
    printf 'RetroArch commit: %s\n' "$retroarch_commit"
    printf 'PSPDEV image: %s\n' "$pspdev_image"
    printf 'RetroArch PSP configuration: HAVE_KERNEL_PRX=0\n'
    printf 'Core archive SHA-256: %s\n' "$core_archive_sha"
    printf 'retroarchpsp.elf SHA-256: %s\n' "$elf_sha"
    printf 'EBOOT.PBP SHA-256: %s\n' "$pbp_sha"
} >"$package_root/BUILD-PROVENANCE.txt"
cp "$package_root/BUILD-PROVENANCE.txt" "$artifact_dir/BUILD-PROVENANCE.txt"
cp "$package_root/README-PSP.md" "$artifact_dir/README-PSP.md"

package_zip="$artifact_dir/johnny-castaway-psp-frontend.zip"
(
    cd "$package_root"
    zip -q -9 -r "$package_zip" .
)
unzip -tq "$package_zip"
for expected_path in \
    PSP/GAME/JohnnyCastaway/EBOOT.PBP \
    README-PSP.md \
    BUILD-PROVENANCE.txt \
    LICENSE \
    CREDITS.md \
    johnny_castaway_libretro.info \
    docs/CONSOLE_BUILDS.md \
    docs/PROVENANCE.md \
    docs/THIRD_PARTY_NOTICES.md \
    docs/licenses/BigSoundBank-0266-CC0.md
do
    unzip -Z1 "$package_zip" | grep -Fx "$expected_path" >/dev/null || {
        printf 'PSP install ZIP is missing: %s\n' "$expected_path" >&2
        exit 1
    }
done
packaged_pbp_sha=$(unzip -p "$package_zip" \
    PSP/GAME/JohnnyCastaway/EBOOT.PBP | sha256sum | awk '{print $1}')
if [ "$packaged_pbp_sha" != "$pbp_sha" ]; then
    printf 'packaged EBOOT.PBP does not match the validated frontend\n' >&2
    exit 1
fi
if unzip -Z1 "$package_zip" | grep -Ei \
    '(^|/)(RESOURCE\.(MAP|001)|sound[0-9]+\.wav|[^/]+\.(ads|ttm|bmp|scr|vag))$' \
    >/dev/null; then
    printf 'PSP install ZIP contains original game data\n' >&2
    exit 1
fi

(
    cd "$artifact_dir"
    sha256sum retroarchpsp.elf EBOOT.PBP \
        johnny-castaway-psp-frontend.zip BUILD-PROVENANCE.txt README-PSP.md \
        >SHA256SUMS
    sha256sum -c SHA256SUMS
)

printf 'PSP frontend validation passed at RetroArch %s\n' "$retroarch_commit"
printf 'Artifacts:\n'
printf '  %s\n' "$package_zip" "$artifact_dir/retroarchpsp.elf" \
    "$artifact_dir/EBOOT.PBP" "$artifact_dir/SHA256SUMS"
