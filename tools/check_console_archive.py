#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate a static libretro console archive with host GNU binutils."""

from __future__ import annotations

import argparse
import collections
import hashlib
import pathlib
import re
import subprocess

from check_core_exports import REQUIRED_EXPORTS


def check_deterministic_archive_metadata(archive: pathlib.Path) -> int:
    """Require GNU/SVR4 ar members to carry deterministic metadata."""
    data = archive.read_bytes()
    if not data.startswith(b"!<arch>\n"):
        raise SystemExit("archive check failed: invalid ar global header")

    offset = 8
    header_count = 0
    while offset < len(data):
        if len(data) - offset < 60:
            raise SystemExit("archive check failed: truncated ar member header")
        header = data[offset:offset + 60]
        if header[58:60] != b"`\n":
            raise SystemExit("archive check failed: invalid ar member header")

        name = header[:16].decode("ascii", errors="replace").strip()
        fields = {
            "timestamp": header[16:28],
            "uid": header[28:34],
            "gid": header[34:40],
        }
        for label, raw_value in fields.items():
            try:
                value = int(raw_value.strip() or b"0", 10)
            except ValueError as error:
                raise SystemExit(
                    f"archive check failed: invalid {label} for member {name!r}"
                ) from error
            if value != 0:
                raise SystemExit(
                    "archive check failed: nondeterministic "
                    f"{label}={value} for member {name!r}"
                )

        try:
            member_size = int(header[48:58].strip(), 10)
        except ValueError as error:
            raise SystemExit(
                f"archive check failed: invalid size for member {name!r}"
            ) from error
        offset += 60 + member_size
        if offset % 2:
            offset += 1
        header_count += 1

    if offset != len(data):
        raise SystemExit("archive check failed: invalid ar member padding")
    if header_count == 0:
        raise SystemExit("archive check failed: archive has no member headers")
    return header_count


def run(tool: str, arguments: list[str]) -> str:
    result = subprocess.run(
        [tool, *arguments],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if result.returncode != 0:
        raise SystemExit(
            f"archive check failed: {tool} returned {result.returncode}:\n"
            f"{result.stderr.strip()}"
        )
    return result.stdout


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--machine", required=True)
    parser.add_argument("--ar", default="ar")
    parser.add_argument("--nm", default="nm")
    parser.add_argument("--readelf", default="readelf")
    parser.add_argument("archive", type=pathlib.Path)
    args = parser.parse_args()

    if not args.archive.is_file() or args.archive.stat().st_size == 0:
        raise SystemExit(f"archive check failed: missing/empty archive: {args.archive}")

    header_count = check_deterministic_archive_metadata(args.archive)
    members = [line for line in run(args.ar, ["t", str(args.archive)]).splitlines()
               if line]
    if not members:
        raise SystemExit("archive check failed: archive has no members")
    if len(members) != len(set(members)):
        raise SystemExit("archive check failed: duplicate member names")
    if any(not member.endswith(".o") for member in members):
        raise SystemExit("archive check failed: non-object archive member")

    header_output = run(args.readelf, ["-h", str(args.archive)])
    machines = re.findall(r"^\s*Machine:\s*(.+?)\s*$", header_output, re.MULTILINE)
    if len(machines) != len(members):
        raise SystemExit(
            f"archive check failed: {len(members)} members but "
            f"{len(machines)} ELF headers"
        )
    wrong_machines = sorted({machine for machine in machines
                             if machine != args.machine})
    if wrong_machines:
        raise SystemExit(
            "archive check failed: unexpected machines: "
            + ", ".join(wrong_machines)
        )

    defined_output = run(args.nm, ["-g", "--defined-only", str(args.archive)])
    definitions = re.findall(
        r"\b[TDB]\s+(retro_[a-z0-9_]+)\s*$", defined_output, re.MULTILINE
    )
    counts = collections.Counter(definitions)
    found = set(counts)
    missing = REQUIRED_EXPORTS - found
    unexpected = found - REQUIRED_EXPORTS
    duplicates = sorted(symbol for symbol, count in counts.items() if count != 1)
    if missing or unexpected or duplicates:
        raise SystemExit(
            "archive check failed: "
            f"missing={sorted(missing)} unexpected={sorted(unexpected)} "
            f"duplicate={duplicates}"
        )

    undefined_output = run(args.nm, ["-u", str(args.archive)])
    undefined_retro = sorted(set(re.findall(
        r"\bU\s+(retro_[a-z0-9_]+)\s*$", undefined_output, re.MULTILINE
    )))
    if undefined_retro:
        raise SystemExit(
            f"archive check failed: undefined libretro symbols: {undefined_retro}"
        )

    digest = hashlib.sha256(args.archive.read_bytes()).hexdigest()
    print(
        f"console archive check passed: {args.archive} "
        f"members={len(members)} ar_headers={header_count} "
        f"machine={args.machine} deterministic_metadata=yes "
        f"retro_symbols={len(found)} sha256={digest}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
