#!/usr/bin/env sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu

root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
package=${1:-${PSP_PACKAGE:-build/psp-frontend/out/johnny-castaway-psp-frontend.zip}}

ppsspp_version=1.20.4
ppsspp_url="https://github.com/hrydgard/ppsspp/releases/download/v${ppsspp_version}/PPSSPP-v${ppsspp_version}-anylinux-x86_64.AppImage"
ppsspp_sha256=661c098e6b7f7610171a57b7c533ce8bba6f2312b71e76d61e850461973eba21

case "$package" in
    /*) ;;
    *) package="$root/$package" ;;
esac

for tool in curl grep mktemp mv sed sha256sum timeout unzip xvfb-run; do
    command -v "$tool" >/dev/null 2>&1 || {
        printf 'required PSP emulator smoke tool is unavailable: %s\n' "$tool" >&2
        exit 1
    }
done

if [ ! -s "$package" ]; then
    printf 'PSP frontend package is missing or empty: %s\n' "$package" >&2
    exit 1
fi

mkdir -p "$root/build"
run_dir=$(mktemp -d "$root/build/ppsspp-smoke.XXXXXX")
finish()
{
    status=$?
    trap - 0
    printf 'PPSSPP smoke evidence retained at %s\n' "$run_dir"
    exit "$status"
}
trap finish 0

package_entries="$run_dir/package-entries.txt"
unzip -tq "$package"
unzip -Z1 "$package" >"$package_entries"

if grep -Eq '(^/|(^|/)\.\.(/|$)|\\)' "$package_entries"; then
    printf 'PSP package contains an unsafe archive path\n' >&2
    exit 1
fi
if ! grep -Fx 'PSP/GAME/JohnnyCastaway/EBOOT.PBP' "$package_entries" >/dev/null; then
    printf 'PSP package does not install the expected EBOOT.PBP\n' >&2
    exit 1
fi
if ! grep -Fx \
        'PSP/GAME/JohnnyCastaway/INFO/johnny_castaway_libretro.info' \
        "$package_entries" >/dev/null; then
    printf 'PSP package does not install its core metadata in INFO/\n' >&2
    exit 1
fi
if grep -Ei '(^|/)(RESOURCE\.(MAP|001)|sound[0-9]+\.wav|[^/]+\.(ads|ttm|bmp|scr|vag))$' \
        "$package_entries" >/dev/null; then
    printf 'PSP package contains original Johnny Castaway data\n' >&2
    exit 1
fi

memstick="$run_dir/config/ppsspp"
mkdir -p "$memstick" "$run_dir/data" "$run_dir/cache"
(
    cd "$memstick"
    unzip -q "$package" 'PSP/GAME/JohnnyCastaway/EBOOT.PBP'
)
eboot="$memstick/PSP/GAME/JohnnyCastaway/EBOOT.PBP"
if [ ! -s "$eboot" ]; then
    printf 'installed PSP EBOOT is missing or empty: %s\n' "$eboot" >&2
    exit 1
fi

cache_dir="$root/build/ppsspp-cache/v${ppsspp_version}"
appimage="$cache_dir/PPSSPP-v${ppsspp_version}-anylinux-x86_64.AppImage"
mkdir -p "$cache_dir"
if [ ! -e "$appimage" ]; then
    download=$(mktemp "$cache_dir/.PPSSPP.AppImage.XXXXXX")
    printf 'Downloading official PPSSPP v%s AppImage\n' "$ppsspp_version"
    curl --fail --location --show-error --output "$download" "$ppsspp_url"
    printf '%s  %s\n' "$ppsspp_sha256" "$download" | sha256sum -c -
    chmod u+x "$download"
    mv "$download" "$appimage"
fi
printf '%s  %s\n' "$ppsspp_sha256" "$appimage" | sha256sum -c -
chmod u+x "$appimage"

appimage_version=$(
    APPIMAGE_EXTRACT_AND_RUN=1 \
    XDG_CONFIG_HOME="$run_dir/config" \
    XDG_DATA_HOME="$run_dir/data" \
    XDG_CACHE_HOME="$run_dir/cache" \
    "$appimage" --version 2>&1
)
printf '%s\n' "$appimage_version" | grep -F "$ppsspp_version" >/dev/null || {
    printf 'PPSSPP AppImage did not report pinned version %s: %s\n' \
        "$ppsspp_version" "$appimage_version" >&2
    exit 1
}

stdio_log="$run_dir/stdio.log"
ppsspp_log="$run_dir/ppsspp.log"
set +e
APPIMAGE_EXTRACT_AND_RUN=1 \
XDG_CONFIG_HOME="$run_dir/config" \
XDG_DATA_HOME="$run_dir/data" \
XDG_CACHE_HOME="$run_dir/cache" \
SDL_AUDIODRIVER=dummy \
LIBGL_ALWAYS_SOFTWARE=1 \
xvfb-run -a -s '-screen 0 960x544x24' \
    timeout --signal=TERM --kill-after=5 30 \
    "$appimage" -d --windowed --xres 960 --yres 544 \
    --log="$ppsspp_log" --escape-exit "$eboot" \
    >"$stdio_log" 2>&1
emulator_status=$?
set -e

if [ "$emulator_status" -ne 124 ]; then
    printf 'PPSSPP exited before the 30-second observation window (status %s)\n' \
        "$emulator_status" >&2
    tail -n 120 "$stdio_log" >&2
    exit 1
fi
if [ ! -s "$ppsspp_log" ]; then
    printf 'PPSSPP did not produce its requested log file\n' >&2
    exit 1
fi

combined_log="$run_dir/combined.log"
{
    printf '%s\n' '--- PPSSPP stdio ---'
    sed -n '1,$p' "$stdio_log"
    printf '%s\n' '--- PPSSPP file log ---'
    sed -n '1,$p' "$ppsspp_log"
} >"$combined_log"

for milestone in \
    "PPSSPP v${ppsspp_version}" \
    'Booted ' \
    'EBOOT.PBP' \
    'Module "RetroArch"' \
    'sceDisplaySetFrameBuf(' \
    'sceGeListEnQueue('
do
    if ! grep -F "$milestone" "$combined_log" >/dev/null; then
        printf 'PPSSPP log is missing boot milestone: %s\n' "$milestone" >&2
        tail -n 160 "$combined_log" >&2
        exit 1
    fi
done

if grep -Ei \
    'segmentation fault|illegal instruction|unhandled (host )?exception|core dumped|failed to load executable|failed to start .*EBOOT' \
    "$combined_log" >/dev/null; then
    printf 'PPSSPP log contains a crash or executable-load failure\n' >&2
    grep -Ein \
        'segmentation fault|illegal instruction|unhandled (host )?exception|core dumped|failed to load executable|failed to start .*EBOOT' \
        "$combined_log" >&2
    exit 1
fi

package_sha=$(sha256sum "$package" | sed 's/[[:space:]].*$//')
eboot_sha=$(sha256sum "$eboot" | sed 's/[[:space:]].*$//')
printf 'PPSSPP emulator smoke passed\n'
printf '  emulator: PPSSPP v%s (%s)\n' "$ppsspp_version" "$ppsspp_sha256"
printf '  package: %s (%s)\n' "$package" "$package_sha"
printf '  EBOOT.PBP: %s\n' "$eboot_sha"
printf '  runtime: 30 seconds; RetroArch module, GE, and framebuffer activity observed\n'
