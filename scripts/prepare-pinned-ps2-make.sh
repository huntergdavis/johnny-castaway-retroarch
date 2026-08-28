#!/usr/bin/env sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu

root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
cache_dir=${PS2_MAKE_CACHE_DIR:-build/tool-cache/ps2-make}
offline=${OFFLINE:-0}
apk_name=make-4.4.1-r4.apk
apk_url=https://dl-cdn.alpinelinux.org/alpine/v3.24/main/x86_64/make-4.4.1-r4.apk
apk_sha=a916626a876f64c6b6fb32a6bf0fa93351d09fae0aa7684135642390da647e60
binary_sha=a75fd4f07fadd6506b7be7d4684af96d22d6cebf7847acdad4270d503d020f63

case "$cache_dir" in
    build/*) ;;
    *) printf 'PS2_MAKE_CACHE_DIR must stay below build/\n' >&2; exit 2 ;;
esac
case "$cache_dir" in
    /*|*'/../'*|*'/..'|../*)
        printf 'PS2_MAKE_CACHE_DIR must not be absolute or contain parent traversal\n' >&2
        exit 2
        ;;
esac
case "$offline" in 0|1) ;; *) printf 'OFFLINE must be 0 or 1\n' >&2; exit 2 ;; esac

apk_path="$root/$cache_dir/$apk_name"
binary_path="$root/$cache_dir/usr/bin/make"
mkdir -p "$(dirname "$apk_path")"
if [ ! -s "$apk_path" ]; then
    if [ "$offline" -eq 1 ]; then
        printf 'pinned PS2 make APK is absent in offline mode: %s\n' "$apk_path" >&2
        exit 1
    fi
    command -v curl >/dev/null 2>&1 || {
        printf 'curl is required to populate the pinned PS2 make cache\n' >&2
        exit 1
    }
    download=$(mktemp "$root/$cache_dir/.make-apk.XXXXXX")
    trap 'rm -f "$download"' EXIT HUP INT TERM
    curl --proto '=https' --tlsv1.2 -fsSL "$apk_url" -o "$download"
    received=$(sha256sum "$download" | sed 's/[[:space:]].*$//')
    if [ "$received" != "$apk_sha" ]; then
        printf 'pinned PS2 make APK hash mismatch\n' >&2
        exit 1
    fi
    mv "$download" "$apk_path"
    trap - EXIT HUP INT TERM
fi
received=$(sha256sum "$apk_path" | sed 's/[[:space:]].*$//')
if [ "$received" != "$apk_sha" ]; then
    printf 'cached PS2 make APK hash mismatch: %s\n' "$apk_path" >&2
    exit 1
fi
if [ ! -s "$binary_path" ]; then
    mkdir -p "$(dirname "$binary_path")"
    tar --ignore-zeros -xzf "$apk_path" -C "$root/$cache_dir" usr/bin/make
fi
received=$(sha256sum "$binary_path" | sed 's/[[:space:]].*$//')
if [ "$received" != "$binary_sha" ]; then
    printf 'extracted PS2 make binary hash mismatch: %s\n' "$binary_path" >&2
    exit 1
fi
printf '%s\n' "$binary_path"
