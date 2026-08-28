#!/usr/bin/env python3
"""Static regression checks for the release ZIP count contract."""

from __future__ import annotations

import pathlib
import re
import shlex
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
ASSEMBLER = ROOT / "scripts" / "assemble-release.sh"


def array_entries(source: str, name: str) -> list[str]:
    match = re.search(
        rf"^{re.escape(name)}=\((?P<body>[^)]*)\)", source, re.MULTILINE
    )
    if match is None:
        raise AssertionError(f"missing {name} array")
    return shlex.split(match.group("body"), comments=True, posix=True)


class ReleaseZipContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = ASSEMBLER.read_text(encoding="utf-8")

    def test_contract_is_ten_artifacts_plus_seven_frontends(self) -> None:
        artifacts = array_entries(self.source, "ci_artifacts") + array_entries(
            self.source, "console_artifacts"
        )
        frontends = array_entries(self.source, "frontend_targets")
        names = [f"{name}.zip" for name in artifacts]
        names.extend(f"johnny-castaway-{name}-frontend.zip" for name in frontends)

        self.assertEqual(10, len(artifacts))
        self.assertEqual(7, len(frontends))
        self.assertEqual(17, len(names))
        self.assertEqual(len(names), len(set(names)))

    def test_assembler_checks_explicit_and_derived_counts(self) -> None:
        self.assertIn("expected_release_zip_count=17", self.source)
        self.assertIn(
            "((${#expected_release_zips_sorted[@]} == expected_release_zip_count))",
            self.source,
        )
        self.assertIn(
            "((${#actual_release_zips[@]} == ${#expected_release_zips_sorted[@]}))",
            self.source,
        )
        self.assertIn(
            'fail "release ZIP count is ${#actual_release_zips[@]}, expected '
            '${#expected_release_zips_sorted[@]}"',
            self.source,
        )
        self.assertNotIn("${#actual_release_zips[@]} == 16", self.source)


if __name__ == "__main__":
    unittest.main(verbosity=2)
