#!/usr/bin/env bash
# Build a local RetroArch Web Player linked with this libretro core.

set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
web_build_root="${JC_WEB_BUILD_ROOT:-${project_root}/build/web-player}"
retroarch_dir="${web_build_root}/RetroArch"
assets_dir="${web_build_root}/retroarch-assets"
dist_dir="${web_build_root}/dist"
jobs="${JOBS:-2}"

# Revisions inspected and validated for this project on 2026-08-27.
retroarch_revision="${RETROARCH_REVISION:-96a1b1a9cf3f9166affcfd7df4323aa58d5c281a}"
assets_revision="${RETROARCH_ASSETS_REVISION:-73106363e14e34c08a5854b4cfbc29f184e3b783}"

core_archive="${project_root}/build/emscripten/johnny_castaway_libretro_emscripten.bc"

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "error: required command not found: $1" >&2
        exit 1
    fi
}

checkout_revision() {
    local repository_url="$1"
    local revision="$2"
    local destination="$3"

    if [[ ! -d "${destination}/.git" ]]; then
        git clone --filter=blob:none --no-checkout "${repository_url}" "${destination}"
    fi
    git -C "${destination}" fetch --depth 1 origin "${revision}"
    git -C "${destination}" checkout --detach --force "${revision}"
}

checkout_sparse_revision() {
    local repository_url="$1"
    local revision="$2"
    local destination="$3"
    shift 3

    if [[ ! -d "${destination}/.git" ]]; then
        git clone --filter=blob:none --no-checkout "${repository_url}" "${destination}"
    fi
    # Set the sparse paths before checkout so a fresh partial clone never
    # downloads blobs from the rest of the large asset repository.
    git -C "${destination}" sparse-checkout init --cone
    git -C "${destination}" sparse-checkout set "$@"
    git -C "${destination}" fetch --depth 1 origin "${revision}"
    git -C "${destination}" checkout --detach --force "${revision}"
}

for command_name in emcc emmake git make python3 sha256sum zip; do
    require_command "${command_name}"
done

mkdir -p "${web_build_root}"

echo "[1/5] Building the Johnny Castaway Emscripten core archive"
make -C "${project_root}" platform=emscripten -j"${jobs}"
if [[ ! -f "${core_archive}" ]]; then
    echo "error: core build did not produce ${core_archive}" >&2
    exit 1
fi

echo "[2/5] Checking out RetroArch ${retroarch_revision}"
checkout_revision \
    "https://github.com/libretro/RetroArch.git" \
    "${retroarch_revision}" \
    "${retroarch_dir}"

echo "[3/5] Linking the core into the official RetroArch web frontend"
# Debian/Ubuntu Emscripten packages may ship with a frozen system cache that
# lacks ports required by RetroArch. An isolated writable cache works for both
# distro Emscripten and emsdk installations.
export EM_FROZEN_CACHE=0
export EM_CACHE="${EM_CACHE:-${web_build_root}/emscripten-cache}"
mkdir -p "${EM_CACHE}"
link_stamp="${web_build_root}/retroarch-link-input.txt"
link_key="$(sha256sum "${core_archive}" | awk '{print $1}') ${retroarch_revision} $(emcc --version | head -n 1)"
if [[ -f "${retroarch_dir}/johnny_castaway_libretro.js" &&
      -f "${retroarch_dir}/johnny_castaway_libretro.wasm" &&
      -f "${link_stamp}" &&
      "$(<"${link_stamp}")" == "${link_key}" ]]; then
    echo "Core and frontend inputs are unchanged; reusing the linked web module"
else
    cp "${core_archive}" "${retroarch_dir}/libretro_emscripten.bc"
    emmake make -C "${retroarch_dir}" -f Makefile.emscripten \
        LIBRETRO=johnny_castaway -j"${jobs}" all
    printf '%s\n' "${link_key}" >"${link_stamp}"
fi

echo "[4/5] Checking out minimal RetroArch UI assets ${assets_revision}"
checkout_sparse_revision \
    "https://github.com/libretro/retroarch-assets.git" \
    "${assets_revision}" \
    "${assets_dir}" \
    ozone pkg sounds

echo "[5/5] Assembling ${dist_dir}"
staging_dir="$(mktemp -d "${web_build_root}/dist-staging.XXXXXX")"
trap 'rm -rf "${staging_dir}"' EXIT

mkdir -p \
    "${staging_dir}/assets/frontend" \
    "${staging_dir}/licenses" \
    "${staging_dir}/bundle/assets" \
    "${staging_dir}/bundle/cores" \
    "${staging_dir}/bundle/info" \
    "${staging_dir}/bundle/userdata"

cp "${project_root}/web/index.html" "${staging_dir}/"
cp "${project_root}/web/jc-web-player.js" "${staging_dir}/"
cp "${project_root}/web/style.css" "${staging_dir}/"
cp "${project_root}/web/WEB_PLAYER_NOTICE.md" "${staging_dir}/"
cp "${project_root}/web/licenses/BrowserFS-license.md" "${staging_dir}/licenses/"
cp "${project_root}/LICENSE" "${staging_dir}/licenses/johnny-castaway-retroarch-GPL-3.0"
cp "${project_root}/docs/licenses/BigSoundBank-0266-CC0.md" \
    "${staging_dir}/licenses/"
cp "${retroarch_dir}/COPYING" "${staging_dir}/licenses/RetroArch-GPL-3.0"
cp "${assets_dir}/COPYING" "${staging_dir}/licenses/retroarch-assets-CC-BY-4.0"

cp "${retroarch_dir}/pkg/emscripten/libretro/browserfs.min.js" "${staging_dir}/"
cp "${retroarch_dir}/johnny_castaway_libretro.js" "${staging_dir}/"
cp "${retroarch_dir}/johnny_castaway_libretro.wasm" "${staging_dir}/"

cp -R "${assets_dir}/ozone" "${staging_dir}/bundle/assets/"
cp -R "${assets_dir}/pkg" "${staging_dir}/bundle/assets/"
cp -R "${assets_dir}/sounds" "${staging_dir}/bundle/assets/"
cp "${project_root}/johnny_castaway_libretro.info" "${staging_dir}/bundle/info/"

# Preserve empty mount points in the ZIP. BrowserFS needs these directories
# before its writable in-memory filesystems can be mounted over them.
: >"${staging_dir}/bundle/cores/.mountpoint"
: >"${staging_dir}/bundle/userdata/.mountpoint"
(
    cd "${staging_dir}/bundle"
    zip -qr "${staging_dir}/assets/frontend/bundle.zip" assets cores info userdata
)
rm -rf "${staging_dir}/bundle"

{
    echo "Johnny Castaway RetroArch Web Player build provenance"
    echo "RetroArch: https://github.com/libretro/RetroArch/tree/${retroarch_revision}"
    echo "retroarch-assets: https://github.com/libretro/retroarch-assets/tree/${assets_revision}"
    echo "Core archive SHA-256: $(sha256sum "${core_archive}" | awk '{print $1}')"
    echo "BrowserFS SHA-256: $(sha256sum "${staging_dir}/browserfs.min.js" | awk '{print $1}')"
    echo "WebAssembly SHA-256: $(sha256sum "${staging_dir}/johnny_castaway_libretro.wasm" | awk '{print $1}')"
    echo "Built with: $(emcc --version | head -n 1)"
} >"${staging_dir}/BUILD-PROVENANCE.txt"

python3 "${project_root}/tools/check_web_dist.py" "${staging_dir}"

# This is a generated directory under build/. Replace it only after the new
# staging tree has passed validation.
rm -rf "${dist_dir}"
mv "${staging_dir}" "${dist_dir}"
trap - EXIT

echo
echo "Web Player ready: ${dist_dir}"
echo "Run: ${project_root}/scripts/serve-web.sh"
