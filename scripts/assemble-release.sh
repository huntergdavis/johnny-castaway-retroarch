#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Assemble and independently validate a release payload from exact Actions runs.
# shellcheck disable=SC2016  # Markdown backticks in generated files are literal.

set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
expected_sha=""
ci_run=""
console_run=""
output_argument=""
dry_run=0
staging=""

usage()
{
    cat <<'EOF'
usage: scripts/assemble-release.sh \
  --sha FULL_40_CHARACTER_COMMIT_SHA \
  --ci-run GITHUB_ACTIONS_RUN_ID \
  --console-run GITHUB_ACTIONS_RUN_ID \
  --output build/release/OUTPUT_DIRECTORY [--dry-run]

Downloads exactly nine expected artifacts, validates their source runs, ABI and
machine contracts, scans for original game data, and writes release ZIPs plus
SHA256SUMS, CONTENTS.sha256, INVENTORY.md, and RELEASE_NOTES_DRAFT.md.

The output must be a new ignored directory below build/release/. --dry-run allows
development-version metadata for testing the assembler; it marks the resulting
inventory and notes as non-publishable.
EOF
}

fail()
{
    printf 'release assembly failed: %s\n' "$*" >&2
    exit 1
}

cleanup()
{
    if [[ -n "${staging}" && -d "${staging}" ]]; then
        rm -rf -- "${staging}"
    fi
}
trap cleanup EXIT

while (($#)); do
    case "$1" in
        --sha)
            (($# >= 2)) || fail '--sha requires a value'
            expected_sha=$2
            shift 2
            ;;
        --ci-run)
            (($# >= 2)) || fail '--ci-run requires a value'
            ci_run=$2
            shift 2
            ;;
        --console-run)
            (($# >= 2)) || fail '--console-run requires a value'
            console_run=$2
            shift 2
            ;;
        --output)
            (($# >= 2)) || fail '--output requires a value'
            output_argument=$2
            shift 2
            ;;
        --dry-run)
            dry_run=1
            shift
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

[[ -n "${expected_sha}" ]] || fail '--sha is required'
[[ -n "${ci_run}" ]] || fail '--ci-run is required'
[[ -n "${console_run}" ]] || fail '--console-run is required'
[[ -n "${output_argument}" ]] || fail '--output is required'
[[ "${expected_sha}" =~ ^[0-9a-f]{40}$ ]] ||
    fail '--sha must be a full lowercase 40-character commit SHA'
[[ "${ci_run}" =~ ^[0-9]+$ ]] || fail '--ci-run must be numeric'
[[ "${console_run}" =~ ^[0-9]+$ ]] || fail '--console-run must be numeric'
[[ "${ci_run}" != "${console_run}" ]] || fail 'CI and console run IDs must differ'

required_commands=(
    ar awk cat cmp dirname file find gh git grep mkdir mktemp mv nm objdump
    python3 readelf realpath rm sed sha256sum sort stat unzip wc xargs zip
)
for command_name in "${required_commands[@]}"; do
    command -v "${command_name}" >/dev/null 2>&1 ||
        fail "required command not found: ${command_name}"
done

release_root="${project_root}/build/release"
if [[ "${output_argument}" = /* ]]; then
    output="$(realpath -m -- "${output_argument}")"
else
    output="$(realpath -m -- "${project_root}/${output_argument}")"
fi
case "${output}" in
    "${release_root}"/*) ;;
    *) fail 'output must be a child directory of build/release/' ;;
esac
[[ ! -e "${output}" ]] || fail "output already exists: ${output}"
git -C "${project_root}" check-ignore -q -- "${output}" ||
    fail 'output must be ignored by Git'

git -C "${project_root}" cat-file -e "${expected_sha}^{commit}" 2>/dev/null ||
    fail "expected commit is not available locally: ${expected_sha}"
resolved_sha="$(git -C "${project_root}" rev-parse "${expected_sha}^{commit}")"
[[ "${resolved_sha}" = "${expected_sha}" ]] || fail 'commit resolution mismatch'

metadata="$(git -C "${project_root}" show \
    "${expected_sha}:johnny_castaway_libretro.info")" ||
    fail 'expected commit has no core metadata'
display_version="$(printf '%s\n' "${metadata}" |
    sed -n 's/^display_version = "\(.*\)"$/\1/p')"
[[ -n "${display_version}" ]] || fail 'core metadata has no display_version'
if [[ "${display_version}" = *-dev* && "${dry_run}" -ne 1 ]]; then
    fail "development metadata (${display_version}) requires --dry-run"
fi

forbidden_pattern='(^|/)(resource\.(map|001)|sound[0-9]+\.wav|[^/]+\.(ads|ttm|bmp|scr|vag))$'
tracked_forbidden="$(git -C "${project_root}" ls-tree -r --name-only \
    "${expected_sha}" | grep -Ei "${forbidden_pattern}" || true)"
[[ -z "${tracked_forbidden}" ]] ||
    fail "expected commit tracks original game data: ${tracked_forbidden}"

repository="$(gh repo view --json nameWithOwner --jq .nameWithOwner)"
[[ -n "${repository}" ]] || fail 'could not resolve GitHub repository'

verify_run()
{
    local run_id=$1
    local expected_workflow=$2
    local record head status conclusion workflow url
    record="$(gh run view "${run_id}" --repo "${repository}" \
        --json headSha,status,conclusion,workflowName,url \
        --jq '[.headSha,.status,.conclusion,.workflowName,.url] | @tsv')" ||
        fail "could not read Actions run ${run_id}"
    IFS=$'\t' read -r head status conclusion workflow url <<<"${record}"
    [[ "${head}" = "${expected_sha}" ]] ||
        fail "run ${run_id} head ${head} does not match ${expected_sha}"
    [[ "${status}" = completed ]] || fail "run ${run_id} is not completed"
    [[ "${conclusion}" = success ]] || fail "run ${run_id} did not succeed"
    [[ "${workflow}" = "${expected_workflow}" ]] ||
        fail "run ${run_id} uses unexpected workflow: ${workflow}"
    verified_run_url=${url}
}

verify_run "${ci_run}" CI
ci_run_url=${verified_run_url}
verify_run "${console_run}" 'Console cross-builds'
console_run_url=${verified_run_url}

ci_artifacts=(
    johnny-castaway-android-all-abis
    johnny-castaway-apple-cores
    johnny-castaway-emscripten-core
    johnny-castaway-linux-arm-and-windows-x86
    johnny-castaway-linux-x86
    johnny-castaway-linux-x86_64
    johnny-castaway-retroarch-web
    johnny-castaway-windows-x64
)
console_artifacts=(johnny-castaway-console-cores)
all_artifacts=("${ci_artifacts[@]}" "${console_artifacts[@]}")
declare -A artifact_id artifact_api_bytes artifact_run

collect_artifacts()
{
    local run_id=$1
    shift
    local expected=("$@")
    local lines=() actual=() expected_sorted=() actual_sorted=()
    local line name id size expired index
    mapfile -t lines < <(gh api -X GET \
        "repos/${repository}/actions/runs/${run_id}/artifacts" \
        -f per_page=100 \
        --jq '.artifacts[] | [.name, (.id|tostring), (.size_in_bytes|tostring), (.expired|tostring)] | @tsv')
    ((${#lines[@]} > 0)) || fail "run ${run_id} has no artifacts"
    for line in "${lines[@]}"; do
        IFS=$'\t' read -r name id size expired <<<"${line}"
        [[ "${expired}" = false ]] || fail "artifact is expired: ${name}"
        actual+=("${name}")
        artifact_id["${name}"]=${id}
        artifact_api_bytes["${name}"]=${size}
        artifact_run["${name}"]=${run_id}
    done
    mapfile -t expected_sorted < <(printf '%s\n' "${expected[@]}" | sort)
    mapfile -t actual_sorted < <(printf '%s\n' "${actual[@]}" | sort)
    ((${#expected_sorted[@]} == ${#actual_sorted[@]})) ||
        fail "run ${run_id} artifact count is ${#actual_sorted[@]}, expected ${#expected_sorted[@]}"
    for index in "${!expected_sorted[@]}"; do
        [[ "${expected_sorted[index]}" = "${actual_sorted[index]}" ]] ||
            fail "run ${run_id} artifact set does not match the release contract"
    done
}

collect_artifacts "${ci_run}" "${ci_artifacts[@]}"
collect_artifacts "${console_run}" "${console_artifacts[@]}"

mkdir -p -- "$(dirname "${output}")"
staging="$(mktemp -d "$(dirname "${output}")/.assemble-release.XXXXXX")"
artifacts_dir="${staging}/artifacts"
assets_dir="${staging}/assets"
mkdir -p -- "${artifacts_dir}" "${assets_dir}"

printf 'Downloading CI artifacts from %s\n' "${ci_run_url}"
gh run download "${ci_run}" --repo "${repository}" --dir "${artifacts_dir}"
console_name=${console_artifacts[0]}
mkdir -p -- "${artifacts_dir}/${console_name}"
printf 'Downloading console artifact from %s\n' "${console_run_url}"
gh run download "${console_run}" --repo "${repository}" \
    --name "${console_name}" --dir "${artifacts_dir}/${console_name}"

mapfile -t downloaded_directories < <(
    find "${artifacts_dir}" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | sort
)
mapfile -t expected_directories < <(printf '%s\n' "${all_artifacts[@]}" | sort)
((${#downloaded_directories[@]} == ${#expected_directories[@]})) ||
    fail 'downloaded artifact directory count is incorrect'
for index in "${!expected_directories[@]}"; do
    [[ "${expected_directories[index]}" = "${downloaded_directories[index]}" ]] ||
        fail 'downloaded artifact directories do not match the release contract'
done
special_file="$(find "${artifacts_dir}" -mindepth 1 ! -type f ! -type d -print -quit)"
[[ -z "${special_file}" ]] || fail "artifact contains a special file: ${special_file}"

scan_file_names()
{
    local directory=$1 label=$2 matches
    matches="$(find "${directory}" -type f -printf '%P\n' |
        grep -Ei "${forbidden_pattern}" || true)"
    [[ -z "${matches}" ]] || fail "${label} contains original game data: ${matches}"
}

scan_zip_entries()
{
    local directory=$1 archive entries matches
    while IFS= read -r -d '' archive; do
        entries="$(unzip -Z1 "${archive}")" || fail "cannot inspect ZIP: ${archive}"
        matches="$(printf '%s\n' "${entries}" |
            grep -Ei "${forbidden_pattern}" || true)"
        [[ -z "${matches}" ]] ||
            fail "ZIP ${archive} contains original game data: ${matches}"
    done < <(find "${directory}" -type f -iname '*.zip' -print0)
}

scan_ar_entries()
{
    local directory=$1 archive entries matches
    while IFS= read -r -d '' archive; do
        entries="$(ar t "${archive}")" || fail "cannot inspect archive: ${archive}"
        matches="$(printf '%s\n' "${entries}" |
            grep -Ei "${forbidden_pattern}" || true)"
        [[ -z "${matches}" ]] ||
            fail "archive ${archive} contains original game data: ${matches}"
    done < <(find "${directory}" -type f \( -name '*.a' -o -name '*.bc' \) -print0)
}

scan_file_names "${artifacts_dir}" 'downloaded artifacts'
scan_zip_entries "${artifacts_dir}"
scan_ar_entries "${artifacts_dir}"

must_file()
{
    [[ -f "$1" && -s "$1" ]] || fail "missing or empty required file: $1"
}

common_artifacts=(
    johnny-castaway-android-all-abis
    johnny-castaway-apple-cores
    johnny-castaway-console-cores
    johnny-castaway-emscripten-core
    johnny-castaway-linux-arm-and-windows-x86
    johnny-castaway-linux-x86
    johnny-castaway-linux-x86_64
    johnny-castaway-windows-x64
)
common_files=(
    CREDITS.md LICENSE johnny_castaway_libretro.info docs/PROVENANCE.md
    docs/THIRD_PARTY_NOTICES.md docs/licenses/BigSoundBank-0266-CC0.md
)
for artifact_name in "${common_artifacts[@]}"; do
    for relative_file in "${common_files[@]}"; do
        packaged_file="${artifacts_dir}/${artifact_name}/${relative_file}"
        must_file "${packaged_file}"
        expected_hash="$(git -C "${project_root}" show \
            "${expected_sha}:${relative_file}" | sha256sum | awk '{print $1}')"
        actual_hash="$(sha256sum "${packaged_file}" | awk '{print $1}')"
        [[ "${actual_hash}" = "${expected_hash}" ]] ||
            fail "${artifact_name}/${relative_file} does not match ${expected_sha}"
    done
done

web_dir="${artifacts_dir}/johnny-castaway-retroarch-web"
for relative_file in index.html jc-web-player.js johnny_castaway_libretro.js \
    johnny_castaway_libretro.wasm BUILD-PROVENANCE.txt WEB_PLAYER_NOTICE.md \
    licenses/RetroArch-GPL-3.0 licenses/johnny-castaway-retroarch-GPL-3.0; do
    must_file "${web_dir}/${relative_file}"
done
python3 "${project_root}/tools/check_web_dist.py" "${web_dir}"

expect_file_type()
{
    local path=$1 pattern=$2 description=$3 details
    must_file "${path}"
    details="$(file -b "${path}")"
    printf '%s\n' "${details}" | grep -Eq "${pattern}" ||
        fail "${description} has unexpected type: ${details}"
}

linux64="${artifacts_dir}/johnny-castaway-linux-x86_64/build/linux_x86_64/johnny_castaway_libretro.so"
linux32="${artifacts_dir}/johnny-castaway-linux-x86/build/linux_x86/johnny_castaway_libretro.so"
windows64="${artifacts_dir}/johnny-castaway-windows-x64/build/mingw_x86_64/johnny_castaway_libretro.dll"
portable="${artifacts_dir}/johnny-castaway-linux-arm-and-windows-x86/build"
android="${artifacts_dir}/johnny-castaway-android-all-abis/build"
apple="${artifacts_dir}/johnny-castaway-apple-cores/build"
emscripten="${artifacts_dir}/johnny-castaway-emscripten-core/build/emscripten/johnny_castaway_libretro_emscripten.bc"

expect_file_type "${linux64}" 'ELF 64-bit.*x86-64' 'Linux x86_64 core'
expect_file_type "${linux32}" 'ELF 32-bit.*Intel i386' 'Linux x86 core'
expect_file_type "${windows64}" 'PE32\+ executable.*x86-64' 'Windows x64 core'
expect_file_type "${portable}/mingw_x86/johnny_castaway_libretro.dll" \
    'PE32 executable.*Intel i386' 'Windows x86 core'
expect_file_type "${portable}/linux_aarch64/johnny_castaway_libretro.so" \
    'ELF 64-bit.*ARM aarch64' 'Linux AArch64 core'
for arm_core in \
    "${portable}/linux_armv7/johnny_castaway_libretro.so" \
    "${portable}/linux_armv7_neon/johnny_castaway_libretro.so"; do
    expect_file_type "${arm_core}" 'ELF 32-bit.*ARM, EABI5' 'Linux ARMv7 core'
done
expect_file_type "${android}/android_arm64/johnny_castaway_libretro.so" \
    'ELF 64-bit.*ARM aarch64' 'Android arm64 core'
expect_file_type "${android}/android_armv7/johnny_castaway_libretro.so" \
    'ELF 32-bit.*ARM, EABI5' 'Android armv7 core'
expect_file_type "${android}/android_x86_64/johnny_castaway_libretro.so" \
    'ELF 64-bit.*x86-64' 'Android x86_64 core'
expect_file_type "${android}/android_x86/johnny_castaway_libretro.so" \
    'ELF 32-bit.*Intel i386' 'Android x86 core'
expect_file_type "${apple}/osx_universal/johnny_castaway_libretro.dylib" \
    'Mach-O universal binary with 2 architectures' 'universal macOS core'
for apple_arm_core in \
    "${apple}/ios_arm64/johnny_castaway_libretro_ios.dylib" \
    "${apple}/ios_sim_arm64/johnny_castaway_libretro_ios_sim_arm64.dylib" \
    "${apple}/tvos_arm64/johnny_castaway_libretro_tvos.dylib" \
    "${apple}/tvos_sim_arm64/johnny_castaway_libretro_tvos_sim_arm64.dylib"; do
    expect_file_type "${apple_arm_core}" 'Mach-O 64-bit arm64' 'Apple arm64 core'
done
for apple_x86_core in \
    "${apple}/ios_sim_x86_64/johnny_castaway_libretro_ios_sim_x86_64.dylib" \
    "${apple}/tvos_sim_x86_64/johnny_castaway_libretro_tvos_sim_x86_64.dylib"; do
    expect_file_type "${apple_x86_core}" 'Mach-O 64-bit x86_64' 'Apple x86_64 core'
done
expect_file_type "${emscripten}" 'current ar archive' 'Emscripten core archive'
expect_file_type "${web_dir}/johnny_castaway_libretro.wasm" 'WebAssembly' 'Web core'

elf_cores=(
    "${linux64}" "${linux32}"
    "${portable}/linux_aarch64/johnny_castaway_libretro.so"
    "${portable}/linux_armv7/johnny_castaway_libretro.so"
    "${portable}/linux_armv7_neon/johnny_castaway_libretro.so"
    "${android}/android_arm64/johnny_castaway_libretro.so"
    "${android}/android_armv7/johnny_castaway_libretro.so"
    "${android}/android_x86_64/johnny_castaway_libretro.so"
    "${android}/android_x86/johnny_castaway_libretro.so"
)
for core in "${elf_cores[@]}"; do
    python3 "${project_root}/tools/check_core_exports.py" \
        --format elf --tool nm "${core}"
done
for core in "${windows64}" \
    "${portable}/mingw_x86/johnny_castaway_libretro.dll"; do
    python3 "${project_root}/tools/check_core_exports.py" \
        --format pe --tool objdump "${core}"
done

validation_notes=(
    'Every ELF and PE core passed exact 25-symbol libretro export validation.'
    'Every dynamic core passed an independent file-format and architecture check.'
)
macos_core="${apple}/osx_universal/johnny_castaway_libretro.dylib"
if nm -gU "${macos_core}" >/dev/null 2>&1; then
    for core in "${macos_core}" "${apple}"/*/*.dylib; do
        python3 "${project_root}/tools/check_core_exports.py" \
            --format macho --tool nm "${core}"
    done
    validation_notes+=('A compatible nm repeated exact 25-symbol Mach-O export checks.')
else
    validation_notes+=('Mach-O export recheck skipped locally: no compatible nm; the successful CI run performed the exact export checks on macOS.')
fi

console_base="${artifacts_dir}/johnny-castaway-console-cores/build/console"
python3 "${project_root}/tools/check_console_archive.py" --machine ARM \
    "${console_base}/ctr/johnny_castaway_libretro_ctr.a"
python3 "${project_root}/tools/check_console_archive.py" --machine AArch64 \
    "${console_base}/libnx/johnny_castaway_libretro_libnx.a"
python3 "${project_root}/tools/check_console_archive.py" --machine PowerPC \
    "${console_base}/ngc/johnny_castaway_libretro_ngc.a"
python3 "${project_root}/tools/check_console_archive.py" --machine 'MIPS R3000' \
    "${console_base}/ps2/johnny_castaway_libretro_ps2.a"
python3 "${project_root}/tools/check_console_archive.py" --machine 'MIPS R3000' \
    "${console_base}/psp1/johnny_castaway_libretro_psp1.a"
python3 "${project_root}/tools/check_console_archive.py" --machine ARM \
    "${console_base}/vita/johnny_castaway_libretro_vita.a"
python3 "${project_root}/tools/check_console_archive.py" --machine PowerPC \
    "${console_base}/wii/johnny_castaway_libretro_wii.a"
python3 "${project_root}/tools/check_console_archive.py" --machine PowerPC \
    "${console_base}/wiiu/johnny_castaway_libretro_wiiu.a"
validation_notes+=('All eight console archives passed member, target-machine, duplicate, undefined-symbol, and exact 25-symbol checks.')

mapfile -t emscripten_members < <(ar t "${emscripten}")
((${#emscripten_members[@]} > 0)) || fail 'Emscripten archive has no members'
for member in "${emscripten_members[@]}"; do
    [[ "${member}" = *.o ]] || fail "unexpected Emscripten archive member: ${member}"
done
unique_emscripten_members="$(printf '%s\n' "${emscripten_members[@]}" | sort -u | wc -l)"
[[ "${unique_emscripten_members}" -eq "${#emscripten_members[@]}" ]] ||
    fail 'Emscripten archive has duplicate members'
if command -v emnm >/dev/null 2>&1 && emnm -g --defined-only "${emscripten}" >/dev/null 2>&1; then
    emscripten_symbol_count="$(emnm -g --defined-only "${emscripten}" |
        awk '{print $NF}' | grep -E '^_?retro_' | sed 's/^_//' | sort -u | wc -l)"
    [[ "${emscripten_symbol_count}" -eq 25 ]] ||
        fail "Emscripten archive exposes ${emscripten_symbol_count} libretro symbols"
    validation_notes+=('emnm found all 25 libretro symbols in the Emscripten archive.')
else
    validation_notes+=('Emscripten symbol recheck skipped locally: emnm is unavailable; archive members and the linked Web distribution were validated.')
fi

declare -A principal
principal[johnny-castaway-linux-x86_64]='Linux x86_64 shared core'
principal[johnny-castaway-linux-x86]='Linux i386 shared core'
principal[johnny-castaway-windows-x64]='Windows x86_64 DLL core'
principal[johnny-castaway-linux-arm-and-windows-x86]='Linux AArch64, ARMv7/VFP, ARMv7/NEON, and Windows i386 cores'
principal[johnny-castaway-android-all-abis]='Android arm64, armv7, x86_64, and x86 cores'
principal[johnny-castaway-apple-cores]='Universal macOS and iOS/tvOS device/simulator cores'
principal[johnny-castaway-emscripten-core]='Emscripten static core and local Web source kit'
principal[johnny-castaway-retroarch-web]='Complete local RetroArch Web Player'
principal[johnny-castaway-console-cores]='3DS, Switch, GameCube, PS2, PSP, Vita, Wii, and Wii U static cores'

for artifact_name in "${all_artifacts[@]}"; do
    (
        cd "${artifacts_dir}"
        zip -q -9 -r "${assets_dir}/${artifact_name}.zip" "${artifact_name}"
    )
    unzip -tq "${assets_dir}/${artifact_name}.zip"
done
scan_file_names "${assets_dir}" 'release assets'
scan_zip_entries "${assets_dir}"

(
    cd "${artifacts_dir}"
    find . -type f -print0 | sort -z | xargs -0 sha256sum |
        sed 's#  \./#  #' >"${staging}/CONTENTS.sha256"
)
(
    cd "${assets_dir}"
    sha256sum ./*.zip | sed 's#  \./#  #' | sort -k2 >"${staging}/SHA256SUMS"
)

if [[ "${dry_run}" -eq 1 ]]; then
    inventory_title="Release packaging dry run for ${expected_sha:0:7}"
    publish_warning='**Do not publish this dry-run payload.** Development metadata was explicitly allowed.'
else
    inventory_title="Release payload for ${display_version}"
    publish_warning='This payload passed every automated assembly gate described below.'
fi

{
    printf '# %s\n\n%s\n\n' "${inventory_title}" "${publish_warning}"
    printf -- '- Repository: `%s`\n' "${repository}"
    printf -- '- Commit: `%s`\n' "${expected_sha}"
    printf -- '- Core metadata version: `%s`\n' "${display_version}"
    printf -- '- Full CI: run [%s](%s)\n' "${ci_run}" "${ci_run_url}"
    printf -- '- Console cross-builds: run [%s](%s)\n\n' \
        "${console_run}" "${console_run_url}"
    printf 'Both runs were completed successfully at the exact expected commit.\n\n'
    printf '| Release ZIP | Artifact ID | Run | API bytes | Files | Binaries | ZIP bytes | SHA-256 | Contents |\n'
    printf '|---|---:|---:|---:|---:|---:|---:|---|---|\n'
    for artifact_name in "${all_artifacts[@]}"; do
        artifact_directory="${artifacts_dir}/${artifact_name}"
        file_count="$(find "${artifact_directory}" -type f | wc -l)"
        binary_count="$(find "${artifact_directory}" -type f \
            \( -name '*.so' -o -name '*.dll' -o -name '*.dylib' \
               -o -name '*.a' -o -name '*.bc' -o -name '*.wasm' \) | wc -l)"
        zip_path="${assets_dir}/${artifact_name}.zip"
        zip_bytes="$(stat -c '%s' "${zip_path}")"
        zip_hash="$(sha256sum "${zip_path}" | awk '{print $1}')"
        printf '| `%s.zip` | %s | %s | %s | %s | %s | %s | `%s` | %s |\n' \
            "${artifact_name}" "${artifact_id[${artifact_name}]}" \
            "${artifact_run[${artifact_name}]}" "${artifact_api_bytes[${artifact_name}]}" \
            "${file_count}" "${binary_count}" "${zip_bytes}" "${zip_hash}" \
            "${principal[${artifact_name}]}"
    done
    printf '\n## Validation\n\n'
    for note in "${validation_notes[@]}"; do
        printf -- '- %s\n' "${note}"
    done
    printf -- '- The complete Web distribution passed `tools/check_web_dist.py`.\n'
    printf -- '- Every package ZIP passed `unzip -t`; `SHA256SUMS` verifies the nine release ZIPs.\n'
    printf -- '- `CONTENTS.sha256` inventories and verifies every extracted artifact file.\n'
    printf -- '- Commit paths, extracted filenames, nested ZIP entries, static archive members, and final package entries were scanned for the original resource pair, numbered WAVs, and ADS/TTM/BMP/SCR/VAG data; none were found.\n'
    printf -- '- Legal, credit, provenance, core-info, and CC0 notice files in every applicable artifact match the expected commit byte-for-byte.\n'
} >"${staging}/INVENTORY.md"

{
    printf '# Johnny Castaway libretro %s — release notes draft\n\n' "${display_version}"
    if [[ "${dry_run}" -eq 1 ]]; then
        printf '> **Packaging dry run only. Do not publish this payload.**\n\n'
    fi
    printf 'This release brings Johnny Castaway to RetroArch as a portable C99 core with deterministic 640x480 software rendering, automatic story playback, and direct access to all 63 chapters.\n\n'
    printf 'Highlights include closed captions, live chapter previews, deterministic seed and simulated-calendar controls, 1x-4x playback, authentic tide/raft presentation, an asset-free 36-holiday overlay, optional user-supplied original sound effects, separately licensed CC0 ocean ambience, island walking and tree occlusion, five fade styles, and versioned save states for scenes and transitions.\n\n'
    printf 'Packages cover Linux, Windows, Android, macOS, iOS, tvOS, Emscripten/Web, PSP, Vita, PS2, Nintendo 3DS, GameCube, Wii, Wii U, and Switch. Static console `.a` files are RetroArch frontend link inputs, not standalone applications.\n\n'
    printf 'Original Sierra/Dynamix data is not distributed. Supply your legally owned `RESOURCE.MAP` and `RESOURCE.001`; supported sibling sound WAVs are also user-supplied. See the included credits, provenance, third-party notices, and platform documentation for lineage and validation boundaries.\n'
} >"${staging}/RELEASE_NOTES_DRAFT.md"

(
    cd "${assets_dir}"
    sha256sum -c "${staging}/SHA256SUMS"
)
(
    cd "${artifacts_dir}"
    sha256sum -c "${staging}/CONTENTS.sha256" >/dev/null
)

mv -- "${staging}" "${output}"
staging=""
trap - EXIT
printf 'Release payload assembled: %s\n' "${output}"
printf 'Review INVENTORY.md and RELEASE_NOTES_DRAFT.md before any release action.\n'
