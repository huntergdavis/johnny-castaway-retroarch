#!/usr/bin/env bash
# Build a universal macOS libretro core from separate Intel and Apple Silicon slices.

set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
jobs="${JOBS:-2}"
output_dir="${project_root}/build/osx_universal"
output="${output_dir}/johnny_castaway_libretro.dylib"
x86_slice="${project_root}/build/osx_x86_64/johnny_castaway_libretro.dylib"
arm_slice="${project_root}/build/osx_arm64/johnny_castaway_libretro.dylib"

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "error: universal Apple builds require macOS and Xcode" >&2
    exit 1
fi
for command_name in make xcrun; do
    if ! command -v "${command_name}" >/dev/null 2>&1; then
        echo "error: required command not found: ${command_name}" >&2
        exit 1
    fi
done

make -C "${project_root}" platform=osx_x86_64 -j"${jobs}"
make -C "${project_root}" platform=osx_arm64 -j"${jobs}"
mkdir -p "${output_dir}"
xcrun lipo -create "${x86_slice}" "${arm_slice}" -output "${output}"
xcrun lipo "${output}" -verify_arch x86_64 arm64

echo "Universal macOS core ready: ${output}"
