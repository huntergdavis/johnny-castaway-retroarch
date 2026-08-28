#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Run a bounded real-RetroArch smoke test without writing outside ignored build output.

set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
frontend=""
core="${project_root}/build/linux_x86_64/johnny_castaway_libretro.so"
content=""
output=""
frames=180
chapter=""
expected_resources=""
expected_min_scenes=""

usage()
{
    cat <<'EOF'
usage: scripts/test-native-retroarch.sh \
  --content /path/to/RESOURCE.MAP \
  [--frontend /path/to/retroarch] \
  [--core build/linux_x86_64/johnny_castaway_libretro.so] \
  [--output build/native-retroarch-NAME] \
  [--frames 180] [--chapter fishing1] [--expected-resources 5] \
  [--expected-min-scenes 2]

Runs the core in a real native RetroArch frontend for a bounded number of frames,
captures a screenshot, and checks Core Options v2, VFS, pixel format, geometry,
content indexing, API version, clean unload, and material 640x480 video without
a dominant indexed-renderer color key.

Use --chapter fishing1 with this repository's five-resource synthetic fixture.
Authentic data should normally retain the default Automatic Story selection.
EOF
}

fail()
{
    printf 'native RetroArch smoke failed: %s\n' "$*" >&2
    exit 1
}

while (($#)); do
    case "$1" in
        --frontend)
            (($# >= 2)) || fail '--frontend requires a value'
            frontend=$2
            shift 2
            ;;
        --core)
            (($# >= 2)) || fail '--core requires a value'
            core=$2
            shift 2
            ;;
        --content)
            (($# >= 2)) || fail '--content requires a value'
            content=$2
            shift 2
            ;;
        --output)
            (($# >= 2)) || fail '--output requires a value'
            output=$2
            shift 2
            ;;
        --frames)
            (($# >= 2)) || fail '--frames requires a value'
            frames=$2
            shift 2
            ;;
        --chapter)
            (($# >= 2)) || fail '--chapter requires a value'
            chapter=$2
            shift 2
            ;;
        --expected-resources)
            (($# >= 2)) || fail '--expected-resources requires a value'
            expected_resources=$2
            shift 2
            ;;
        --expected-min-scenes)
            (($# >= 2)) || fail '--expected-min-scenes requires a value'
            expected_min_scenes=$2
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            usage >&2
            fail "unknown argument: $1"
            ;;
    esac
done

if [[ -z "${frontend}" ]]; then
    frontend="$(command -v retroarch || true)"
fi
[[ -n "${frontend}" && -x "${frontend}" ]] ||
    fail 'RetroArch executable not found; pass --frontend'
[[ -f "${core}" ]] || fail "core not found: ${core}"
[[ -n "${content}" && -f "${content}" ]] ||
    fail 'a readable --content RESOURCE.MAP is required'
[[ "${frames}" =~ ^[1-9][0-9]*$ ]] || fail '--frames must be a positive integer'
if [[ -n "${expected_resources}" ]]; then
    [[ "${expected_resources}" =~ ^[1-9][0-9]*$ ]] ||
        fail '--expected-resources must be a positive integer'
fi
if [[ -n "${expected_min_scenes}" ]]; then
    [[ "${expected_min_scenes}" =~ ^[1-9][0-9]*$ ]] ||
        fail '--expected-min-scenes must be a positive integer'
fi

for command_name in grep identify python3 realpath sha256sum; do
    command -v "${command_name}" >/dev/null 2>&1 ||
        fail "required command not found: ${command_name}"
done
if [[ -z "${DISPLAY:-}" ]]; then
    command -v xvfb-run >/dev/null 2>&1 ||
        fail 'DISPLAY is unset and xvfb-run is unavailable'
fi

mkdir -p "${project_root}/build"
if [[ -z "${output}" ]]; then
    output="$(mktemp -d "${project_root}/build/native-retroarch-smoke.XXXXXX")"
else
    if [[ "${output}" = /* ]]; then
        output="$(realpath -m -- "${output}")"
    else
        output="$(realpath -m -- "${project_root}/${output}")"
    fi
    case "${output}" in
        "${project_root}/build"/*) ;;
        *) fail '--output must be a child directory of build/' ;;
    esac
    [[ ! -e "${output}" ]] || fail "output already exists: ${output}"
    mkdir -p "${output}"
fi

config_home="${output}/xdg-config"
data_home="${output}/xdg-data"
config_file="${output}/retroarch.cfg"
log_file="${output}/retroarch.log"
screenshot="${output}/frame-${frames}.png"
options_dir="${config_home}/retroarch/config/Johnny Castaway"
options_file="${options_dir}/Johnny Castaway.opt"
mkdir -p "${config_home}" "${data_home}" "${options_dir}"

printf '%s\n' \
    'video_driver = "gl"' \
    'audio_driver = "null"' \
    'input_driver = "null"' \
    'menu_driver = "null"' \
    'video_fullscreen = "false"' \
    'video_windowed_fullscreen = "false"' \
    'video_vsync = "false"' \
    'video_threaded = "false"' \
    'video_scale = "1.0"' \
    'save_config_on_exit = "false"' \
    'log_verbosity = "true"' \
    'libretro_log_level = "0"' >"${config_file}"

if [[ -n "${chapter}" ]]; then
    printf 'johnny_castaway_chapter = "%s"\n' "${chapter}" >"${options_file}"
fi

run_command=(
    "${frontend}"
    --config "${config_file}"
    --verbose
    "--max-frames=${frames}"
    --max-frames-ss
    "--max-frames-ss-path=${screenshot}"
    -L "${core}" "${content}"
)

printf 'RetroArch frontend: '
"${frontend}" --version | sed -n '1,2p'
printf 'Smoke output: %s\n' "${output}"
if [[ -n "${DISPLAY:-}" ]]; then
    XDG_CONFIG_HOME="${config_home}" \
    XDG_DATA_HOME="${data_home}" \
    LIBGL_ALWAYS_SOFTWARE=1 \
        "${run_command[@]}" >"${log_file}" 2>&1
else
    XDG_CONFIG_HOME="${config_home}" \
    XDG_DATA_HOME="${data_home}" \
    LIBGL_ALWAYS_SOFTWARE=1 \
        xvfb-run -a -s '-screen 0 1280x960x24' \
        "${run_command[@]}" >"${log_file}" 2>&1
fi

require_log()
{
    grep -Fq -- "$1" "${log_file}" || fail "missing log marker: $1"
}

require_log 'Loading dynamic libretro core from:'
require_log 'SET_CORE_OPTIONS_V2'
require_log 'SET_PIXEL_FORMAT: XRGB8888'
require_log 'GET_VFS_INTERFACE'
require_log 'Johnny Castaway: indexed '
require_log 'Version of libretro API: 1, Compiled against API: 1'
require_log 'Geometry: 640x480, Aspect: 1.333, FPS: 50.00, Sample rate: 44100.00 Hz.'
require_log 'Unloading game'
require_log 'Unloading core'
if [[ -n "${expected_resources}" ]]; then
    require_log "Johnny Castaway: indexed ${expected_resources} resources"
fi
if [[ -n "${chapter}" ]]; then
    require_log 'Johnny Castaway chapter:'
fi
if [[ -n "${expected_min_scenes}" ]]; then
    scene_count="$(grep -Fc 'Johnny Castaway automatic scene:' "${log_file}" || true)"
    [[ "${scene_count}" -ge "${expected_min_scenes}" ]] ||
        fail "Automatic Story started ${scene_count} scenes; expected at least ${expected_min_scenes}"
fi

[[ -s "${screenshot}" ]] || fail 'RetroArch did not produce a screenshot'
read -r image_width image_height image_colors < <(
    identify -format '%w %h %k\n' "${screenshot}"
)
[[ "${image_width}" = 640 && "${image_height}" = 480 ]] ||
    fail "unexpected screenshot geometry: ${image_width}x${image_height}"
[[ "${image_colors}" =~ ^[0-9]+$ && "${image_colors}" -gt 1 ]] ||
    fail "screenshot is blank or monochrome: ${image_colors} colors"
frame_quality="$({
    PYTHONPATH="${project_root}" python3 - "${screenshot}" <<'PY'
import pathlib
import sys

from tools.web_smoke_test import (
    frame_has_color_key_failure,
    frame_quality,
    png_pixels,
)

quality = frame_quality(png_pixels(pathlib.Path(sys.argv[1])))
print(
    f"key_ratio={quality['magenta_ratio']:.6f} "
    f"key_component={quality['renderer_key_component_pixels']}px/"
    f"{quality['renderer_key_component_width']}x"
    f"{quality['renderer_key_component_height']} "
    f"meaningful_ratio={quality['meaningful_ratio']:.6f}"
)
raise SystemExit(
    frame_has_color_key_failure(quality)
    # The five-resource CI fixture intentionally draws one small scripted
    # object on a mostly black canvas. Authentic browser captures use the
    # stricter 5% floor; this native ABI/frontend gate only needs to reject a
    # blank/lost frame while independently enforcing the full color-key gate.
    or quality["meaningful_ratio"] < 0.005
)
PY
} 2>&1)" ||
    fail "screenshot is dominated by the renderer color key or lacks material pixels: ${frame_quality}"

[[ -s "${options_file}" ]] || fail 'RetroArch did not persist core options'
grep -Fq 'johnny_castaway_story_seed = ' "${options_file}" ||
    fail 'Automatic Story Seed is absent from persisted Core Options'
grep -Fq 'johnny_castaway_raft_stage = ' "${options_file}" ||
    fail 'Raft Stage is absent from persisted Core Options'

printf 'Native RetroArch smoke passed: frames=%s geometry=%sx%s colors=%s %s screenshot_sha256=%s\n' \
    "${frames}" "${image_width}" "${image_height}" "${image_colors}" \
    "${frame_quality}" \
    "$(sha256sum "${screenshot}" | awk '{print $1}')"
