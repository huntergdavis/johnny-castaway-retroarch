#!/usr/bin/env python3
"""Unit tests for exact Web chapter matrix accounting and resume behavior."""

from __future__ import annotations

import json
import pathlib
import subprocess
import tempfile
import unittest

from web_chapter_matrix import (
    CATALOG_PATH,
    MatrixFailure,
    empty_record,
    parse_chapter_catalog,
    paths_overlap,
    reusable_record,
    run_scene,
    select_catalog,
    sha256_file,
    summary_document,
)


class CatalogTests(unittest.TestCase):
    def test_repository_catalog_has_exact_expected_order_and_count(self) -> None:
        slugs = parse_chapter_catalog(CATALOG_PATH.read_text(encoding="utf-8"))
        self.assertEqual(len(slugs), 63)
        self.assertEqual(slugs[:3], ["fishing1", "fishing2", "fishing3"])
        self.assertEqual(slugs[8], "johnny1")
        self.assertEqual(slugs[-3:], ["building5", "building6", "building7"])

    def test_parser_ignores_macro_declaration_but_rejects_duplicates(self) -> None:
        source = """
        #define CHAPTER(slug, title) slug
        static const jc_chapter_t chapters[] = {
            CHAPTER("one", "One"),
            CHAPTER("one", "Again")
        };
        """
        with self.assertRaisesRegex(MatrixFailure, "duplicate slugs"):
            parse_chapter_catalog(source, expected_count=2)

    def test_parser_rejects_unparseable_catalog_invocation(self) -> None:
        source = """
        static const jc_chapter_t chapters[] = {
            CHAPTER(dynamic_slug, "One")
        };
        """
        with self.assertRaisesRegex(MatrixFailure, "unparseable"):
            parse_chapter_catalog(source, expected_count=1)


class SelectionTests(unittest.TestCase):
    def setUp(self) -> None:
        self.slugs = [f"scene{index}" for index in range(63)]

    def test_three_balanced_shards_cover_catalog_once_in_order(self) -> None:
        shards = [
            select_catalog(self.slugs, shard_index=index, shard_count=3)
            for index in range(3)
        ]
        self.assertEqual([shard["count"] for shard in shards], [21, 21, 21])
        combined = [slug for shard in shards for slug in shard["slugs"]]
        self.assertEqual(combined, self.slugs)
        self.assertEqual(len(set(combined)), 63)

    def test_range_is_exact_and_out_of_bounds_is_rejected(self) -> None:
        selection = select_catalog(self.slugs, start=20, count=7)
        self.assertEqual(selection["start"], 20)
        self.assertEqual(selection["end_exclusive"], 27)
        self.assertEqual(selection["slugs"], self.slugs[20:27])
        with self.assertRaisesRegex(MatrixFailure, "beyond catalog"):
            select_catalog(self.slugs, start=60, count=4)

    def test_partial_or_mixed_shard_arguments_are_rejected(self) -> None:
        with self.assertRaisesRegex(MatrixFailure, "supplied together"):
            select_catalog(self.slugs, shard_index=0)
        with self.assertRaisesRegex(MatrixFailure, "cannot be combined"):
            select_catalog(
                self.slugs, start=0, shard_index=0, shard_count=3
            )

    def test_path_overlap_detects_ancestor_in_either_direction(self) -> None:
        root = pathlib.Path("/fixture")
        child = root / "child"
        self.assertTrue(paths_overlap(root, child))
        self.assertTrue(paths_overlap(child, root))
        self.assertFalse(paths_overlap(root / "left", root / "right"))


class ResultTests(unittest.TestCase):
    @staticmethod
    def write_passing_result(path: pathlib.Path, slug: str) -> None:
        path.write_text(
            json.dumps(
                {
                    "chapter": slug,
                    "passed": True,
                    "content": f"user-owned local data (fixed {slug} chapter)",
                }
            )
            + "\n",
            encoding="utf-8",
        )

    def test_run_scene_requires_fresh_matching_result_and_writes_marker(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            scene_dir = root / "scene"
            invoked_command: list[str] = []

            def passing_runner(command: list[str], **_kwargs: object) -> subprocess.CompletedProcess[object]:
                invoked_command.extend(command)
                artifacts = pathlib.Path(command[command.index("--artifacts") + 1])
                artifacts.mkdir(parents=True, exist_ok=True)
                self.write_passing_result(artifacts / "result.json", "fishing1")
                return subprocess.CompletedProcess(command, 0)

            record = run_scene(
                dist=root,
                content_dir=root,
                scene_dir=scene_dir,
                index=0,
                slug="fishing1",
                fingerprint="f" * 64,
                timeout=1.0,
                no_xvfb=True,
                smoke_runner=root / "runner.py",
                command_runner=passing_runner,
            )
            self.assertEqual(record["status"], "passed")
            self.assertIn("--scene-visual-only", invoked_command)
            marker = json.loads(
                (scene_dir / "matrix-result.json").read_text(encoding="utf-8")
            )
            self.assertEqual(marker["smoke_result_sha256"], sha256_file(scene_dir / "result.json"))
            resumed = reusable_record(scene_dir, 0, "fishing1", "f" * 64)
            self.assertIsNotNone(resumed)
            assert resumed is not None
            self.assertTrue(resumed["resumed"])

    def test_stale_passing_result_cannot_mask_nonzero_subprocess(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            scene_dir = root / "scene"
            scene_dir.mkdir()
            self.write_passing_result(scene_dir / "result.json", "fishing1")

            def failing_runner(command: list[str], **_kwargs: object) -> subprocess.CompletedProcess[object]:
                return subprocess.CompletedProcess(command, 1)

            record = run_scene(
                dist=root,
                content_dir=root,
                scene_dir=scene_dir,
                index=0,
                slug="fishing1",
                fingerprint="f" * 64,
                timeout=1.0,
                no_xvfb=False,
                smoke_runner=root / "runner.py",
                command_runner=failing_runner,
            )
            self.assertEqual(record["status"], "failed")
            self.assertEqual(record["error"], "smoke-runner-nonzero-exit")

    def test_summary_fails_closed_for_pending_or_failed_rows(self) -> None:
        selection = select_catalog(["one", "two"], start=0, count=2)
        records = [empty_record(0, "one"), empty_record(1, "two")]
        summary = summary_document(
            selection=selection,
            records=records,
            catalog_sha256="a" * 64,
            fingerprint="b" * 64,
        )
        self.assertFalse(summary["passed"])
        self.assertTrue(summary["accounted"])
        records[0].update(status="passed", smoke_passed=True, error="")
        records[1].update(status="failed", error="failed")
        summary = summary_document(
            selection=selection,
            records=records,
            catalog_sha256="a" * 64,
            fingerprint="b" * 64,
        )
        self.assertFalse(summary["passed"])
        self.assertEqual(summary["counts"]["failed"], 1)


if __name__ == "__main__":
    unittest.main()
