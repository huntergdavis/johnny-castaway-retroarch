#!/usr/bin/env python3
"""Adversarial tests for deterministic ZIP creation and source preflight."""

from __future__ import annotations

import pathlib
import stat
import subprocess
import sys
import tempfile
import unittest
import warnings
import zipfile
from unittest import mock

import make_deterministic_zip as maker
from check_installable_frontend import expected_zip_time


ODD_EPOCH = 1_770_000_001
SCRIPT = pathlib.Path(__file__).with_name("make_deterministic_zip.py")


class DeterministicZipTests(unittest.TestCase):
    def run_maker(
        self,
        source: pathlib.Path,
        output: pathlib.Path,
        source_zip: bool = False,
        success: bool = True,
    ) -> subprocess.CompletedProcess[str]:
        result = subprocess.run(
            [
                sys.executable,
                str(SCRIPT),
                "--source-zip" if source_zip else "--directory",
                str(source),
                "--output",
                str(output),
                "--epoch",
                str(ODD_EPOCH),
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        if success:
            self.assertEqual(result.returncode, 0, result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("deterministic ZIP failed:", result.stderr)
        return result

    def test_directory_output_is_ordered_reproducible_and_normalized(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            source = root / "source"
            source.mkdir()
            (source / "z-last.txt").write_text("z\n", encoding="utf-8")
            (source / "A-first.txt").write_text("a\n", encoding="utf-8")
            (source / "z-last.txt").chmod(0o777)
            (source / "A-first.txt").chmod(0o600)
            first, second = root / "first.zip", root / "second.zip"
            self.run_maker(source, first)
            self.run_maker(source, second)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            with zipfile.ZipFile(first) as archive:
                infos = archive.infolist()
                self.assertEqual(
                    [info.filename for info in infos], ["A-first.txt", "z-last.txt"]
                )
                self.assertEqual(
                    expected_zip_time(ODD_EPOCH), maker.zip_time(ODD_EPOCH)
                )
                for info in infos:
                    self.assertEqual(info.date_time, maker.zip_time(ODD_EPOCH))
                    self.assertEqual(info.create_system, 3)
                    self.assertEqual(info.external_attr >> 16, maker.OUTPUT_MODE)

    def test_source_zip_is_sorted_and_metadata_is_normalized(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            source, output = root / "source.zip", root / "output.zip"
            with zipfile.ZipFile(source, "w") as archive:
                directory = zipfile.ZipInfo("folder/")
                directory.external_attr = (stat.S_IFDIR | 0o755) << 16
                archive.writestr(directory, b"")
                for name, payload, mode in (
                    ("z.txt", b"z", 0o777),
                    ("folder/a.txt", b"a", 0o600),
                ):
                    info = zipfile.ZipInfo(name, (2020, 1, 2, 3, 4, 4))
                    info.create_system = 3
                    info.external_attr = (stat.S_IFREG | mode) << 16
                    archive.writestr(info, payload)
            self.run_maker(source, output, source_zip=True)
            with zipfile.ZipFile(output) as archive:
                self.assertEqual(archive.namelist(), ["folder/a.txt", "z.txt"])
                self.assertEqual(archive.read("folder/a.txt"), b"a")
                for info in archive.infolist():
                    self.assertEqual(info.date_time, maker.zip_time(ODD_EPOCH))
                    self.assertEqual(info.external_attr >> 16, maker.OUTPUT_MODE)

    def test_unsafe_source_zip_names_are_rejected(self) -> None:
        unsafe = (
            "/absolute",
            "../escape",
            "dir/../escape",
            "back\\slash",
            "C:/drive-qualified",
            "name:stream",
            "double//separator",
            "./dot-segment",
        )
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            for index, name in enumerate(unsafe):
                with self.subTest(name=name):
                    source = root / f"unsafe-{index}.zip"
                    with zipfile.ZipFile(source, "w") as archive:
                        archive.writestr(name, b"payload")
                    result = self.run_maker(
                        source,
                        root / f"output-{index}.zip",
                        source_zip=True,
                        success=False,
                    )
                    self.assertIn("unsafe member name", result.stderr)

    def test_duplicate_and_case_fold_collisions_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            fixtures = (
                (("same", "same"), "duplicate"),
                (("README", "readme"), "case-fold collision"),
            )
            for index, (names, message) in enumerate(fixtures):
                source = root / f"collision-{index}.zip"
                with warnings.catch_warnings():
                    warnings.simplefilter("ignore", UserWarning)
                    with zipfile.ZipFile(source, "w") as archive:
                        for name in names:
                            archive.writestr(name, name.encode())
                result = self.run_maker(
                    source, root / f"collision-{index}-out.zip", True, False
                )
                self.assertIn(message, result.stderr)

            directory = root / "directory"
            directory.mkdir()
            (directory / "NOTICE").write_text("upper", encoding="utf-8")
            (directory / "notice").write_text("lower", encoding="utf-8")
            result = self.run_maker(directory, root / "directory.zip", success=False)
            self.assertIn("case-fold collision", result.stderr)

    def test_source_zip_symlink_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            source = root / "symlink.zip"
            with zipfile.ZipFile(source, "w") as archive:
                info = zipfile.ZipInfo("link")
                info.create_system = 3
                info.external_attr = (stat.S_IFLNK | 0o777) << 16
                archive.writestr(info, b"target")
            result = self.run_maker(source, root / "output.zip", True, False)
            self.assertIn("symlinks are not accepted", result.stderr)

    def test_names_are_validated_before_testzip(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            source = pathlib.Path(temporary) / "unsafe.zip"
            with zipfile.ZipFile(source, "w") as archive:
                archive.writestr("C:/unsafe", b"payload")
            with mock.patch.object(
                zipfile.ZipFile, "testzip", side_effect=AssertionError("too early")
            ):
                with self.assertRaisesRegex(SystemExit, "unsafe member name"):
                    maker.archive_members(source)

    def test_member_and_aggregate_limits_fail_before_reads(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            source = pathlib.Path(temporary) / "source"
            source.mkdir()
            (source / "one").write_bytes(b"1")
            (source / "two").write_bytes(b"2")
            with mock.patch.object(maker, "MAX_MEMBER_COUNT", 1):
                with self.assertRaisesRegex(SystemExit, "member limit"):
                    maker.directory_members(source)
            with mock.patch.object(maker, "MAX_TOTAL_SIZE", 1):
                with self.assertRaisesRegex(SystemExit, "aggregate limit"):
                    maker.directory_members(source)
            with mock.patch.object(maker, "MAX_MEMBER_SIZE", 0):
                with self.assertRaisesRegex(SystemExit, "member exceeds"):
                    maker.directory_members(source)


def main() -> int:
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(DeterministicZipTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
