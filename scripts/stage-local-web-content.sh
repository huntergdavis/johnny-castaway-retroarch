#!/usr/bin/env sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copy user-owned Johnny data into the ignored local Web test distribution.

set -eu

project_root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
source_directory=${1:-}
dist_directory=${JC_WEB_DIST:-"$project_root/build/web-player/dist"}
destination="$dist_directory/local-content"

if [ -z "$source_directory" ]; then
    printf 'usage: %s /directory/containing/RESOURCE.MAP/RESOURCE.001\n' "$0" >&2
    exit 2
fi
if [ ! -f "$source_directory/RESOURCE.MAP" ] ||
   [ ! -f "$source_directory/RESOURCE.001" ]; then
    printf 'error: source directory must contain RESOURCE.MAP and RESOURCE.001\n' >&2
    exit 1
fi
if [ ! -f "$dist_directory/index.html" ]; then
    printf 'error: Web Player is not built; run scripts/build-web-player.sh first\n' >&2
    exit 1
fi

mkdir -p "$destination"
cp "$source_directory/RESOURCE.MAP" "$destination/RESOURCE.MAP"
cp "$source_directory/RESOURCE.001" "$destination/RESOURCE.001"

# Original sound effects are optional siblings in the PS1-port convention.
# Clear numbered samples from an earlier staging run so data sets cannot mix.
sample_id=0
sample_count=0
while [ "$sample_id" -lt 25 ]; do
    sample_name="sound${sample_id}.wav"
    rm -f "$destination/$sample_name"
    if [ "$sample_id" -ne 11 ] && [ "$sample_id" -ne 13 ] &&
       [ -f "$source_directory/$sample_name" ]; then
        cp "$source_directory/$sample_name" "$destination/$sample_name"
        sample_count=$((sample_count + 1))
    fi
    sample_id=$((sample_id + 1))
done

printf 'Staged local-only Johnny data in %s\n' "$destination"
printf 'Staged %s optional original sound-effect WAVs\n' "$sample_count"
printf 'These files are ignored by Git but are downloadable by clients of the test server.\n'
