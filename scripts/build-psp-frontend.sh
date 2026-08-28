#!/usr/bin/env sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu

root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
build_base=${PSP_FRONTEND_BUILD_DIR:-build/psp-frontend}
core_archive_override=${PSP_CORE_ARCHIVE:-}
jobs=${JOBS:-}
allow_dirty=${ALLOW_DIRTY:-0}
pull=0
offline=0
expected_release_version=0.1.3
source_date_epoch=${SOURCE_DATE_EPOCH:-}

retroarch_url=https://github.com/libretro/RetroArch.git
retroarch_commit=96a1b1a9cf3f9166affcfd7df4323aa58d5c281a
pspdev_image='ghcr.io/pspdev/pspdev@sha256:c9f1e60e8635d4df5ea246981b7473cbf48a9cf8457c1735f787821a684957f2'
pspsdk_license_path=/usr/local/pspdev/psp/share/licenses/pspsdk/LICENSE
pspsdk_license_sha=2a72b3d563b8e080dd2be9a963f44c8396ca615421833d3cffb6d126101c1c82

usage()
{
    printf 'usage: %s [--pull|--offline]\n' "$0"
}

for argument in "$@"; do
    case "$argument" in
        --pull) pull=1 ;;
        --offline) offline=1 ;;
        -*) usage >&2; exit 2 ;;
        *) usage >&2; exit 2 ;;
    esac
done
if [ "$pull" -eq 1 ] && [ "$offline" -eq 1 ]; then
    printf '%s\n' '--pull and --offline are mutually exclusive' >&2
    exit 2
fi
case "$allow_dirty" in
    0|1) ;;
    *) printf 'ALLOW_DIRTY must be 0 or 1\n' >&2; exit 2 ;;
esac

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
command -v unzip >/dev/null 2>&1 || {
    printf 'unzip is required for PSP package validation\n' >&2
    exit 1
}
docker info >/dev/null 2>&1 || {
    printf 'Docker daemon is unavailable\n' >&2
    exit 1
}

project_commit=$(git -C "$root" rev-parse HEAD)
project_version=$(sed -n 's/^display_version = "\(.*\)"$/\1/p' \
    "$root/johnny_castaway_libretro.info")
case "$project_version" in
    [0-9]*.[0-9]*.[0-9]*) ;;
    *) printf 'display_version must be a numeric three-part version\n' >&2; exit 1 ;;
esac
if [ "$project_version" != "$expected_release_version" ]; then
    printf 'PSP release builder expects version %s, found %s\n' \
        "$expected_release_version" "$project_version" >&2
    exit 1
fi
info_sha=$(sha256sum "$root/johnny_castaway_libretro.info" | awk '{print $1}')
tree_status=$(git -C "$root" status --porcelain --untracked-files=all)
tree_state=clean
if [ -n "$tree_status" ]; then
    if [ "$allow_dirty" -ne 1 ]; then
        printf 'refusing publishable PSP build from a dirty tree; use ALLOW_DIRTY=1 only for local development\n' >&2
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
            if [ -f "$root/$path" ]; then
                sha256sum "$root/$path"
            elif [ -L "$root/$path" ]; then
                printf 'symlink %s -> %s\n' "$path" "$(readlink "$root/$path")"
            else
                printf 'missing %s\n' "$path"
            fi
        done
    } | sha256sum | awk '{print $1}'
}
source_fingerprint=$(project_fingerprint)
if [ -z "$source_date_epoch" ]; then
    source_date_epoch=$(git -C "$root" show -s --format=%ct "$project_commit")
fi
case "$source_date_epoch" in
    ''|*[!0-9]*) printf 'SOURCE_DATE_EPOCH must be a non-negative integer\n' >&2; exit 2 ;;
esac

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
fi

if [ "$pull" -eq 1 ]; then
    printf 'Refreshing pinned RetroArch commit %s\n' "$retroarch_commit"
    git -C "$checkout" fetch --depth 1 origin "$retroarch_commit"
elif ! git -C "$checkout" cat-file -e "$retroarch_commit^{commit}" 2>/dev/null; then
    if [ "$offline" -eq 1 ]; then
        printf 'pinned RetroArch commit is absent in offline mode: %s\n' \
            "$retroarch_commit" >&2
        exit 1
    fi
    printf 'Fetching pinned RetroArch commit %s\n' "$retroarch_commit"
    git -C "$checkout" fetch --depth 1 origin "$retroarch_commit"
fi
git -C "$checkout" checkout -q --detach "$retroarch_commit"
git -C "$checkout" reset -q --hard "$retroarch_commit"
git -C "$checkout" clean -ffdqx
if [ "$(git -C "$checkout" rev-parse HEAD)" != "$retroarch_commit" ]; then
    printf 'RetroArch checkout did not resolve to the pinned commit\n' >&2
    exit 1
fi
if [ -n "$(git -C "$checkout" status --porcelain --untracked-files=all)" ]; then
    printf 'generated RetroArch checkout is not clean after recreation\n' >&2
    exit 1
fi

if docker image inspect "$pspdev_image" >/dev/null 2>&1; then
    if [ "$pull" -eq 1 ]; then
        docker pull "$pspdev_image"
    fi
elif [ "$offline" -eq 1 ]; then
    printf 'pinned PSPDEV image is absent in offline mode: %s\n' \
        "$pspdev_image" >&2
    exit 1
else
    docker pull "$pspdev_image"
fi

if [ -n "$core_archive_override" ]; then
    printf 'Validating the supplied strict PSP Johnny Castaway core\n'
    python3 "$root/tools/check_console_archive.py" --machine 'MIPS R3000' \
        "$core_archive"
else
    printf 'Building and validating the strict PSP Johnny Castaway core\n'
    if [ "$offline" -eq 1 ]; then
        ALLOW_DIRTY="$allow_dirty" CONSOLE_BUILD_DIR="$build_base/core" JOBS="$jobs" \
            "$root/scripts/build-console-cores.sh" --offline psp
    elif [ "$pull" -eq 1 ]; then
        ALLOW_DIRTY="$allow_dirty" CONSOLE_BUILD_DIR="$build_base/core" JOBS="$jobs" \
            "$root/scripts/build-console-cores.sh" --pull psp
    else
        ALLOW_DIRTY="$allow_dirty" CONSOLE_BUILD_DIR="$build_base/core" JOBS="$jobs" \
            "$root/scripts/build-console-cores.sh" psp
    fi
fi

if [ ! -s "$core_archive" ]; then
    printf 'validated PSP core archive is missing: %s\n' "$core_archive" >&2
    exit 1
fi
core_provenance=$(dirname "$core_archive")/BUILD-PROVENANCE.txt
if [ ! -s "$core_provenance" ]; then
    printf 'adjacent PSP core provenance is missing: %s\n' "$core_provenance" >&2
    exit 1
fi
core_archive_sha=$(sha256sum "$core_archive" | awk '{print $1}')
expected_core_provenance=$(cat <<EOF
Johnny Castaway console core provenance
Target: psp
Platform: psp1
Archive: johnny_castaway_libretro_psp1.a
Johnny Castaway commit: $project_commit
Frontend version: $project_version
Tree state: $tree_state
Core metadata SHA-256: $info_sha
SDK image: $pspdev_image
Archive SHA-256: $core_archive_sha
Archive metadata: deterministic ar rcsD timestamp=0 uid=0 gid=0
EOF
)
if [ "$(cat "$core_provenance")" != "$expected_core_provenance" ]; then
    printf 'adjacent PSP core provenance does not exactly bind this source/archive build\n' >&2
    exit 1
fi
core_provenance_sha=$(sha256sum "$core_provenance" | awk '{print $1}')

printf 'Cleaning the generated PSP frontend checkout\n'
docker run --rm --read-only --network none --security-opt no-new-privileges \
    --cap-drop ALL --tmpfs /tmp --user "$(id -u):$(id -g)" \
    -e SOURCE_DATE_EPOCH="$source_date_epoch" -e TZ=UTC \
    -v "$checkout:/src:rw" -w /src "$pspdev_image" \
    make -f Makefile.psp1 HAVE_KERNEL_PRX=0 clean

cp "$core_archive" "$checkout/libretro_psp1.a"

printf 'Building the pinned PSP RetroArch frontend with HAVE_KERNEL_PRX=0\n'
docker run --rm --read-only --network none --security-opt no-new-privileges \
    --cap-drop ALL --tmpfs /tmp --user "$(id -u):$(id -g)" \
    -e SOURCE_DATE_EPOCH="$source_date_epoch" -e TZ=UTC \
    -v "$checkout:/src:rw" -w /src "$pspdev_image" \
    make -j"$jobs" -f Makefile.psp1 HAVE_KERNEL_PRX=0

elf="$checkout/retroarchpsp.elf"
pbp="$checkout/EBOOT.PBP"
if [ ! -s "$elf" ] || [ ! -s "$pbp" ]; then
    printf 'PSP frontend did not produce non-empty ELF and EBOOT.PBP artifacts\n' >&2
    exit 1
fi

elf_header=$(docker run --rm --read-only --network none \
    --security-opt no-new-privileges --cap-drop ALL --tmpfs /tmp \
    --user "$(id -u):$(id -g)" -v "$checkout:/src:ro" "$pspdev_image" \
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

for expected in 'Johnny Castaway' 'Closed Captions; disabled|enabled' \
    "$project_version"
do
    if ! LC_ALL=C grep -aF "$expected" "$elf" >/dev/null; then
        printf 'PSP ELF is missing embedded Johnny string: %s\n' "$expected" >&2
        exit 1
    fi
done
core_option_count=0
for expected in \
    johnny_castaway_initial_screen \
    johnny_castaway_chapter \
    johnny_castaway_holiday_overlay \
    johnny_castaway_story_seed \
    johnny_castaway_story_calendar \
    johnny_castaway_simulated_month \
    johnny_castaway_simulated_day \
    johnny_castaway_simulated_hour \
    johnny_castaway_playback_speed \
    johnny_castaway_tide \
    johnny_castaway_raft_stage \
    johnny_castaway_display_source \
    johnny_castaway_audio_enabled \
    johnny_castaway_audio_volume \
    johnny_castaway_ocean_enabled \
    johnny_castaway_ocean_volume \
    johnny_castaway_captions_enabled \
    johnny_castaway_caption_size \
    johnny_castaway_caption_background \
    johnny_castaway_caption_opacity \
    johnny_castaway_caption_position
do
    if ! LC_ALL=C grep -aF "$expected" "$elf" >/dev/null; then
        printf 'PSP ELF is missing Core Option key: %s\n' "$expected" >&2
        exit 1
    fi
    core_option_count=$((core_option_count + 1))
done
if [ "$core_option_count" -ne 21 ]; then
    printf 'internal PSP Core Option contract does not contain exactly 21 keys\n' >&2
    exit 1
fi

rm -rf "$package_root"
mkdir -p "$artifact_dir" "$package_root/PSP/GAME/JohnnyCastaway/INFO" \
    "$package_root/docs/licenses"
rm -f "$artifact_dir/EBOOT.PBP" "$artifact_dir/retroarchpsp.elf" \
    "$artifact_dir/johnny-castaway-psp-frontend.zip" \
    "$artifact_dir/BUILD-PROVENANCE.txt" \
    "$artifact_dir/CORE-BUILD-PROVENANCE.txt" \
    "$artifact_dir/README-PSP.md" "$artifact_dir/SHA256SUMS"
cp "$elf" "$artifact_dir/retroarchpsp.elf"
cp "$pbp" "$artifact_dir/EBOOT.PBP"
cp "$pbp" "$package_root/PSP/GAME/JohnnyCastaway/EBOOT.PBP"
cp "$root/johnny_castaway_libretro.info" \
    "$package_root/PSP/GAME/JohnnyCastaway/INFO/"
cp "$root/docs/PSP_PACKAGE.md" "$package_root/README-PSP.md"
cp "$root/LICENSE" "$root/CREDITS.md" "$root/johnny_castaway_libretro.info" \
    "$package_root/"
cp "$root/docs/CONSOLE_BUILDS.md" "$root/docs/PROVENANCE.md" \
    "$root/docs/THIRD_PARTY_NOTICES.md" "$package_root/docs/"
cp "$root/docs/licenses/BigSoundBank-0266-CC0.md" \
    "$package_root/docs/licenses/"
cp "$core_provenance" "$package_root/CORE-BUILD-PROVENANCE.txt"
docker run --rm --read-only --network none --security-opt no-new-privileges \
    --cap-drop ALL --tmpfs /tmp --user "$(id -u):$(id -g)" \
    -v "$package_root/docs/licenses:/licenses:rw" "$pspdev_image" \
    cp "$pspsdk_license_path" /licenses/PSPSDK-LICENSE
packaged_pspsdk_license_sha=$(sha256sum \
    "$package_root/docs/licenses/PSPSDK-LICENSE" | awk '{print $1}')
if [ "$packaged_pspsdk_license_sha" != "$pspsdk_license_sha" ]; then
    printf 'pinned image PSPSDK license hash changed unexpectedly\n' >&2
    exit 1
fi

elf_sha=$(sha256sum "$elf" | awk '{print $1}')
pbp_sha=$(sha256sum "$pbp" | awk '{print $1}')
{
    printf 'Johnny Castaway PSP RetroArch package build provenance\n'
    printf 'Johnny Castaway commit: %s (%s)\n' "$project_commit" "$tree_state"
    printf 'Frontend version: %s\n' "$project_version"
    printf 'Core metadata SHA-256: %s\n' "$info_sha"
    printf 'Source tree fingerprint: %s\n' "$source_fingerprint"
    printf 'RetroArch commit: %s\n' "$retroarch_commit"
    printf 'PSPDEV image: %s\n' "$pspdev_image"
    printf 'RetroArch PSP configuration: HAVE_KERNEL_PRX=0\n'
    printf 'Core archive SHA-256: %s\n' "$core_archive_sha"
    printf 'Core build provenance SHA-256: %s\n' "$core_provenance_sha"
    printf 'PSPSDK runtime license SHA-256: %s\n' "$pspsdk_license_sha"
    printf 'SOURCE_DATE_EPOCH: %s\n' "$source_date_epoch"
    printf 'ZIP metadata: sorted files timestamp=SOURCE_DATE_EPOCH mode=0100644\n'
    printf 'retroarchpsp.elf SHA-256: %s\n' "$elf_sha"
    printf 'EBOOT.PBP SHA-256: %s\n' "$pbp_sha"
} >"$package_root/BUILD-PROVENANCE.txt"
cp "$package_root/BUILD-PROVENANCE.txt" "$artifact_dir/BUILD-PROVENANCE.txt"
cp "$package_root/CORE-BUILD-PROVENANCE.txt" \
    "$artifact_dir/CORE-BUILD-PROVENANCE.txt"
cp "$package_root/README-PSP.md" "$artifact_dir/README-PSP.md"

package_zip="$artifact_dir/johnny-castaway-psp-frontend.zip"
python3 "$root/tools/make_deterministic_zip.py" \
    --directory "$package_root" --output "$package_zip" \
    --epoch "$source_date_epoch"
unzip -tq "$package_zip"
python3 - "$package_zip" "$source_date_epoch" <<'PY'
import datetime
import pathlib
import stat
import sys
import zipfile

package = pathlib.Path(sys.argv[1])
epoch = int(sys.argv[2])
value = datetime.datetime.fromtimestamp(epoch, datetime.timezone.utc)
if value.year < 1980:
    value = datetime.datetime(1980, 1, 1, tzinfo=datetime.timezone.utc)
if value.year > 2107:
    raise SystemExit("PSP ZIP epoch exceeds the supported DOS timestamp range")
expected_time = (
    value.year, value.month, value.day, value.hour, value.minute,
    value.second - value.second % 2,
)
expected_names = {
    "BUILD-PROVENANCE.txt",
    "CORE-BUILD-PROVENANCE.txt",
    "CREDITS.md",
    "LICENSE",
    "PSP/GAME/JohnnyCastaway/EBOOT.PBP",
    "PSP/GAME/JohnnyCastaway/INFO/johnny_castaway_libretro.info",
    "README-PSP.md",
    "docs/CONSOLE_BUILDS.md",
    "docs/PROVENANCE.md",
    "docs/THIRD_PARTY_NOTICES.md",
    "docs/licenses/BigSoundBank-0266-CC0.md",
    "docs/licenses/PSPSDK-LICENSE",
    "johnny_castaway_libretro.info",
}
with zipfile.ZipFile(package) as archive:
    infos = archive.infolist()
    names = [info.filename for info in infos]
    if names != sorted(names):
        raise SystemExit("PSP ZIP entries are not sorted")
    if set(names) != expected_names or len(names) != len(expected_names):
        missing = sorted(expected_names - set(names))
        extra = sorted(set(names) - expected_names)
        raise SystemExit(f"PSP ZIP member mismatch; missing={missing}, extra={extra}")
    if len({name.casefold() for name in names}) != len(names):
        raise SystemExit("PSP ZIP has a duplicate or case-fold collision")
    for info in infos:
        parts = info.filename.split("/")
        if (
            not info.filename
            or info.filename.startswith("/")
            or "\\" in info.filename
            or ":" in info.filename
            or any(part in ("", ".", "..") for part in parts)
        ):
            raise SystemExit(f"PSP ZIP has an unsafe path: {info.filename!r}")
        if info.date_time != expected_time:
            raise SystemExit(f"PSP ZIP timestamp is not normalized: {info.filename}")
        if info.create_system != 3 or info.external_attr >> 16 != stat.S_IFREG | 0o644:
            raise SystemExit(f"PSP ZIP mode is not 0100644: {info.filename}")
    bad = archive.testzip()
    if bad:
        raise SystemExit(f"PSP ZIP has a corrupt member: {bad}")
PY
for expected_path in \
    PSP/GAME/JohnnyCastaway/EBOOT.PBP \
    PSP/GAME/JohnnyCastaway/INFO/johnny_castaway_libretro.info \
    README-PSP.md \
    BUILD-PROVENANCE.txt \
    CORE-BUILD-PROVENANCE.txt \
    LICENSE \
    CREDITS.md \
    johnny_castaway_libretro.info \
    docs/CONSOLE_BUILDS.md \
    docs/PROVENANCE.md \
    docs/THIRD_PARTY_NOTICES.md \
    docs/licenses/BigSoundBank-0266-CC0.md \
    docs/licenses/PSPSDK-LICENSE
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
source_info_sha=$(sha256sum "$root/johnny_castaway_libretro.info" | awk '{print $1}')
packaged_info_sha=$(unzip -p "$package_zip" \
    PSP/GAME/JohnnyCastaway/INFO/johnny_castaway_libretro.info | \
    sha256sum | awk '{print $1}')
packaged_root_info_sha=$(unzip -p "$package_zip" \
    johnny_castaway_libretro.info | sha256sum | awk '{print $1}')
if [ "$source_info_sha" != "$info_sha" ] || \
   [ "$packaged_info_sha" != "$info_sha" ] || \
   [ "$packaged_root_info_sha" != "$info_sha" ]; then
    printf 'packaged PSP core metadata copies do not match the captured source file\n' >&2
    exit 1
fi
packaged_core_provenance_sha=$(unzip -p "$package_zip" \
    CORE-BUILD-PROVENANCE.txt | sha256sum | awk '{print $1}')
if [ "$packaged_core_provenance_sha" != "$core_provenance_sha" ]; then
    printf 'packaged PSP core provenance does not match the validated record\n' >&2
    exit 1
fi
packaged_build_provenance_sha=$(unzip -p "$package_zip" \
    BUILD-PROVENANCE.txt | sha256sum | awk '{print $1}')
build_provenance_sha=$(sha256sum "$artifact_dir/BUILD-PROVENANCE.txt" | awk '{print $1}')
if [ "$packaged_build_provenance_sha" != "$build_provenance_sha" ]; then
    printf 'packaged PSP build provenance does not match the artifact record\n' >&2
    exit 1
fi
packaged_license_sha=$(unzip -p "$package_zip" \
    docs/licenses/PSPSDK-LICENSE | sha256sum | awk '{print $1}')
if [ "$packaged_license_sha" != "$pspsdk_license_sha" ]; then
    printf 'packaged PSPSDK runtime license does not match the pinned image\n' >&2
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
        johnny-castaway-psp-frontend.zip BUILD-PROVENANCE.txt \
        CORE-BUILD-PROVENANCE.txt README-PSP.md \
        >SHA256SUMS
    sha256sum -c SHA256SUMS
)

if [ "$(git -C "$root" rev-parse HEAD)" != "$project_commit" ] || \
   [ "$(sed -n 's/^display_version = "\(.*\)"$/\1/p' \
        "$root/johnny_castaway_libretro.info")" != "$project_version" ] || \
   [ "$(sha256sum "$root/johnny_castaway_libretro.info" | awk '{print $1}')" != "$info_sha" ] || \
   [ "$(project_fingerprint)" != "$source_fingerprint" ]; then
    printf 'Johnny Castaway source changed during the PSP frontend build\n' >&2
    exit 1
fi

printf 'PSP frontend validation passed at RetroArch %s\n' "$retroarch_commit"
printf 'Artifacts:\n'
printf '  %s\n' "$package_zip" "$artifact_dir/retroarchpsp.elf" \
    "$artifact_dir/EBOOT.PBP" "$artifact_dir/CORE-BUILD-PROVENANCE.txt" \
    "$artifact_dir/SHA256SUMS"
