#!/usr/bin/env python3
"""Create a sorted, fixed-metadata ZIP from a directory or existing ZIP."""

from __future__ import annotations

import argparse
import datetime
import os
import pathlib
import stat
import zipfile


MAX_MEMBER_COUNT = 4096
MAX_MEMBER_SIZE = 256 * 1024 * 1024
MAX_TOTAL_SIZE = 512 * 1024 * 1024
OUTPUT_MODE = stat.S_IFREG | 0o644


def fail(message: str) -> None:
    raise SystemExit(f"deterministic ZIP failed: {message}")


def zip_time(epoch: int) -> tuple[int, int, int, int, int, int]:
    value = datetime.datetime.fromtimestamp(epoch, datetime.timezone.utc)
    if value.year < 1980:
        value = datetime.datetime(1980, 1, 1, tzinfo=datetime.timezone.utc)
    if value.year > 2107:
        fail("SOURCE_DATE_EPOCH exceeds the ZIP timestamp range")
    second = value.second - value.second % 2
    return (value.year, value.month, value.day, value.hour, value.minute, second)


def safe_name(name: str) -> str:
    if (
        not name
        or "\0" in name
        or "\\" in name
        or ":" in name
        or any(part in ("", ".", "..") for part in name.split("/"))
    ):
        fail(f"unsafe member name: {name!r}")
    path = pathlib.PurePosixPath(name)
    if path.is_absolute():
        fail(f"unsafe member name: {name!r}")
    return path.as_posix()


def check_member_limits(count: int, size: int, total: int, name: str) -> int:
    if count > MAX_MEMBER_COUNT:
        fail(f"input exceeds the {MAX_MEMBER_COUNT}-member limit")
    if size < 0 or size > MAX_MEMBER_SIZE:
        fail(f"member exceeds the {MAX_MEMBER_SIZE}-byte limit: {name}")
    total += size
    if total > MAX_TOTAL_SIZE:
        fail(f"input exceeds the {MAX_TOTAL_SIZE}-byte aggregate limit")
    return total


def remember_name(seen: dict[str, str], name: str, label: str) -> None:
    folded = name.casefold()
    previous = seen.get(folded)
    if previous is not None:
        kind = "duplicate" if previous == name else "case-fold collision"
        fail(f"{label} has a {kind}: {previous!r} and {name!r}")
    seen[folded] = name


def write_member(
    output: zipfile.ZipFile,
    name: str,
    payload: bytes,
    timestamp: tuple[int, int, int, int, int, int],
) -> None:
    info = zipfile.ZipInfo(safe_name(name), timestamp)
    info.create_system = 3
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = OUTPUT_MODE << 16
    output.writestr(info, payload, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)


def directory_members(root: pathlib.Path) -> list[tuple[str, bytes]]:
    members: list[tuple[str, bytes]] = []
    seen: dict[str, str] = {}
    total = 0
    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            fail(f"symlinks are not accepted: {path}")
        if not path.is_file():
            continue
        relative = safe_name(path.relative_to(root).as_posix())
        remember_name(seen, relative, "input directory")
        size = path.stat().st_size
        total = check_member_limits(len(members) + 1, size, total, relative)
        payload = path.read_bytes()
        if len(payload) != size:
            fail(f"member changed while reading: {relative}")
        members.append((relative, payload))
    if not members:
        fail(f"input directory contains no files: {root}")
    return members


def archive_members(source: pathlib.Path) -> list[tuple[str, bytes]]:
    members: list[tuple[str, bytes]] = []
    with zipfile.ZipFile(source) as archive:
        infos = archive.infolist()
        if len(infos) > MAX_MEMBER_COUNT:
            fail(f"source ZIP exceeds the {MAX_MEMBER_COUNT}-member limit")
        selected: list[tuple[str, zipfile.ZipInfo]] = []
        seen: dict[str, str] = {}
        total = 0
        for info in infos:
            raw_name = info.filename[:-1] if info.is_dir() else info.filename
            name = safe_name(raw_name)
            remember_name(seen, name, "source ZIP")
            if info.flag_bits & 0x1:
                fail(f"encrypted source member is not supported: {name}")
            unix_mode = info.external_attr >> 16
            file_type = stat.S_IFMT(unix_mode)
            if file_type == stat.S_IFLNK:
                fail(f"source ZIP symlinks are not accepted: {name}")
            if info.is_dir():
                if info.file_size:
                    fail(f"source ZIP directory has payload data: {name}")
                continue
            if file_type not in (0, stat.S_IFREG):
                fail(f"source ZIP has a non-regular member: {name}")
            total = check_member_limits(
                len(selected) + 1, info.file_size, total, name
            )
            selected.append((name, info))
        bad = archive.testzip()
        if bad:
            fail(f"source ZIP has a corrupt member: {bad}")
        for name, info in sorted(selected):
            payload = archive.read(info)
            if len(payload) != info.file_size:
                fail(f"source ZIP member size changed while reading: {name}")
            members.append((name, payload))
    if not members:
        fail(f"source ZIP contains no files: {source}")
    return members


def main() -> int:
    parser = argparse.ArgumentParser()
    inputs = parser.add_mutually_exclusive_group(required=True)
    inputs.add_argument("--directory", type=pathlib.Path)
    inputs.add_argument("--source-zip", type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--epoch", required=True, type=int)
    args = parser.parse_args()

    source = args.directory or args.source_zip
    assert source is not None
    if not source.exists():
        fail(f"input does not exist: {source}")
    if args.directory and not source.is_dir():
        fail(f"--directory input is not a directory: {source}")
    if args.source_zip and not source.is_file():
        fail(f"--source-zip input is not a file: {source}")

    members = directory_members(source) if args.directory else archive_members(source)
    timestamp = zip_time(args.epoch)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_name(args.output.name + ".tmp")
    try:
        with zipfile.ZipFile(temporary, "w", allowZip64=True) as output:
            for name, payload in members:
                write_member(output, name, payload, timestamp)
        os.replace(temporary, args.output)
    finally:
        temporary.unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
