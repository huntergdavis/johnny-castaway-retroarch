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

Downloads exactly ten expected artifacts, validates their source runs, ABI,
machine, and installable-frontend contracts, scans for original game data, and
writes sixteen release ZIPs plus
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
    ar awk cat cmp cp dirname file find gh git grep mkdir mktemp mv nm objdump
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
expected_epoch="$(git -C "${project_root}" show -s --format=%ct "${expected_sha}")"
[[ "${expected_epoch}" =~ ^[0-9]+$ ]] ||
    fail 'expected commit has an invalid timestamp'
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
console_artifacts=(
    johnny-castaway-console-cores
    johnny-castaway-psp-frontend
)
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
printf 'Downloading console artifacts from %s\n' "${console_run_url}"
for console_name in "${console_artifacts[@]}"; do
    mkdir -p -- "${artifacts_dir}/${console_name}"
    gh run download "${console_run}" --repo "${repository}" \
        --name "${console_name}" --dir "${artifacts_dir}/${console_name}"
done

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
    CREDITS.md docs/PROVENANCE.md docs/THIRD_PARTY_NOTICES.md \
    licenses/RetroArch-GPL-3.0 licenses/johnny-castaway-retroarch-GPL-3.0; do
    must_file "${web_dir}/${relative_file}"
done
python3 "${project_root}/tools/check_web_dist.py" "${web_dir}"

psp_dir="${artifacts_dir}/johnny-castaway-psp-frontend"
psp_elf="${psp_dir}/retroarchpsp.elf"
psp_pbp="${psp_dir}/EBOOT.PBP"
psp_package="${psp_dir}/johnny-castaway-psp-frontend.zip"
for relative_file in EBOOT.PBP retroarchpsp.elf \
    johnny-castaway-psp-frontend.zip SHA256SUMS BUILD-PROVENANCE.txt \
    CORE-BUILD-PROVENANCE.txt README-PSP.md; do
    must_file "${psp_dir}/${relative_file}"
done
(
    cd "${psp_dir}"
    sha256sum -c SHA256SUMS
)
grep -Fxq "Johnny Castaway commit: ${expected_sha} (clean)" \
    "${psp_dir}/BUILD-PROVENANCE.txt" ||
    fail 'PSP build provenance does not name the exact clean release commit'
cmp "${psp_dir}/README-PSP.md" \
    <(git -C "${project_root}" show "${expected_sha}:docs/PSP_PACKAGE.md") ||
    fail 'PSP package README does not match the expected commit'

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
expect_file_type "${psp_elf}" 'ELF 32-bit.*MIPS.*MIPS-II' 'PSP frontend ELF'

psp_elf_header="$(readelf -h "${psp_elf}")"
printf '%s\n' "${psp_elf_header}" | grep -Eq 'Class:[[:space:]]+ELF32' ||
    fail 'PSP frontend is not ELF32'
printf '%s\n' "${psp_elf_header}" | grep -Eq 'Data:[[:space:]]+2.s complement, little endian' ||
    fail 'PSP frontend is not little endian'
printf '%s\n' "${psp_elf_header}" | grep -Eq 'Type:[[:space:]]+EXEC' ||
    fail 'PSP frontend is not an executable ELF'
printf '%s\n' "${psp_elf_header}" | grep -Eq 'Machine:[[:space:]]+MIPS R3000' ||
    fail 'PSP frontend has the wrong machine type'

psp_pbp_header="$(python3 - "${psp_pbp}" <<'PY'
from pathlib import Path
import sys

print(Path(sys.argv[1]).read_bytes()[:8].hex())
PY
)"
[[ "${psp_pbp_header}" = 0050425000000100 ]] ||
    fail "PSP EBOOT has an invalid PBP header: ${psp_pbp_header}"
for marker in 'Johnny Castaway' 'Closed Captions; disabled|enabled' \
    "${display_version}" \
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
    johnny_castaway_caption_position; do
    grep -aFq "${marker}" "${psp_elf}" ||
        fail "PSP frontend ELF is missing embedded marker: ${marker}"
done

unzip -tq "${psp_package}"
python3 - "${psp_package}" "${expected_epoch}" <<'PY'
import datetime
import stat
import sys
import zipfile

package = sys.argv[1]
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
        raise SystemExit("PSP ZIP member set does not match the release contract")
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
psp_package_entries="$(unzip -Z1 "${psp_package}")"
for package_entry in \
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
    docs/licenses/PSPSDK-LICENSE; do
    printf '%s\n' "${psp_package_entries}" | grep -Fxq "${package_entry}" ||
        fail "PSP install package is missing: ${package_entry}"
done
packaged_pbp_hash="$(unzip -p "${psp_package}" \
    PSP/GAME/JohnnyCastaway/EBOOT.PBP | sha256sum | awk '{print $1}')"
raw_pbp_hash="$(sha256sum "${psp_pbp}" | awk '{print $1}')"
[[ "${packaged_pbp_hash}" = "${raw_pbp_hash}" ]] ||
    fail 'PSP install package EBOOT does not match the validated artifact'
expected_info_hash="$(git -C "${project_root}" show \
    "${expected_sha}:johnny_castaway_libretro.info" | sha256sum | awk '{print $1}')"
packaged_root_info_hash="$(unzip -p "${psp_package}" \
    johnny_castaway_libretro.info | sha256sum | awk '{print $1}')"
packaged_nested_info_hash="$(unzip -p "${psp_package}" \
    PSP/GAME/JohnnyCastaway/INFO/johnny_castaway_libretro.info | \
    sha256sum | awk '{print $1}')"
[[ "${packaged_root_info_hash}" = "${expected_info_hash}" ]] ||
    fail 'PSP root core metadata does not match the expected commit'
[[ "${packaged_nested_info_hash}" = "${expected_info_hash}" ]] ||
    fail 'PSP installed core metadata does not match the expected commit'
[[ "${packaged_nested_info_hash}" = "${packaged_root_info_hash}" ]] ||
    fail 'PSP installed and root core metadata copies differ'

while IFS=$'\t' read -r package_entry source_path; do
    packaged_hash="$(unzip -p "${psp_package}" "${package_entry}" | sha256sum | awk '{print $1}')"
    expected_hash="$(git -C "${project_root}" show \
        "${expected_sha}:${source_path}" | sha256sum | awk '{print $1}')"
    [[ "${packaged_hash}" = "${expected_hash}" ]] ||
        fail "PSP install package ${package_entry} does not match ${expected_sha}:${source_path}"
done <<'EOF'
LICENSE	LICENSE
CREDITS.md	CREDITS.md
johnny_castaway_libretro.info	johnny_castaway_libretro.info
docs/CONSOLE_BUILDS.md	docs/CONSOLE_BUILDS.md
docs/PROVENANCE.md	docs/PROVENANCE.md
docs/THIRD_PARTY_NOTICES.md	docs/THIRD_PARTY_NOTICES.md
docs/licenses/BigSoundBank-0266-CC0.md	docs/licenses/BigSoundBank-0266-CC0.md
README-PSP.md	docs/PSP_PACKAGE.md
EOF
packaged_provenance_hash="$(unzip -p "${psp_package}" \
    BUILD-PROVENANCE.txt | sha256sum | awk '{print $1}')"
root_provenance_hash="$(sha256sum "${psp_dir}/BUILD-PROVENANCE.txt" | awk '{print $1}')"
[[ "${packaged_provenance_hash}" = "${root_provenance_hash}" ]] ||
    fail 'PSP install package build provenance does not match the validated artifact'
packaged_core_provenance_hash="$(unzip -p "${psp_package}" \
    CORE-BUILD-PROVENANCE.txt | sha256sum | awk '{print $1}')"
root_core_provenance_hash="$(sha256sum \
    "${psp_dir}/CORE-BUILD-PROVENANCE.txt" | awk '{print $1}')"
[[ "${packaged_core_provenance_hash}" = "${root_core_provenance_hash}" ]] ||
    fail 'PSP install package core provenance does not match the validated artifact'
packaged_pspsdk_license_hash="$(unzip -p "${psp_package}" \
    docs/licenses/PSPSDK-LICENSE | sha256sum | awk '{print $1}')"
[[ "${packaged_pspsdk_license_hash}" = \
    2a72b3d563b8e080dd2be9a963f44c8396ca615421833d3cffb6d126101c1c82 ]] ||
    fail 'PSP install package has the wrong pinned PSPSDK runtime license'

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

declare -A console_target console_archive console_image
console_target[ctr]=3ds
console_target[libnx]=switch
console_target[ngc]=gamecube
console_target[ps2]=ps2
console_target[psp1]=psp
console_target[vita]=vita
console_target[wii]=wii
console_target[wiiu]=wiiu
console_archive[ctr]=johnny_castaway_libretro_ctr.a
console_archive[libnx]=johnny_castaway_libretro_libnx.a
console_archive[ngc]=johnny_castaway_libretro_ngc.a
console_archive[ps2]=johnny_castaway_libretro_ps2.a
console_archive[psp1]=johnny_castaway_libretro_psp1.a
console_archive[vita]=johnny_castaway_libretro_vita.a
console_archive[wii]=johnny_castaway_libretro_wii.a
console_archive[wiiu]=johnny_castaway_libretro_wiiu.a
console_image[ctr]='devkitpro/devkitarm@sha256:15b79ce75822c289538d8153da5fa7aafe5e6adc32ad8a575a197beca0f0761b'
console_image[libnx]='devkitpro/devkita64@sha256:82575ea78651b530b2e232bb3799cfd1fe331514e053d5f724bb4b28191fb79d'
console_image[ngc]='devkitpro/devkitppc@sha256:4c919aa26151dd43d88ca28c922d1fe2409579a8ba60ef56517baf1abdfb1a48'
console_image[ps2]='ps2dev/ps2dev@sha256:29f42ffaadc62d2615db4a8c22df933579e31e8f8004546dd84629314802d789'
console_image[psp1]='ghcr.io/pspdev/pspdev@sha256:c9f1e60e8635d4df5ea246981b7473cbf48a9cf8457c1735f787821a684957f2'
console_image[vita]='vitasdk/vitasdk@sha256:9506538924a2f7d6e2505f919f3db285ceb297de5f57c211e5f60afa4b85ce85'
console_image[wii]="${console_image[ngc]}"
console_image[wiiu]="${console_image[ngc]}"

for console_platform in ctr libnx ngc ps2 psp1 vita wii wiiu; do
    console_archive_path="${console_base}/${console_platform}/${console_archive[${console_platform}]}"
    console_manifest="${console_base}/${console_platform}/BUILD-PROVENANCE.txt"
    must_file "${console_manifest}"
    console_archive_hash="$(sha256sum "${console_archive_path}" | awk '{print $1}')"
    cmp "${console_manifest}" <(
        printf '%s\n' \
            'Johnny Castaway console core provenance' \
            "Target: ${console_target[${console_platform}]}" \
            "Platform: ${console_platform}" \
            "Archive: ${console_archive[${console_platform}]}" \
            "Johnny Castaway commit: ${expected_sha}" \
            "Frontend version: ${display_version}" \
            'Tree state: clean' \
            "Core metadata SHA-256: ${expected_info_hash}" \
            "SDK image: ${console_image[${console_platform}]}" \
            "Archive SHA-256: ${console_archive_hash}" \
            'Archive metadata: deterministic ar rcsD timestamp=0 uid=0 gid=0'
    ) || fail "${console_platform} core provenance does not match the release contract"
done
cmp "${psp_dir}/CORE-BUILD-PROVENANCE.txt" \
    "${console_base}/psp1/BUILD-PROVENANCE.txt" ||
    fail 'PSP frontend core provenance differs from the validated console artifact'
validation_notes+=('All eight console archives passed deterministic timestamp/UID/GID metadata, member, target-machine, duplicate, undefined-symbol, and exact 25-symbol checks.')
validation_notes+=('All eight console archives have byte-exact clean-build manifests binding their SHA-256 to the expected commit, version, metadata, platform, and immutable SDK image.')
validation_notes+=('The PSP frontend passed ELF/PBP identity, embedded-option, checksum, provenance, legal-file, and Memory Stick install-layout checks.')
validation_notes+=('The PSP Memory Stick INFO copy and package-root core metadata are byte-identical to the expected commit.')

frontend_targets=(switch 3ds gamecube wiiu vita ps2)
frontend_base="${artifacts_dir}/johnny-castaway-console-cores/build/installable-frontends/out"
declare -A frontend_raw_outputs
declare -A frontend_platform
frontend_raw_outputs[switch]='JohnnyCastaway.nro retroarch_switch.elf'
frontend_raw_outputs[3ds]='JohnnyCastaway.3dsx JohnnyCastaway.smdh JohnnyCastaway.cia retroarch_3ds.elf'
frontend_raw_outputs[gamecube]='JohnnyCastaway.dol retroarch_ngc.elf'
frontend_raw_outputs[wiiu]='JohnnyCastaway.rpx meta.xml retroarch_wiiu.elf'
frontend_raw_outputs[vita]='JohnnyCastaway.vpk retroarch_vita.unstripped.elf'
frontend_raw_outputs[ps2]='JohnnyCastaway.elf'
frontend_platform[switch]=libnx
frontend_platform[3ds]=ctr
frontend_platform[gamecube]=ngc
frontend_platform[wiiu]=wiiu
frontend_platform[vita]=vita
frontend_platform[ps2]=ps2
frontend_project_files=(
    LICENSE
    CREDITS.md
    johnny_castaway_libretro.info
    docs/PROVENANCE.md
    docs/THIRD_PARTY_NOTICES.md
    docs/FRONTEND_SDK_NOTICES.md
    docs/licenses/BigSoundBank-0266-CC0.md
    docs/licenses/frontend/libctru-Zlib.txt
    docs/licenses/frontend/libnx-ISC.txt
    docs/licenses/frontend/wut-Zlib.txt
)

verify_frontend_checksums()
{
    local directory=$1 target=$2 unexpected_directory
    must_file "${directory}/SHA256SUMS"
    unexpected_directory="$(find "${directory}" -mindepth 1 -maxdepth 1 \
        -type d -print -quit)"
    [[ -z "${unexpected_directory}" ]] ||
        fail "${target} frontend output contains an unexpected directory"
    (
        cd "${directory}"
        sha256sum -c SHA256SUMS
    )
    cmp \
        <(awk 'NF == 2 { name=$2; sub(/^\*/, "", name); sub(/^\.\//, "", name); print name }' \
            "${directory}/SHA256SUMS" | sort) \
        <(find "${directory}" -maxdepth 1 -type f ! -name SHA256SUMS \
            -printf '%f\n' | sort) ||
        fail "${target} SHA256SUMS does not cover the exact frontend output set"
}

for frontend_target in "${frontend_targets[@]}"; do
    frontend_directory="${frontend_base}/${frontend_target}"
    [[ -d "${frontend_directory}" ]] ||
        fail "missing installable frontend directory: ${frontend_directory}"
    verify_frontend_checksums "${frontend_directory}" "${frontend_target}"
    for raw_output in ${frontend_raw_outputs[${frontend_target}]}; do
        must_file "${frontend_directory}/${raw_output}"
    done
    frontend_package="${frontend_directory}/johnny-castaway-${frontend_target}-frontend.zip"
    must_file "${frontend_package}"
    grep -Fxq "Frontend version: ${display_version}" \
        "${frontend_directory}/BUILD-PROVENANCE.txt" ||
        fail "${frontend_target} provenance has the wrong frontend version"
    grep -Fxq "Johnny Castaway commit: ${expected_sha} (clean)" \
        "${frontend_directory}/BUILD-PROVENANCE.txt" ||
        fail "${frontend_target} provenance does not name the exact clean commit"
    grep -Fxq "SOURCE_DATE_EPOCH: ${expected_epoch}" \
        "${frontend_directory}/BUILD-PROVENANCE.txt" ||
        fail "${frontend_target} provenance has the wrong source epoch"
    cmp "${frontend_directory}/BUILD-PROVENANCE.txt" \
        <(unzip -p "${frontend_package}" BUILD-PROVENANCE.txt) ||
        fail "${frontend_target} packaged provenance differs from the validated output"
    cmp "${console_base}/${frontend_platform[${frontend_target}]}/BUILD-PROVENANCE.txt" \
        <(unzip -p "${frontend_package}" CORE-BUILD-PROVENANCE.txt) ||
        fail "${frontend_target} packaged core provenance differs from the validated console artifact"
    for project_file in "${frontend_project_files[@]}"; do
        packaged_project_hash="$(unzip -p "${frontend_package}" \
            "${project_file}" | sha256sum | awk '{print $1}')"
        expected_project_hash="$(git -C "${project_root}" show \
            "${expected_sha}:${project_file}" | sha256sum | awk '{print $1}')"
        [[ "${packaged_project_hash}" = "${expected_project_hash}" ]] ||
            fail "${frontend_target} packaged ${project_file} does not match the expected commit"
    done
    packaged_install_hash="$(unzip -p "${frontend_package}" \
        README-INSTALL.md | sha256sum | awk '{print $1}')"
    expected_install_hash="$(git -C "${project_root}" show \
        "${expected_sha}:docs/INSTALLABLE_FRONTENDS.md" | sha256sum | awk '{print $1}')"
    [[ "${packaged_install_hash}" = "${expected_install_hash}" ]] ||
        fail "${frontend_target} packaged install guide does not match the expected commit"
    python3 "${project_root}/tools/check_installable_frontend.py" \
        --target "${frontend_target}" --artifact-dir "${frontend_directory}" \
        --package "${frontend_package}" --version "${display_version}" \
        --epoch "${expected_epoch}" --project-root "${project_root}" \
        --expected-commit "${expected_sha}"
done
validation_notes+=('Six installable console frontend directories passed complete SHA256SUMS coverage, raw-output, exact commit/version/epoch, deterministic ZIP, metadata, linked-core, provenance, and install-layout validation.')

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
principal[johnny-castaway-psp-frontend]='Memory Stick-ready PSP RetroArch EBOOT package'

for artifact_name in "${all_artifacts[@]}"; do
    if [[ "${artifact_name}" = johnny-castaway-psp-frontend ]]; then
        cp -- "${psp_package}" "${assets_dir}/${artifact_name}.zip"
    else
        (
            cd "${artifacts_dir}"
            zip -q -9 -r "${assets_dir}/${artifact_name}.zip" "${artifact_name}"
        )
    fi
    unzip -tq "${assets_dir}/${artifact_name}.zip"
done
for frontend_target in "${frontend_targets[@]}"; do
    frontend_package="${frontend_base}/${frontend_target}/johnny-castaway-${frontend_target}-frontend.zip"
    frontend_asset="${assets_dir}/johnny-castaway-${frontend_target}-frontend.zip"
    cp -- "${frontend_package}" "${frontend_asset}"
    cmp "${frontend_package}" "${frontend_asset}" ||
        fail "${frontend_target} direct release asset changed while copying"
    unzip -tq "${frontend_asset}"
done

expected_release_zips=()
for artifact_name in "${all_artifacts[@]}"; do
    expected_release_zips+=("${artifact_name}.zip")
done
for frontend_target in "${frontend_targets[@]}"; do
    expected_release_zips+=("johnny-castaway-${frontend_target}-frontend.zip")
done
mapfile -t expected_release_zips_sorted < <(
    printf '%s\n' "${expected_release_zips[@]}" | sort
)
mapfile -t actual_release_zips < <(
    find "${assets_dir}" -maxdepth 1 -type f -name '*.zip' -printf '%f\n' | sort
)
((${#actual_release_zips[@]} == 16)) ||
    fail "release ZIP count is ${#actual_release_zips[@]}, expected 16"
((${#expected_release_zips_sorted[@]} == ${#actual_release_zips[@]})) ||
    fail 'expected release ZIP list has the wrong size'
for index in "${!expected_release_zips_sorted[@]}"; do
    [[ "${expected_release_zips_sorted[index]}" = "${actual_release_zips[index]}" ]] ||
        fail 'release ZIP set does not match the sixteen-asset contract'
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
    printf '## Source Actions artifacts\n\n'
    printf '| Release ZIP | Artifact ID | Run | API bytes | Files | Binaries | ZIP bytes | SHA-256 | Contents |\n'
    printf '|---|---:|---:|---:|---:|---:|---:|---|---|\n'
    for artifact_name in "${all_artifacts[@]}"; do
        artifact_directory="${artifacts_dir}/${artifact_name}"
        file_count="$(find "${artifact_directory}" -type f | wc -l)"
        binary_count="$(find "${artifact_directory}" -type f \
            \( -name '*.so' -o -name '*.dll' -o -name '*.dylib' \
               -o -name '*.a' -o -name '*.bc' -o -name '*.wasm' \
               -o -name '*.elf' -o -name '*.PBP' \) | wc -l)"
        zip_path="${assets_dir}/${artifact_name}.zip"
        zip_bytes="$(stat -c '%s' "${zip_path}")"
        zip_hash="$(sha256sum "${zip_path}" | awk '{print $1}')"
        printf '| `%s.zip` | %s | %s | %s | %s | %s | %s | `%s` | %s |\n' \
            "${artifact_name}" "${artifact_id[${artifact_name}]}" \
            "${artifact_run[${artifact_name}]}" "${artifact_api_bytes[${artifact_name}]}" \
            "${file_count}" "${binary_count}" "${zip_bytes}" "${zip_hash}" \
            "${principal[${artifact_name}]}"
    done
    printf '\n## Direct installable console frontends\n\n'
    printf '| Target | Release ZIP | Source artifact | Raw validated outputs | Package files | ZIP bytes | SHA-256 |\n'
    printf '|---|---|---|---|---:|---:|---|\n'
    for frontend_target in "${frontend_targets[@]}"; do
        frontend_package="${assets_dir}/johnny-castaway-${frontend_target}-frontend.zip"
        frontend_files="$(unzip -Z1 "${frontend_package}" | \
            awk '!/\/$/ {count++} END {print count + 0}')"
        frontend_bytes="$(stat -c '%s' "${frontend_package}")"
        frontend_hash="$(sha256sum "${frontend_package}" | awk '{print $1}')"
        raw_display="$(printf '%s' "${frontend_raw_outputs[${frontend_target}]}" | \
            sed 's/ /`, `/g')"
        printf '| %s | `%s` | `johnny-castaway-console-cores` | `%s` | %s | %s | `%s` |\n' \
            "${frontend_target}" "johnny-castaway-${frontend_target}-frontend.zip" \
            "${raw_display}" "${frontend_files}" "${frontend_bytes}" \
            "${frontend_hash}"
    done
    printf '\n## Validation\n\n'
    for note in "${validation_notes[@]}"; do
        printf -- '- %s\n' "${note}"
    done
    printf -- '- The complete Web distribution passed `tools/check_web_dist.py`.\n'
    printf -- '- Every package ZIP passed `unzip -t`; `SHA256SUMS` verifies the sixteen release ZIPs assembled from ten source Actions artifacts.\n'
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
    printf 'Packages cover Linux, Windows, Android, macOS, iOS, tvOS, Emscripten/Web, PSP, Vita, PS2, Nintendo 3DS, GameCube, Wii, Wii U, and Switch. Seven consoles have direct installable frontend ZIPs: PSP, Switch, Nintendo 3DS, GameCube, Wii U, Vita, and PlayStation 2. Wii remains a validated static `.a` core for frontend linking, not a standalone application.\n\n'
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
