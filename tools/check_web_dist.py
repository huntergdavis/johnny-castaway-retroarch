#!/usr/bin/env python3
"""Validate the generated local Web Player distribution."""

from __future__ import annotations

import argparse
import pathlib
import re
import zipfile


REQUIRED_FILES = (
    "index.html",
    "style.css",
    "jc-web-player.js",
    "browserfs.min.js",
    "johnny_castaway_libretro.js",
    "johnny_castaway_libretro.wasm",
    "assets/frontend/bundle.zip",
    "BUILD-PROVENANCE.txt",
    "WEB_PLAYER_NOTICE.md",
    "CREDITS.md",
    "docs/PROVENANCE.md",
    "docs/THIRD_PARTY_NOTICES.md",
    "licenses/BrowserFS-license.md",
    "licenses/johnny-castaway-retroarch-GPL-3.0",
    "licenses/BigSoundBank-0266-CC0.md",
    "licenses/RetroArch-GPL-3.0",
    "licenses/retroarch-assets-CC-BY-4.0",
)


def fail(message: str) -> None:
    raise SystemExit(f"web distribution check failed: {message}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=pathlib.Path)
    args = parser.parse_args()
    root = args.directory.resolve()

    for relative in REQUIRED_FILES:
        if not (root / relative).is_file():
            fail(f"missing {relative}")

    wasm = (root / "johnny_castaway_libretro.wasm").read_bytes()
    if not wasm.startswith(b"\0asm"):
        fail("johnny_castaway_libretro.wasm has no WebAssembly magic")

    with zipfile.ZipFile(root / "assets/frontend/bundle.zip") as bundle:
        names = set(bundle.namelist())
        for prefix in ("assets/ozone/", "assets/pkg/", "assets/sounds/", "info/"):
            if not any(name.startswith(prefix) for name in names):
                fail(f"asset bundle has no {prefix}")

    html = (root / "index.html").read_text(encoding="utf-8")
    local_refs = re.findall(r'(?:src|href)="([^"#]+)"', html)
    for reference in local_refs:
        if "://" in reference:
            fail(f"index.html has an external dependency: {reference}")
        if not (root / reference).is_file():
            fail(f"index.html references missing file: {reference}")

    for path in root.rglob("*"):
        if path.is_file() and path.name.upper() in {"RESOURCE.MAP", "RESOURCE.001"}:
            fail(f"copyrighted game data was copied into dist: {path}")

    print(f"Web distribution check passed: {root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
