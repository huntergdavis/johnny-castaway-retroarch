#!/usr/bin/env python3
"""Validate the generated local Web Player distribution."""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import re
import stat
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
MAX_BUNDLE_MEMBERS = 65536
MAX_BUNDLE_MEMBER_SIZE = 256 * 1024 * 1024
MAX_BUNDLE_TOTAL_SIZE = 1024 * 1024 * 1024


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

    provenance = (root / "BUILD-PROVENANCE.txt").read_text(encoding="utf-8")
    commit_match = re.search(
        r"^Johnny Castaway commit: ([0-9a-f]{40}) "
        r"\((clean|dirty-developer-mode)\)$",
        provenance,
        re.MULTILINE,
    )
    version_match = re.search(
        r"^Frontend version: ([0-9]+\.[0-9]+\.[0-9]+)$",
        provenance,
        re.MULTILINE,
    )
    tree_match = re.search(
        r"^Tree state: (clean|dirty-developer-mode)$", provenance, re.MULTILINE
    )
    info_hash_match = re.search(
        r"^Core metadata SHA-256: ([0-9a-f]{64})$", provenance, re.MULTILINE
    )
    if not all((commit_match, version_match, tree_match, info_hash_match)):
        fail("BUILD-PROVENANCE.txt lacks exact Johnny commit/version/tree/info fields")
    assert commit_match and version_match and tree_match and info_hash_match
    if commit_match.group(2) != tree_match.group(1):
        fail("BUILD-PROVENANCE.txt has inconsistent tree-state fields")

    for label, relative in (
        ("BrowserFS", "browserfs.min.js"),
        ("JavaScript", "johnny_castaway_libretro.js"),
        ("WebAssembly", "johnny_castaway_libretro.wasm"),
        ("Asset bundle", "assets/frontend/bundle.zip"),
        ("RetroArch license", "licenses/RetroArch-GPL-3.0"),
        (
            "retroarch-assets license",
            "licenses/retroarch-assets-CC-BY-4.0",
        ),
    ):
        expected_match = re.search(
            rf"^{label} SHA-256: ([0-9a-f]{{64}})$", provenance, re.MULTILINE
        )
        actual = hashlib.sha256((root / relative).read_bytes()).hexdigest()
        if not expected_match or expected_match.group(1) != actual:
            fail(f"{label} hash does not match BUILD-PROVENANCE.txt")

    with zipfile.ZipFile(root / "assets/frontend/bundle.zip") as bundle:
        infos = bundle.infolist()
        if len(infos) > MAX_BUNDLE_MEMBERS:
            fail(f"asset bundle exceeds the {MAX_BUNDLE_MEMBERS}-member limit")
        names: set[str] = set()
        folded: set[str] = set()
        total_size = 0
        for info in infos:
            name = info.filename[:-1] if info.is_dir() else info.filename
            parts = name.split("/")
            if (
                not name
                or name.startswith("/")
                or "\\" in name
                or ":" in name
                or any(part in ("", ".", "..") for part in parts)
            ):
                fail(f"asset bundle has an unsafe member: {info.filename!r}")
            folded_name = name.casefold()
            if name in names or folded_name in folded:
                fail(f"asset bundle has a duplicate or case-fold collision: {name}")
            names.add(name)
            folded.add(folded_name)
            if info.flag_bits & 0x1:
                fail(f"asset bundle has an encrypted member: {name}")
            unix_mode = info.external_attr >> 16
            if stat.S_IFMT(unix_mode) == stat.S_IFLNK:
                fail(f"asset bundle has a symlink member: {name}")
            if info.file_size > MAX_BUNDLE_MEMBER_SIZE:
                fail(f"asset bundle member is too large: {name}")
            total_size += info.file_size
            if total_size > MAX_BUNDLE_TOTAL_SIZE:
                fail("asset bundle exceeds its aggregate size limit")
        bad_member = bundle.testzip()
        if bad_member:
            fail(f"asset bundle has a corrupt member: {bad_member}")
        for prefix in ("assets/ozone/", "assets/pkg/", "assets/sounds/", "info/"):
            if not any(name.startswith(prefix) for name in names):
                fail(f"asset bundle has no {prefix}")
        info_name = "info/johnny_castaway_libretro.info"
        if info_name not in names:
            fail(f"asset bundle is missing {info_name}")
        info = bundle.read(info_name)
        if hashlib.sha256(info).hexdigest() != info_hash_match.group(1):
            fail("bundled core metadata hash does not match BUILD-PROVENANCE.txt")
        info_text = info.decode("utf-8")
        if f'display_version = "{version_match.group(1)}"' not in info_text:
            fail("bundled core metadata version does not match BUILD-PROVENANCE.txt")

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
