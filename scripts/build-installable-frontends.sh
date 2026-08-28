#!/usr/bin/env sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu

root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
build_base=${INSTALLABLE_FRONTEND_BUILD_DIR:-build/installable-frontends}
core_base=${INSTALLABLE_FRONTEND_CORE_DIR:-build/console}
jobs=${JOBS:-}
pull=0
offline=0
targets=
allow_dirty=${ALLOW_DIRTY:-0}

retroarch_url=https://github.com/libretro/RetroArch.git
retroarch_commit=96a1b1a9cf3f9166affcfd7df4323aa58d5c281a
devkita64_image='devkitpro/devkita64@sha256:82575ea78651b530b2e232bb3799cfd1fe331514e053d5f724bb4b28191fb79d'
devkitarm_image='devkitpro/devkitarm@sha256:15b79ce75822c289538d8153da5fa7aafe5e6adc32ad8a575a197beca0f0761b'
devkitppc_image='devkitpro/devkitppc@sha256:4c919aa26151dd43d88ca28c922d1fe2409579a8ba60ef56517baf1abdfb1a48'
vitasdk_image='vitasdk/vitasdk@sha256:9506538924a2f7d6e2505f919f3db285ceb297de5f57c211e5f60afa4b85ce85'
ps2dev_image='ps2dev/ps2dev@sha256:29f42ffaadc62d2615db4a8c22df933579e31e8f8004546dd84629314802d789'

switch_title='Johnny Castaway'
switch_author='Johnny Castaway contributors'
ctr_title='Johnny Castaway'
ctr_description='Johnny Castaway libretro'
ctr_author='Johnny Castaway contributors'
ctr_product_code='CTR-H-JCAST'
ctr_unique_id='0x4A430'
vita_title_id='JCASTAWAY'
vita_title='Johnny Castaway'

usage()
{
    printf 'usage: %s [--pull] [--offline] [--all|switch|3ds|gamecube|wiiu|vita|ps2 ...]\n' "$0"
}

for argument in "$@"; do
    case "$argument" in
        --pull) pull=1 ;;
        --offline) offline=1 ;;
        --all) targets='switch 3ds gamecube wiiu vita ps2' ;;
        --list)
            printf '%s\n' switch 3ds gamecube wiiu vita ps2
            exit 0
            ;;
        -*) usage >&2; exit 2 ;;
        *) targets="$targets $argument" ;;
    esac
done
if [ -z "$targets" ]; then
    targets='switch 3ds gamecube wiiu vita ps2'
fi
if [ "$pull" -eq 1 ] && [ "$offline" -eq 1 ]; then
    printf '%s\n' '--pull and --offline are mutually exclusive' >&2
    exit 2
fi
if [ -z "$jobs" ]; then
    jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1')
fi

case "$build_base" in
    build/*) ;;
    *) printf 'INSTALLABLE_FRONTEND_BUILD_DIR must stay below build/\n' >&2; exit 2 ;;
esac
case "$core_base" in
    build/*) ;;
    *) printf 'INSTALLABLE_FRONTEND_CORE_DIR must stay below build/\n' >&2; exit 2 ;;
esac
case "$build_base:$core_base" in
    *'/../'*|*'/..:'*|*':../'*|*':..')
        printf 'frontend build paths must not contain parent traversal\n' >&2
        exit 2
        ;;
esac

for command_name in cat cmp cp docker git grep mkdir python3 sed sha256sum tar; do
    command -v "$command_name" >/dev/null 2>&1 || {
        printf 'required command is unavailable: %s\n' "$command_name" >&2
        exit 1
    }
done
docker info >/dev/null 2>&1 || {
    printf 'Docker daemon is unavailable\n' >&2
    exit 1
}

case "$allow_dirty" in 0|1) ;; *) printf 'ALLOW_DIRTY must be 0 or 1\n' >&2; exit 2 ;; esac
project_commit=$(git -C "$root" rev-parse HEAD)
tree_status=$(git -C "$root" status --porcelain --untracked-files=all)
tree_state=clean
if [ -n "$tree_status" ]; then
    if [ "$allow_dirty" -ne 1 ]; then
        printf 'refusing publishable frontend build from a dirty tree; use ALLOW_DIRTY=1 only for local development\n' >&2
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
            if [ -f "$root/$path" ]; then sha256sum "$root/$path"; else printf 'missing %s\n' "$path"; fi
        done
    } | sha256sum | sed 's/[[:space:]].*$//'
}
source_fingerprint=$(project_fingerprint)

version=$(sed -n 's/^display_version = "\(.*\)"$/\1/p' \
    "$root/johnny_castaway_libretro.info")
info_sha=$(sha256sum "$root/johnny_castaway_libretro.info" | sed 's/[[:space:]].*$//')
case "$version" in
    [0-9]*.[0-9]*.[0-9]*) ;;
    *) printf 'display_version must be a numeric three-part version\n' >&2; exit 1 ;;
esac
old_ifs=$IFS
IFS=.
# shellcheck disable=SC2086 # Split the validated dotted version into three fields.
set -- $version
IFS=$old_ifs
if [ "$#" -ne 3 ]; then
    printf 'display_version must have exactly three numeric parts\n' >&2
    exit 1
fi
version_major=$1
version_minor=$2
version_micro=$3
case "$version_major:$version_minor:$version_micro" in
    *[!0-9:]*) printf 'display_version contains non-numeric fields\n' >&2; exit 1 ;;
esac
if [ "$version_major" -gt 63 ] || [ "$version_minor" -gt 63 ] || \
   [ "$version_micro" -gt 15 ]; then
    printf 'display_version exceeds Nintendo 3DS metadata limits\n' >&2
    exit 1
fi

source_date_epoch=${SOURCE_DATE_EPOCH:-$(git -C "$root" show -s --format=%ct HEAD)}
case "$source_date_epoch" in
    ''|*[!0-9]*) printf 'SOURCE_DATE_EPOCH must be a non-negative integer\n' >&2; exit 2 ;;
esac
export LC_ALL=C TZ=UTC SOURCE_DATE_EPOCH="$source_date_epoch"
umask 022

ensure_image()
{
    image=$1
    if docker image inspect "$image" >/dev/null 2>&1; then
        if [ "$pull" -eq 1 ]; then
            docker pull "$image"
        fi
    elif [ "$offline" -eq 1 ]; then
        printf 'required image is absent in offline mode: %s\n' "$image" >&2
        exit 1
    else
        docker pull "$image"
    fi
}

source_checkout="$root/$build_base/RetroArch-source"
prepare_source_checkout()
{
    mkdir -p "$root/$build_base"
    if [ -e "$source_checkout" ] && [ ! -d "$source_checkout/.git" ]; then
        printf '%s exists but is not a Git checkout\n' "$source_checkout" >&2
        exit 1
    fi
    if [ ! -d "$source_checkout/.git" ]; then
        git init -q "$source_checkout"
        git -C "$source_checkout" remote add origin "$retroarch_url"
    fi
    origin=$(git -C "$source_checkout" config --get remote.origin.url || true)
    if [ "$origin" != "$retroarch_url" ]; then
        printf 'unexpected RetroArch origin: %s\n' "$origin" >&2
        exit 1
    fi
    if [ -n "$(git -C "$source_checkout" status --porcelain --untracked-files=all)" ]; then
        printf 'changes found in generated RetroArch source checkout\n' >&2
        exit 1
    fi
    if git -C "$source_checkout" cat-file -e "$retroarch_commit^{commit}" 2>/dev/null; then
        if [ "$offline" -eq 0 ]; then
            git -C "$source_checkout" fetch --depth 1 origin "$retroarch_commit"
        fi
    elif [ "$offline" -eq 1 ]; then
        printf 'pinned RetroArch commit is absent in offline mode\n' >&2
        exit 1
    else
        git -C "$source_checkout" fetch --depth 1 origin "$retroarch_commit"
    fi
    git -C "$source_checkout" checkout -q --detach "$retroarch_commit"
    if [ "$(git -C "$source_checkout" rev-parse HEAD)" != "$retroarch_commit" ]; then
        printf 'RetroArch source checkout did not resolve to the pin\n' >&2
        exit 1
    fi
    if [ -n "$(git -C "$source_checkout" status --porcelain --untracked-files=all)" ]; then
        printf 'generated RetroArch source checkout is not pristine after checkout\n' >&2
        exit 1
    fi
}

prepare_worktree()
{
    target=$1
    worktree="$root/$build_base/work/$target/RetroArch"
    case "$worktree" in
        "$root"/build/*/work/"$target"/RetroArch) ;;
        *) printf 'refusing to recreate unexpected worktree path: %s\n' "$worktree" >&2; exit 1 ;;
    esac
    if [ -e "$worktree" ]; then
        if git -C "$worktree" rev-parse --git-dir >/dev/null 2>&1; then
            git -C "$source_checkout" worktree remove --force "$worktree"
        else
            rm -rf -- "${worktree:?}"
        fi
    fi
    git -C "$source_checkout" worktree prune
    mkdir -p "$(dirname "$worktree")"
    git -C "$source_checkout" worktree add -q --detach "$worktree" "$retroarch_commit"
    if [ "$(git -C "$worktree" rev-parse HEAD)" != "$retroarch_commit" ]; then
        printf 'target worktree is not at the pinned RetroArch commit: %s\n' "$worktree" >&2
        exit 1
    fi
    if [ -n "$(git -C "$worktree" status --porcelain --untracked-files=all)" ]; then
        printf 'fresh generated target worktree is not pristine: %s\n' "$worktree" >&2
        exit 1
    fi
    printf '%s\n' "$worktree"
}

target_configuration()
{
    target=$1
    case "$target" in
        switch)
            image=$devkita64_image
            platform=libnx
            archive=johnny_castaway_libretro_libnx.a
            expected_archive=libretro_libnx.a
            machine=AArch64
            ;;
        3ds)
            image=$devkitarm_image
            platform=ctr
            archive=johnny_castaway_libretro_ctr.a
            expected_archive=libretro_ctr.a
            machine=ARM
            ;;
        vita)
            image=$vitasdk_image
            platform=vita
            archive=johnny_castaway_libretro_vita.a
            expected_archive=libretro_vita.a
            machine=ARM
            ;;
        gamecube)
            image=$devkitppc_image
            platform=ngc
            archive=johnny_castaway_libretro_ngc.a
            expected_archive=libretro_ngc.a
            machine=PowerPC
            ;;
        wiiu)
            image=$devkitppc_image
            platform=wiiu
            archive=johnny_castaway_libretro_wiiu.a
            expected_archive=libretro_wiiu.a
            machine=PowerPC
            ;;
        ps2)
            image=$ps2dev_image
            platform=ps2
            archive=johnny_castaway_libretro_ps2.a
            expected_archive=libretro_ps2.a
            machine='MIPS R3000'
            ;;
        *)
            printf 'unsupported installable frontend target: %s\n' "$target" >&2
            usage >&2
            exit 2
            ;;
    esac
}

prepare_core()
{
    target=$1
    core_path="$root/$core_base/$platform/$archive"
    core_manifest="$root/$core_base/$platform/BUILD-PROVENANCE.txt"
    if [ ! -s "$core_path" ] || [ ! -s "$core_manifest" ]; then
        if [ "$offline" -eq 1 ]; then
            printf 'validated core or provenance is absent in offline mode: %s\n' "$core_path" >&2
            exit 1
        fi
        ALLOW_DIRTY="$allow_dirty" CONSOLE_BUILD_DIR="$core_base" JOBS="$jobs" \
            "$root/scripts/build-console-cores.sh" "$target"
    fi
    python3 "$root/tools/check_console_archive.py" \
        --machine "$machine" "$core_path"
    core_sha=$(sha256sum "$core_path" | sed 's/[[:space:]].*$//')
    core_commit=$(sed -n 's/^Johnny Castaway commit: //p' "$core_manifest")
    if [ "${#core_commit}" -ne 40 ] || [ "$core_commit" != "$project_commit" ]; then
        printf 'core provenance has an invalid or mismatched Johnny commit: %s\n' \
            "$core_commit" >&2
        exit 1
    fi
    case "$core_commit" in
        *[!0-9a-f]*) printf 'core provenance commit is not lowercase hexadecimal\n' >&2; exit 1 ;;
    esac
    expected_core_manifest=$(printf '%s\n' \
        'Johnny Castaway console core provenance' \
        "Target: $target" \
        "Platform: $platform" \
        "Archive: $archive" \
        "Johnny Castaway commit: $core_commit" \
        "Frontend version: $version" \
        "Tree state: $tree_state" \
        "Core metadata SHA-256: $info_sha" \
        "SDK image: $image" \
        "Archive SHA-256: $core_sha" \
        'Archive metadata: deterministic ar rcsD timestamp=0 uid=0 gid=0')
    if [ "$(cat "$core_manifest")" != "$expected_core_manifest" ]; then
        printf 'core provenance does not exactly match the canonical build record: %s\n' \
            "$core_manifest" >&2
        exit 1
    fi
}

verify_project_snapshot()
{
    if [ "$(git -C "$root" rev-parse HEAD)" != "$project_commit" ] || \
       [ "$(sha256sum "$root/johnny_castaway_libretro.info" | sed 's/[[:space:]].*$//')" != "$info_sha" ] || \
       [ "$(project_fingerprint)" != "$source_fingerprint" ]; then
        printf 'project source changed after frontend build inputs were captured\n' >&2
        exit 1
    fi
}

copy_common_package_files()
{
    checkout=$1
    package_root=$2
    mkdir -p "$package_root/docs/licenses"
    cp "$root/LICENSE" "$root/CREDITS.md" \
        "$root/johnny_castaway_libretro.info" "$package_root/"
    cp "$root/docs/INSTALLABLE_FRONTENDS.md" "$package_root/README-INSTALL.md"
    cp "$root/docs/PROVENANCE.md" "$root/docs/THIRD_PARTY_NOTICES.md" \
        "$root/docs/FRONTEND_SDK_NOTICES.md" "$package_root/docs/"
    cp "$root/docs/licenses/BigSoundBank-0266-CC0.md" \
        "$package_root/docs/licenses/"
    cp -R "$root/docs/licenses/frontend" "$package_root/docs/licenses/"
    cp "$checkout/COPYING" "$package_root/docs/licenses/RetroArch-GPL-3.0.txt"
}

copy_sdk_licenses()
{
    target=$1
    image=$2
    package_root=$3
    docker run --rm --read-only --network none --security-opt no-new-privileges \
        --cap-drop ALL --tmpfs /tmp --user "$(id -u):$(id -g)" \
        -e JC_TARGET="$target" \
        -v "$package_root:/package" --entrypoint sh "$image" -lc '
            set -eu
            destination=/package/docs/licenses/sdk
            copy_source()
            {
                label=$1
                source=$2
                if [ -d "$source" ]; then
                    mkdir -p "$destination/$label"
                    cp -RL "$source"/. "$destination/$label/"
                elif [ -f "$source" ]; then
                    mkdir -p "$destination/$label"
                    cp "$source" "$destination/$label/"
                fi
            }
            case "$JC_TARGET" in
                switch)
                    copy_source switch-portlibs /opt/devkitpro/portlibs/switch/licenses
                    ;;
                3ds)
                    copy_source 3ds-portlibs /opt/devkitpro/portlibs/3ds/licenses
                    ;;
                vita)
                    copy_source vita-runtime /usr/local/vitasdk/arm-vita-eabi/share/licenses
                    ;;
                gamecube)
                    copy_source libogc /opt/devkitpro/libogc/LICENSE
                    copy_source gamecube-portlibs /opt/devkitpro/portlibs/gamecube/licenses
                    copy_source ppc-portlibs /opt/devkitpro/portlibs/ppc/licenses
                    copy_source gamecube-shared-portlibs \
                        /opt/devkitpro/portlibs/gamecube/share/licenses
                    ;;
                wiiu)
                    copy_source wiiu-portlibs /opt/devkitpro/portlibs/wiiu/licenses
                    copy_source ppc-portlibs /opt/devkitpro/portlibs/ppc/licenses
                    copy_source wiiu-shared-portlibs \
                        /opt/devkitpro/portlibs/wiiu/share/licenses
                    ;;
                ps2)
                    copy_source ps2sdk /usr/local/ps2dev/ps2sdk/LICENSE
                    copy_source ps2-port-licenses \
                        /usr/local/ps2dev/ps2sdk/ports/share/licenses
                    doc_root=/usr/local/ps2dev/ps2sdk/ports/share/doc
                    if [ -d "$doc_root" ]; then
                        find "$doc_root" -type f \( -name "LICENSE*" -o -name "COPYING*" \) |
                        while IFS= read -r source; do
                            relative=${source#"$doc_root"/}
                            mkdir -p "$destination/ps2-port-docs/$(dirname "$relative")"
                            cp "$source" "$destination/ps2-port-docs/$relative"
                        done
                    fi
                    ;;
            esac
        '
}

write_provenance()
{
    target=$1
    image=$2
    core_path=$3
    artifact_dir=$4
    package_root=$5
    shift 5
    {
        printf 'Johnny Castaway installable RetroArch frontend build provenance\n'
        printf 'Target: %s\n' "$target"
        printf 'Frontend version: %s\n' "$version"
        printf 'Johnny Castaway commit: %s (%s)\n' "$project_commit" "$tree_state"
        printf 'Source fingerprint SHA-256: %s\n' "$source_fingerprint"
        printf 'Core metadata SHA-256: %s\n' "$info_sha"
        printf 'RetroArch commit: %s\n' "$retroarch_commit"
        printf 'SDK image: %s\n' "$image"
        printf 'SOURCE_DATE_EPOCH: %s\n' "$source_date_epoch"
        printf 'Core archive SHA-256: %s\n' "$core_sha"
        while [ "$#" -gt 0 ]; do
            artifact=$1
            shift
            artifact_sha=$(sha256sum "$artifact_dir/$artifact" | sed 's/[[:space:]].*$//')
            printf '%s SHA-256: %s\n' "$artifact" "$artifact_sha"
        done
        case "$target" in
            switch)
                printf 'Metadata: title=%s; author=%s; version=%s\n' \
                    "$switch_title" "$switch_author" "$version"
                ;;
            3ds)
                printf 'Metadata: title=%s; product=%s; unique-id=%s; version=%s.%s.%s\n' \
                    "$ctr_title" "$ctr_product_code" "$ctr_unique_id" \
                    "$version_major" "$version_minor" "$version_micro"
                ;;
            vita)
                printf 'Metadata: title=%s; title-id=%s\n' "$vita_title" "$vita_title_id"
                ;;
            gamecube)
                printf 'RetroArch flags: EXTERNAL_LIBOGC=1 HAVE_THREADS=0 GX_PTHREAD_LEGACY=0 BIG_STACK=0\n'
                ;;
            wiiu)
                printf 'RPX conversion: pinned RetroArch wut elf2rpl, uncompressed large RPX target\n'
                printf 'Layout: wiiu/apps/JohnnyCastaway/JohnnyCastaway.rpx\n'
                ;;
            ps2)
                printf 'Layout: executable, cores, info, and retroarch directories share one root\n'
                ;;
        esac
        printf 'Original Sierra/Dynamix content included: no\n'
    } >"$package_root/BUILD-PROVENANCE.txt"
    cp "$package_root/BUILD-PROVENANCE.txt" "$artifact_dir/BUILD-PROVENANCE.txt"
}

package_and_validate()
{
    target=$1
    artifact_dir=$2
    package_root=$3
    package_zip="$artifact_dir/johnny-castaway-$target-frontend.zip"
    reproducibility_copy="$artifact_dir/.reproducibility-check.zip"
    python3 "$root/tools/make_deterministic_zip.py" \
        --directory "$package_root" --output "$package_zip" --epoch "$source_date_epoch"
    python3 "$root/tools/make_deterministic_zip.py" \
        --directory "$package_root" --output "$reproducibility_copy" \
        --epoch "$source_date_epoch"
    cmp "$package_zip" "$reproducibility_copy"
    rm -f "$reproducibility_copy"
    if [ "$tree_state" = clean ]; then
        python3 "$root/tools/check_installable_frontend.py" \
            --target "$target" --artifact-dir "$artifact_dir" \
            --package "$package_zip" --version "$version" --epoch "$source_date_epoch" \
            --project-root "$root" --expected-commit "$core_commit"
    else
        printf 'developer-only dirty build: commit/legal byte validation is deferred to a clean rebuild\n' >&2
        python3 "$root/tools/check_installable_frontend.py" \
            --target "$target" --artifact-dir "$artifact_dir" \
            --package "$package_zip" --version "$version" --epoch "$source_date_epoch"
    fi
    (
        cd "$artifact_dir"
        sha256sum ./* >SHA256SUMS
        sha256sum -c SHA256SUMS
    )
    printf 'Validated %s frontend artifacts: %s\n' "$target" "$artifact_dir"
}

build_switch()
{
    checkout=$1
    core_path=$2
    artifact_dir=$3
    docker run --rm --read-only --network none --security-opt no-new-privileges \
        --cap-drop ALL --tmpfs /tmp --user "$(id -u):$(id -g)" \
        -e SOURCE_DATE_EPOCH="$source_date_epoch" -e TZ=UTC \
        -v "$checkout:/src" -w /src "$image" \
        make -f Makefile.libnx clean
    cp "$core_path" "$checkout/$expected_archive"
    docker run --rm --read-only --network none --security-opt no-new-privileges \
        --cap-drop ALL --tmpfs /tmp --user "$(id -u):$(id -g)" \
        -e SOURCE_DATE_EPOCH="$source_date_epoch" -e TZ=UTC \
        -v "$checkout:/src" -w /src "$image" \
        make -j"$jobs" -f Makefile.libnx \
        APP_TITLE="$switch_title" APP_AUTHOR="$switch_author" APP_VERSION="$version"
    cp "$checkout/retroarch_switch.nro" "$artifact_dir/JohnnyCastaway.nro"
    cp "$checkout/retroarch_switch.elf" "$artifact_dir/retroarch_switch.elf"
}

build_3ds()
{
    checkout=$1
    core_path=$2
    artifact_dir=$3
    docker run --rm --read-only --network none --security-opt no-new-privileges \
        --cap-drop ALL --tmpfs /tmp --user "$(id -u):$(id -g)" \
        -e SOURCE_DATE_EPOCH="$source_date_epoch" -e TZ=UTC \
        -v "$checkout:/src" -w /src "$image" \
        make -f Makefile.ctr USE_CTRULIB_2=1 DEVKITTOOLS=/opt/devkitpro/tools clean
    cp "$core_path" "$checkout/$expected_archive"
    docker run --rm --read-only --network none --security-opt no-new-privileges \
        --cap-drop ALL --tmpfs /tmp --user "$(id -u):$(id -g)" \
        -e SOURCE_DATE_EPOCH="$source_date_epoch" -e TZ=UTC \
        -v "$checkout:/src" -w /src "$image" \
        make -j"$jobs" -f Makefile.ctr USE_CTRULIB_2=1 \
        DEVKITTOOLS=/opt/devkitpro/tools \
        APP_TITLE="$ctr_title" APP_DESCRIPTION="$ctr_description" \
        APP_AUTHOR="$ctr_author" APP_PRODUCT_CODE="$ctr_product_code" \
        APP_UNIQUE_ID="$ctr_unique_id" APP_VERSION_MAJOR="$version_major" \
        APP_VERSION_MINOR="$version_minor" APP_VERSION_MICRO="$version_micro"
    cp "$checkout/retroarch_3ds.3dsx" "$artifact_dir/JohnnyCastaway.3dsx"
    cp "$checkout/retroarch_3ds.smdh" "$artifact_dir/JohnnyCastaway.smdh"
    cp "$checkout/retroarch_3ds.cia" "$artifact_dir/JohnnyCastaway.cia"
    cp "$checkout/retroarch_3ds.elf" "$artifact_dir/retroarch_3ds.elf"
}

build_vita()
{
    checkout=$1
    core_path=$2
    artifact_dir=$3
    docker run --rm --read-only --network none --security-opt no-new-privileges \
        --cap-drop ALL --tmpfs /tmp --user "$(id -u):$(id -g)" \
        -e SOURCE_DATE_EPOCH="$source_date_epoch" -e TZ=UTC \
        -v "$checkout:/src" -w /src "$image" \
        make -f Makefile.vita clean
    cp "$core_path" "$checkout/$expected_archive"
    docker run --rm --read-only --network none --security-opt no-new-privileges \
        --cap-drop ALL --tmpfs /tmp --user "$(id -u):$(id -g)" \
        -e SOURCE_DATE_EPOCH="$source_date_epoch" -e TZ=UTC \
        -v "$checkout:/src" -w /src "$image" \
        make -j"$jobs" -f Makefile.vita \
        VITA_TITLE_ID="$vita_title_id" VITA_TITLE_NAME="$vita_title"
    python3 "$root/tools/make_deterministic_zip.py" \
        --source-zip "$checkout/retroarch_vita.vpk" \
        --output "$artifact_dir/JohnnyCastaway.vpk" --epoch "$source_date_epoch"
    python3 "$root/tools/make_deterministic_zip.py" \
        --source-zip "$checkout/retroarch_vita.vpk" \
        --output "$artifact_dir/.vpk-reproducibility-check" --epoch "$source_date_epoch"
    cmp "$artifact_dir/JohnnyCastaway.vpk" "$artifact_dir/.vpk-reproducibility-check"
    rm -f "$artifact_dir/.vpk-reproducibility-check"
    cp "$checkout/retroarch_vita.elf.unstripped.elf" \
        "$artifact_dir/retroarch_vita.unstripped.elf"
}

build_ps2()
{
    checkout=$1
    core_path=$2
    artifact_dir=$3
    cp "$core_path" "$checkout/$expected_archive"
    ps2_make=$(OFFLINE="$offline" "$root/scripts/prepare-pinned-ps2-make.sh")
    docker run --rm --read-only --network none --security-opt no-new-privileges \
        --cap-drop ALL --tmpfs /tmp --user "$(id -u):$(id -g)" \
        -e JC_JOBS="$jobs" \
        -e SOURCE_DATE_EPOCH="$source_date_epoch" -e TZ=UTC \
        -v "$ps2_make:/tool/make:ro" -v "$checkout:/src" -w /src "$image" sh -lc '
            export PATH="/tool:/usr/local/ps2dev/ee/bin:/usr/local/ps2dev/bin:$PATH"
            make -f Makefile.ps2 clean
            make -j"$JC_JOBS" -f Makefile.ps2
        '
    cp "$checkout/retroarch_ps2.elf" "$artifact_dir/JohnnyCastaway.elf"
}

build_gamecube()
{
    checkout=$1
    core_path=$2
    artifact_dir=$3
    docker run --rm --read-only --network none --security-opt no-new-privileges \
        --cap-drop ALL --tmpfs /tmp --user "$(id -u):$(id -g)" \
        -e SOURCE_DATE_EPOCH="$source_date_epoch" -e TZ=UTC \
        -v "$checkout:/src" -w /src "$image" \
        make -f Makefile.griffin platform=ngc EXTERNAL_LIBOGC=1 \
        HAVE_THREADS=0 GX_PTHREAD_LEGACY=0 BIG_STACK=0 clean
    cp "$core_path" "$checkout/$expected_archive"
    docker run --rm --read-only --network none --security-opt no-new-privileges \
        --cap-drop ALL --tmpfs /tmp --user "$(id -u):$(id -g)" \
        -e SOURCE_DATE_EPOCH="$source_date_epoch" -e TZ=UTC \
        -v "$checkout:/src" -w /src "$image" \
        make -j"$jobs" -f Makefile.griffin platform=ngc EXTERNAL_LIBOGC=1 \
        HAVE_THREADS=0 GX_PTHREAD_LEGACY=0 BIG_STACK=0
    cp "$checkout/retroarch_ngc.dol" "$artifact_dir/JohnnyCastaway.dol"
    cp "$checkout/retroarch_ngc.elf" "$artifact_dir/retroarch_ngc.elf"
}

build_wiiu()
{
    checkout=$1
    core_path=$2
    artifact_dir=$3
    docker run --rm --read-only --network none --security-opt no-new-privileges \
        --cap-drop ALL --tmpfs /tmp --user "$(id -u):$(id -g)" \
        -e SOURCE_DATE_EPOCH="$source_date_epoch" -e TZ=UTC \
        -v "$checkout:/src" -w /src "$image" make -f Makefile.wiiu clean
    cp "$core_path" "$checkout/$expected_archive"
    docker run --rm --read-only --network none --security-opt no-new-privileges \
        --cap-drop ALL --tmpfs /tmp --user "$(id -u):$(id -g)" \
        -e SOURCE_DATE_EPOCH="$source_date_epoch" -e TZ=UTC \
        -v "$checkout:/src" -w /src "$image" \
        make -j"$jobs" -f Makefile.wiiu \
        objs/wiiu/retroarch_wiiu.large.rpx retroarch_wiiu.elf
    cp "$checkout/objs/wiiu/retroarch_wiiu.large.rpx" \
        "$artifact_dir/JohnnyCastaway.rpx"
    cp "$checkout/retroarch_wiiu.elf" "$artifact_dir/retroarch_wiiu.elf"
    {
        printf '%s\n' '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        printf '%s\n' '<app version="1">'
        printf '  <name>%s</name>\n' 'Johnny Castaway'
        printf '  <coder>%s</coder>\n' 'Johnny Castaway contributors'
        printf '  <version>%s</version>\n' "$version"
        printf '  <short_description>%s</short_description>\n' 'Johnny Castaway for RetroArch'
        printf '  <long_description>%s</long_description>\n' \
            'Runs legally owned Johnny Castaway data with the statically linked libretro core.'
        printf '%s\n' '</app>'
    } >"$artifact_dir/meta.xml"
}

prepare_source_checkout
for target in $targets; do
    target_configuration "$target"
    ensure_image "$image"
    prepare_core "$target"
    checkout=$(prepare_worktree "$target")
    artifact_dir="$root/$build_base/out/$target"
    package_root="$root/$build_base/package-root/$target"
    rm -rf "$artifact_dir" "$package_root"
    mkdir -p "$artifact_dir" "$package_root"

    case "$target" in
        switch)
            build_switch "$checkout" "$core_path" "$artifact_dir"
            mkdir -p "$package_root/switch/JohnnyCastaway"
            cp "$artifact_dir/JohnnyCastaway.nro" \
                "$package_root/switch/JohnnyCastaway/JohnnyCastaway.nro"
            artifact_names='JohnnyCastaway.nro retroarch_switch.elf'
            ;;
        3ds)
            build_3ds "$checkout" "$core_path" "$artifact_dir"
            mkdir -p "$package_root/3ds/JohnnyCastaway" "$package_root/cia"
            cp "$artifact_dir/JohnnyCastaway.3dsx" \
                "$package_root/3ds/JohnnyCastaway/JohnnyCastaway.3dsx"
            cp "$artifact_dir/JohnnyCastaway.smdh" \
                "$package_root/3ds/JohnnyCastaway/JohnnyCastaway.smdh"
            cp "$artifact_dir/JohnnyCastaway.cia" \
                "$package_root/cia/JohnnyCastaway.cia"
            artifact_names='JohnnyCastaway.3dsx JohnnyCastaway.smdh JohnnyCastaway.cia retroarch_3ds.elf'
            ;;
        vita)
            build_vita "$checkout" "$core_path" "$artifact_dir"
            cp "$artifact_dir/JohnnyCastaway.vpk" "$package_root/JohnnyCastaway.vpk"
            artifact_names='JohnnyCastaway.vpk retroarch_vita.unstripped.elf'
            ;;
        gamecube)
            build_gamecube "$checkout" "$core_path" "$artifact_dir"
            mkdir -p "$package_root/apps/JohnnyCastaway"
            cp "$artifact_dir/JohnnyCastaway.dol" \
                "$package_root/apps/JohnnyCastaway/boot.dol"
            artifact_names='JohnnyCastaway.dol retroarch_ngc.elf'
            ;;
        wiiu)
            build_wiiu "$checkout" "$core_path" "$artifact_dir"
            mkdir -p "$package_root/wiiu/apps/JohnnyCastaway"
            cp "$artifact_dir/JohnnyCastaway.rpx" \
                "$package_root/wiiu/apps/JohnnyCastaway/JohnnyCastaway.rpx"
            cp "$artifact_dir/meta.xml" \
                "$package_root/wiiu/apps/JohnnyCastaway/meta.xml"
            artifact_names='JohnnyCastaway.rpx meta.xml retroarch_wiiu.elf'
            ;;
        ps2)
            build_ps2 "$checkout" "$core_path" "$artifact_dir"
            mkdir -p "$package_root/JohnnyCastaway/cores" \
                "$package_root/JohnnyCastaway/info" \
                "$package_root/JohnnyCastaway/retroarch"
            cp "$artifact_dir/JohnnyCastaway.elf" \
                "$package_root/JohnnyCastaway/retroarch_ps2.elf"
            cp "$root/johnny_castaway_libretro.info" \
                "$package_root/JohnnyCastaway/info/"
            printf '%s\n' \
                'The Johnny core is statically linked; keep this directory for RetroArch layout compatibility.' \
                >"$package_root/JohnnyCastaway/cores/README.txt"
            printf '%s\n' \
                'Place a legally owned RESOURCE.MAP and RESOURCE.001 together in this directory.' \
                'Optional supported sound<ID>.wav files also stay here.' \
                >"$package_root/JohnnyCastaway/retroarch/PLACE-CONTENT-HERE.txt"
            artifact_names='JohnnyCastaway.elf'
            ;;
    esac

    copy_common_package_files "$checkout" "$package_root"
    cp "$core_manifest" "$package_root/CORE-BUILD-PROVENANCE.txt"
    copy_sdk_licenses "$target" "$image" "$package_root"
    verify_project_snapshot
    # shellcheck disable=SC2086 # artifact_names is a fixed internal filename list.
    write_provenance "$target" "$image" "$core_path" "$artifact_dir" \
        "$package_root" $artifact_names
    package_and_validate "$target" "$artifact_dir" "$package_root"
    verify_project_snapshot
done
