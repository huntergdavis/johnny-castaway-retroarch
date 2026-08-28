#!/usr/bin/env bash
# Serve the generated Web Player on localhost with browser isolation headers.

set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dist_dir="${JC_WEB_DIST:-${project_root}/build/web-player/dist}"

exec python3 "${project_root}/tools/serve_web.py" \
    --directory "${dist_dir}" "$@"
