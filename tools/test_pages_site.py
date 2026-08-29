#!/usr/bin/env python3
"""Focused tests for the static Pages source and pinned-release assembler."""

from __future__ import annotations

import hashlib
import io
import json
from pathlib import Path
import shutil
import stat
import tempfile
import unittest
import zipfile

from build_pages_site import (
    DEFAULT_RELEASE_MANIFEST,
    DEFAULT_SITE_SOURCE,
    PagesFailure,
    REQUIRED_WEB_FILES,
    WEB_ARCHIVE_ROOT,
    assemble_pages_site,
    load_release_manifest,
    sha256_file,
    validate_site_source,
)


EXPECTED_COMMIT = "cd62e390882ace098e95eb72ba32cd021419f715"
EXPECTED_WEB_SHA256 = "9866c4b0fb028eba13b213c43275f761ae1f37a40e8dfa4e101161a004c057e7"


def make_nested_bundle(*, forbidden_name: str | None = None) -> bytes:
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("assets/ozone/README.md", "synthetic frontend fixture\n")
        if forbidden_name:
            archive.writestr(forbidden_name, b"private")
    return buffer.getvalue()


def make_fixture_archive(
    path: Path,
    *,
    forbidden_outer: str | None = None,
    forbidden_nested: str | None = None,
    unsafe_outer: str | None = None,
    symlink_outer: str | None = None,
) -> None:
    bundle = make_nested_bundle(forbidden_name=forbidden_nested)
    files = {
        "CREDITS.md": b"synthetic credits\n",
        "WEB_PLAYER_NOTICE.md": b"synthetic notice\n",
        "assets/frontend/bundle.zip": bundle,
        "browserfs.min.js": b"synthetic BrowserFS\n",
        "docs/PROVENANCE.md": b"synthetic provenance\n",
        "docs/THIRD_PARTY_NOTICES.md": b"synthetic notices\n",
        "index.html": b"<!doctype html><title>fixture</title>\n",
        "jc-web-player.js": b"// synthetic launcher\n",
        "johnny_castaway_libretro.js": b"// synthetic frontend\n",
        "johnny_castaway_libretro.wasm": b"\0asmfixture",
        "licenses/BigSoundBank-0266-CC0.md": b"synthetic CC0 notice\n",
        "licenses/BrowserFS-license.md": b"synthetic MIT notice\n",
        "licenses/RetroArch-GPL-3.0": b"synthetic RetroArch GPL\n",
        "licenses/johnny-castaway-retroarch-GPL-3.0": b"synthetic project GPL\n",
        "licenses/retroarch-assets-CC-BY-4.0": b"synthetic assets license\n",
        "style.css": b"body { color: white; }\n",
    }
    provenance_hashes = {
        "BrowserFS SHA-256": hashlib.sha256(files["browserfs.min.js"]).hexdigest(),
        "JavaScript SHA-256": hashlib.sha256(files["johnny_castaway_libretro.js"]).hexdigest(),
        "WebAssembly SHA-256": hashlib.sha256(files["johnny_castaway_libretro.wasm"]).hexdigest(),
        "Asset bundle SHA-256": hashlib.sha256(files["assets/frontend/bundle.zip"]).hexdigest(),
        "RetroArch license SHA-256": hashlib.sha256(files["licenses/RetroArch-GPL-3.0"]).hexdigest(),
        "retroarch-assets license SHA-256": hashlib.sha256(files["licenses/retroarch-assets-CC-BY-4.0"]).hexdigest(),
    }
    provenance_lines = [
        "Johnny Castaway RetroArch Web Player build provenance",
        f"Johnny Castaway commit: {EXPECTED_COMMIT} (clean)",
        "Frontend version: 0.1.4",
        "Tree state: clean",
    ] + [f"{label}: {digest}" for label, digest in provenance_hashes.items()]
    files["BUILD-PROVENANCE.txt"] = ("\n".join(provenance_lines) + "\n").encode()
    assert set(files) == REQUIRED_WEB_FILES

    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name in sorted(files):
            archive.writestr(f"{WEB_ARCHIVE_ROOT}/{name}", files[name])
        if forbidden_outer:
            archive.writestr(f"{WEB_ARCHIVE_ROOT}/{forbidden_outer}", b"private")
        if unsafe_outer:
            archive.writestr(unsafe_outer, b"unsafe")
        if symlink_outer:
            info = zipfile.ZipInfo(f"{WEB_ARCHIVE_ROOT}/{symlink_outer}")
            info.create_system = 3
            info.external_attr = (stat.S_IFLNK | 0o777) << 16
            archive.writestr(info, "index.html")


def bind_fixture_archive(site: Path, archive: Path) -> Path:
    manifest_path = site / "release-v0.1.4.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    web_asset = next(
        asset
        for asset in manifest["release"]["assets"]
        if asset.get("role") == "pages_web_player"
    )
    web_asset["size"] = archive.stat().st_size
    web_asset["sha256"] = sha256_file(archive)
    manifest_path.write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n"
    )
    return manifest_path


def tree_hashes(root: Path) -> dict[str, str]:
    return {
        path.relative_to(root).as_posix(): sha256_file(path)
        for path in root.rglob("*")
        if path.is_file()
    }


class PagesSourceTests(unittest.TestCase):
    def test_tracked_site_source_validates(self) -> None:
        manifest = validate_site_source()
        self.assertEqual(manifest["release"]["source_commit"], EXPECTED_COMMIT)
        self.assertEqual(len(manifest["release"]["assets"]), 18)
        self.assertEqual(len(manifest["screenshots"]), 3)

    def test_public_web_release_pin_is_exact(self) -> None:
        manifest = load_release_manifest(DEFAULT_RELEASE_MANIFEST)
        web_asset = next(
            asset
            for asset in manifest["release"]["assets"]
            if asset.get("role") == "pages_web_player"
        )
        self.assertEqual(web_asset["name"], "johnny-castaway-retroarch-web.zip")
        self.assertEqual(web_asset["sha256"], EXPECTED_WEB_SHA256)
        self.assertEqual(web_asset["size"], 11220750)

    def test_remote_script_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            site = Path(temporary) / "site"
            shutil.copytree(DEFAULT_SITE_SOURCE, site)
            index = site / "index.html"
            index.write_text(
                index.read_text(encoding="utf-8").replace(
                    "</body>", '<script src="https://example.invalid/app.js"></script></body>'
                ),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(PagesFailure, "scripts, embeds, or forms"):
                validate_site_source(site, site / "release-v0.1.4.json")

    def test_empty_image_alt_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            site = Path(temporary) / "site"
            shutil.copytree(DEFAULT_SITE_SOURCE, site)
            index = site / "index.html"
            index.write_text(
                index.read_text(encoding="utf-8").replace(
                    'alt="Johnny stands beneath', 'alt="" data-old-alt="Johnny stands beneath', 1
                ),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(PagesFailure, "non-empty alt text"):
                validate_site_source(site, site / "release-v0.1.4.json")


class PagesAssemblerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.site = self.root / "site"
        shutil.copytree(DEFAULT_SITE_SOURCE, self.site)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def _archive(self, **kwargs: str) -> tuple[Path, Path]:
        archive = self.root / "web.zip"
        make_fixture_archive(archive, **kwargs)
        return archive, bind_fixture_archive(self.site, archive)

    def test_assembly_is_complete_private_free_and_deterministic(self) -> None:
        archive, manifest = self._archive()
        first = self.root / "first"
        second = self.root / "second"
        site_count, web_count = assemble_pages_site(
            self.site, manifest, archive, first
        )
        assemble_pages_site(self.site, manifest, archive, second)
        self.assertEqual(site_count, 7)
        self.assertEqual(web_count, len(REQUIRED_WEB_FILES))
        self.assertTrue((first / ".nojekyll").is_file())
        self.assertTrue((first / "play/index.html").is_file())
        self.assertTrue((first / "play/johnny_castaway_libretro.wasm").is_file())
        self.assertIn(
            "Original Sierra/Dynamix data included: no",
            (first / "PAGES-BUILD-PROVENANCE.txt").read_text(encoding="utf-8"),
        )
        self.assertEqual(tree_hashes(first), tree_hashes(second))

    def test_wrong_archive_digest_is_rejected(self) -> None:
        archive, manifest = self._archive()
        archive.write_bytes(archive.read_bytes() + b"tampered")
        with self.assertRaisesRegex(PagesFailure, "size mismatch"):
            assemble_pages_site(self.site, manifest, archive, self.root / "output")

    def test_outer_original_data_is_rejected(self) -> None:
        archive, manifest = self._archive(forbidden_outer="RESOURCE.MAP")
        with self.assertRaisesRegex(PagesFailure, "private content"):
            assemble_pages_site(self.site, manifest, archive, self.root / "output")

    def test_nested_user_wav_is_rejected(self) -> None:
        archive, manifest = self._archive(forbidden_nested="local-content/sound0.wav")
        with self.assertRaisesRegex(PagesFailure, "private content"):
            assemble_pages_site(self.site, manifest, archive, self.root / "output")

    def test_unsafe_archive_path_is_rejected(self) -> None:
        archive, manifest = self._archive(unsafe_outer="../outside")
        with self.assertRaisesRegex(PagesFailure, "unsafe Web release ZIP member"):
            assemble_pages_site(self.site, manifest, archive, self.root / "output")

    def test_symlink_archive_member_is_rejected(self) -> None:
        archive, manifest = self._archive(symlink_outer="linked-index")
        with self.assertRaisesRegex(PagesFailure, "symlink member"):
            assemble_pages_site(self.site, manifest, archive, self.root / "output")

    def test_existing_output_is_not_replaced(self) -> None:
        archive, manifest = self._archive()
        output = self.root / "output"
        output.mkdir()
        marker = output / "keep.txt"
        marker.write_text("keep", encoding="utf-8")
        with self.assertRaisesRegex(PagesFailure, "refusing to replace"):
            assemble_pages_site(self.site, manifest, archive, output)
        self.assertEqual(marker.read_text(encoding="utf-8"), "keep")


if __name__ == "__main__":
    unittest.main()
