#!/usr/bin/env python3
"""Adversarial tests for exact Web distribution provenance checks."""

from __future__ import annotations

import hashlib
import pathlib
import subprocess
import sys
import tempfile
import unittest
import zipfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
CHECKER = ROOT / "tools/check_web_dist.py"


class WebDistributionValidatorTests(unittest.TestCase):
    def make_distribution(self, root: pathlib.Path) -> None:
        required = (
            "style.css",
            "jc-web-player.js",
            "johnny_castaway_libretro.js",
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
        for relative in required:
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(f"fixture {relative}\n", encoding="utf-8")

        (root / "index.html").write_text(
            '<link href="style.css"><script src="jc-web-player.js"></script>\n',
            encoding="utf-8",
        )
        browserfs = b"browserfs fixture\n"
        javascript = (root / "johnny_castaway_libretro.js").read_bytes()
        wasm = b"\0asmfixture"
        (root / "browserfs.min.js").write_bytes(browserfs)
        (root / "johnny_castaway_libretro.wasm").write_bytes(wasm)
        retroarch_license = (root / "licenses/RetroArch-GPL-3.0").read_bytes()
        assets_license = (
            root / "licenses/retroarch-assets-CC-BY-4.0"
        ).read_bytes()

        info = b'display_version = "0.1.2"\n'
        bundle_path = root / "assets/frontend/bundle.zip"
        bundle_path.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(bundle_path, "w") as bundle:
            bundle.writestr("assets/ozone/fixture", b"x")
            bundle.writestr("assets/pkg/fixture", b"x")
            bundle.writestr("assets/sounds/fixture", b"x")
            bundle.writestr("info/johnny_castaway_libretro.info", info)
        asset_bundle = bundle_path.read_bytes()

        (root / "BUILD-PROVENANCE.txt").write_text(
            "\n".join(
                (
                    "Johnny Castaway RetroArch Web Player build provenance",
                    f"Johnny Castaway commit: {'a' * 40} (clean)",
                    "Frontend version: 0.1.2",
                    "Tree state: clean",
                    f"Core metadata SHA-256: {hashlib.sha256(info).hexdigest()}",
                    f"BrowserFS SHA-256: {hashlib.sha256(browserfs).hexdigest()}",
                    f"JavaScript SHA-256: {hashlib.sha256(javascript).hexdigest()}",
                    f"WebAssembly SHA-256: {hashlib.sha256(wasm).hexdigest()}",
                    "Asset bundle SHA-256: "
                    f"{hashlib.sha256(asset_bundle).hexdigest()}",
                    "RetroArch license SHA-256: "
                    f"{hashlib.sha256(retroarch_license).hexdigest()}",
                    "retroarch-assets license SHA-256: "
                    f"{hashlib.sha256(assets_license).hexdigest()}",
                    "",
                )
            ),
            encoding="utf-8",
        )

    def run_checker(self, root: pathlib.Path) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(CHECKER), str(root)],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )

    def test_valid_self_consistent_distribution_passes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            self.make_distribution(root)
            result = self.run_checker(root)
            self.assertEqual(result.returncode, 0, result.stdout)

    def test_missing_exact_commit_field_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            self.make_distribution(root)
            provenance = root / "BUILD-PROVENANCE.txt"
            provenance.write_text(
                provenance.read_text(encoding="utf-8").replace(
                    f"Johnny Castaway commit: {'a' * 40} (clean)\n", ""
                ),
                encoding="utf-8",
            )
            result = self.run_checker(root)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("exact Johnny commit/version/tree/info fields", result.stdout)

    def test_tampered_wasm_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            self.make_distribution(root)
            (root / "johnny_castaway_libretro.wasm").write_bytes(b"\0asmtampered")
            result = self.run_checker(root)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("WebAssembly hash", result.stdout)

    def test_tampered_bundled_metadata_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            self.make_distribution(root)
            bundle_path = root / "assets/frontend/bundle.zip"
            with zipfile.ZipFile(bundle_path) as bundle:
                members = {
                    info.filename: bundle.read(info)
                    for info in bundle.infolist()
                    if info.filename != "info/johnny_castaway_libretro.info"
                }
            members["info/johnny_castaway_libretro.info"] = (
                b'display_version = "9.9.9"\n'
            )
            with zipfile.ZipFile(bundle_path, "w") as bundle:
                for name, payload in members.items():
                    bundle.writestr(name, payload)
            provenance = root / "BUILD-PROVENANCE.txt"
            provenance_text = provenance.read_text(encoding="utf-8")
            provenance.write_text(
                provenance_text.replace(
                    next(
                        line
                        for line in provenance_text.splitlines()
                        if line.startswith("Asset bundle SHA-256: ")
                    ),
                    "Asset bundle SHA-256: "
                    f"{hashlib.sha256(bundle_path.read_bytes()).hexdigest()}",
                ),
                encoding="utf-8",
            )
            result = self.run_checker(root)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("bundled core metadata hash", result.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
