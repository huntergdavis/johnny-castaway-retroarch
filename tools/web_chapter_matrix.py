#!/usr/bin/env python3
"""Run the authentic Firefox smoke gate across the exact chapter catalog.

This is deliberately an orchestration layer around web_smoke_test.py.  It does
not implement a second browser driver or a weaker visual gate.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile
from typing import Any, Callable


ROOT = pathlib.Path(__file__).resolve().parents[1]
CATALOG_PATH = ROOT / "src/jc_chapters.c"
SMOKE_RUNNER = ROOT / "tools/web_smoke_test.py"
EXPECTED_CHAPTER_COUNT = 63
RESULT_SCHEMA = 1
CATALOG_RE = re.compile(
    r"static\s+const\s+jc_chapter_t\s+chapters\s*\[\s*\]\s*=\s*\{"
    r"(?P<body>.*?)\s*\};",
    re.DOTALL,
)
CHAPTER_RE = re.compile(r'\bCHAPTER\s*\(\s*"([a-z][a-z0-9]*)"\s*,')
CSV_FIELDS = (
    "catalog_index",
    "slug",
    "status",
    "resumed",
    "subprocess_returncode",
    "smoke_passed",
    "error",
    "artifact_directory",
)


class MatrixFailure(RuntimeError):
    """A fail-closed catalog, selection, input, or result error."""


def parse_chapter_catalog(
    source: str, expected_count: int = EXPECTED_CHAPTER_COUNT
) -> list[str]:
    """Extract the ordered slugs only from the compiled chapter array."""
    match = CATALOG_RE.search(source)
    if match is None:
        raise MatrixFailure("could not locate the chapters[] catalog")
    body = match.group("body")
    slugs = CHAPTER_RE.findall(body)
    invocation_count = len(re.findall(r"\bCHAPTER\s*\(", body))
    if invocation_count != len(slugs):
        raise MatrixFailure("chapter catalog contains an unparseable CHAPTER entry")
    if len(slugs) != expected_count:
        raise MatrixFailure(
            f"chapter catalog has {len(slugs)} entries; expected {expected_count}"
        )
    if len(set(slugs)) != len(slugs):
        duplicates = sorted({slug for slug in slugs if slugs.count(slug) > 1})
        raise MatrixFailure("chapter catalog has duplicate slugs: " + ", ".join(duplicates))
    return slugs


def select_catalog(
    slugs: list[str],
    *,
    start: int | None = None,
    count: int | None = None,
    shard_index: int | None = None,
    shard_count: int | None = None,
) -> dict[str, Any]:
    """Return one exact, ordered range using range or balanced-shard syntax."""
    total = len(slugs)
    uses_shards = shard_index is not None or shard_count is not None
    uses_range = start is not None or count is not None
    if uses_shards and uses_range:
        raise MatrixFailure("--start/--count cannot be combined with shard options")
    if uses_shards:
        if shard_index is None or shard_count is None:
            raise MatrixFailure("--shard-index and --shard-count must be supplied together")
        if shard_count <= 0 or shard_count > total:
            raise MatrixFailure(f"--shard-count must be between 1 and {total}")
        if shard_index < 0 or shard_index >= shard_count:
            raise MatrixFailure("--shard-index must be zero-based and below --shard-count")
        selected_start = total * shard_index // shard_count
        selected_end = total * (shard_index + 1) // shard_count
        selection_id = f"shard-{shard_index + 1:03d}-of-{shard_count:03d}"
        mode = "shard"
    else:
        selected_start = 0 if start is None else start
        if selected_start < 0 or selected_start >= total:
            raise MatrixFailure(f"--start must be between 0 and {total - 1}")
        selected_count = total - selected_start if count is None else count
        if selected_count <= 0:
            raise MatrixFailure("--count must be positive")
        selected_end = selected_start + selected_count
        if selected_end > total:
            raise MatrixFailure(
                f"selected range ends at {selected_end}, beyond catalog size {total}"
            )
        selection_id = f"range-{selected_start:03d}-{selected_end:03d}"
        mode = "range"
    return {
        "id": selection_id,
        "mode": mode,
        "start": selected_start,
        "end_exclusive": selected_end,
        "count": selected_end - selected_start,
        "shard_index": shard_index if uses_shards else None,
        "shard_count": shard_count if uses_shards else None,
        "slugs": slugs[selected_start:selected_end],
    }


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def update_tree_digest(digest: Any, root: pathlib.Path) -> None:
    files = sorted(path for path in root.rglob("*") if path.is_file())
    if not files:
        raise MatrixFailure(f"distribution has no files: {root}")
    for path in files:
        relative = path.relative_to(root)
        if relative.parts and relative.parts[0] == "local-content":
            continue
        digest.update(relative.as_posix().encode("utf-8"))
        digest.update(b"\0")
        digest.update(bytes.fromhex(sha256_file(path)))


def input_fingerprint(
    catalog_sha256: str,
    dist: pathlib.Path,
    content_dir: pathlib.Path,
    smoke_runner: pathlib.Path = SMOKE_RUNNER,
) -> str:
    """Bind resume eligibility without exposing user-owned file names or bytes."""
    if not dist.is_dir():
        raise MatrixFailure(f"Web distribution directory does not exist: {dist}")
    if not smoke_runner.is_file():
        raise MatrixFailure(f"smoke runner does not exist: {smoke_runner}")
    resource_paths = [content_dir / "RESOURCE.MAP", content_dir / "RESOURCE.001"]
    missing = [path.name for path in resource_paths if not path.is_file()]
    if missing:
        raise MatrixFailure("user-owned content is incomplete: " + ", ".join(missing))
    digest = hashlib.sha256()
    digest.update(b"johnny-web-chapter-matrix-v1\0")
    digest.update(bytes.fromhex(catalog_sha256))
    digest.update(bytes.fromhex(sha256_file(smoke_runner)))
    update_tree_digest(digest, dist)
    for path in resource_paths:
        digest.update(path.name.encode("ascii"))
        digest.update(b"\0")
        digest.update(bytes.fromhex(sha256_file(path)))
    return digest.hexdigest()


def atomic_write(path: pathlib.Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", dir=path.parent
    )
    temporary = pathlib.Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="") as stream:
            stream.write(text)
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()


def write_json(path: pathlib.Path, value: Any) -> None:
    atomic_write(path, json.dumps(value, indent=2, sort_keys=True) + "\n")


def read_json_object(path: pathlib.Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise MatrixFailure(f"invalid JSON result: {path.name}") from error
    if not isinstance(value, dict):
        raise MatrixFailure(f"JSON result is not an object: {path.name}")
    return value


def validate_smoke_result(path: pathlib.Path, slug: str) -> dict[str, Any]:
    result = read_json_object(path)
    if result.get("chapter") != slug:
        raise MatrixFailure("smoke result chapter does not match requested slug")
    if result.get("passed") is not True:
        raise MatrixFailure("smoke result did not report passed=true")
    if not str(result.get("content", "")).startswith("user-owned local data"):
        raise MatrixFailure("smoke result did not use explicit user-owned content")
    return result


def empty_record(index: int, slug: str) -> dict[str, Any]:
    return {
        "catalog_index": index,
        "slug": slug,
        "status": "pending",
        "resumed": False,
        "subprocess_returncode": None,
        "smoke_passed": False,
        "error": "not-run",
        "artifact_directory": f"chapters/{slug}",
    }


def reusable_record(
    scene_dir: pathlib.Path, index: int, slug: str, fingerprint: str
) -> dict[str, Any] | None:
    marker_path = scene_dir / "matrix-result.json"
    result_path = scene_dir / "result.json"
    try:
        marker = read_json_object(marker_path)
        validate_smoke_result(result_path, slug)
    except MatrixFailure:
        return None
    expected = {
        "schema_version": RESULT_SCHEMA,
        "catalog_index": index,
        "slug": slug,
        "status": "passed",
        "input_fingerprint": fingerprint,
        "subprocess_returncode": 0,
        "smoke_passed": True,
        "smoke_result_sha256": sha256_file(result_path),
    }
    if any(marker.get(key) != value for key, value in expected.items()):
        return None
    record = empty_record(index, slug)
    record.update(
        status="passed",
        resumed=True,
        subprocess_returncode=0,
        smoke_passed=True,
        error="",
    )
    return record


CommandRunner = Callable[..., subprocess.CompletedProcess[Any]]


def run_scene(
    *,
    dist: pathlib.Path,
    content_dir: pathlib.Path,
    scene_dir: pathlib.Path,
    index: int,
    slug: str,
    fingerprint: str,
    timeout: float,
    no_xvfb: bool,
    smoke_runner: pathlib.Path = SMOKE_RUNNER,
    command_runner: CommandRunner = subprocess.run,
) -> dict[str, Any]:
    record = empty_record(index, slug)
    scene_dir.mkdir(parents=True, exist_ok=True)
    result_path = scene_dir / "result.json"
    previous_mtime = result_path.stat().st_mtime_ns if result_path.exists() else None
    command = [
        sys.executable,
        str(smoke_runner),
        "--dist",
        str(dist),
        "--content-dir",
        str(content_dir),
        "--chapter",
        slug,
        "--artifacts",
        str(scene_dir),
        "--require-browser",
        "--scene-visual-only",
        "--timeout",
        str(timeout),
    ]
    if no_xvfb:
        command.append("--no-xvfb")
    try:
        completed = command_runner(command, cwd=ROOT, check=False)
        returncode = int(completed.returncode)
    except OSError:
        returncode = None
    record["subprocess_returncode"] = returncode
    current_mtime = result_path.stat().st_mtime_ns if result_path.exists() else None
    fresh_result = current_mtime is not None and current_mtime != previous_mtime
    error = ""
    smoke_passed = False
    if returncode is None:
        error = "smoke-runner-exec-error"
    elif returncode != 0:
        error = "smoke-runner-nonzero-exit"
    elif not fresh_result:
        error = "missing-fresh-smoke-result"
    else:
        try:
            validate_smoke_result(result_path, slug)
            smoke_passed = True
        except MatrixFailure:
            error = "invalid-or-failed-smoke-result"
    record["smoke_passed"] = smoke_passed
    record["status"] = "passed" if returncode == 0 and smoke_passed else "failed"
    record["error"] = error
    marker = {
        "schema_version": RESULT_SCHEMA,
        "catalog_index": index,
        "slug": slug,
        "status": record["status"],
        "input_fingerprint": fingerprint,
        "subprocess_returncode": returncode,
        "smoke_passed": smoke_passed,
        "smoke_result_sha256": (
            sha256_file(result_path) if fresh_result and result_path.is_file() else None
        ),
    }
    write_json(scene_dir / "matrix-result.json", marker)
    return record


def summary_document(
    *,
    selection: dict[str, Any],
    records: list[dict[str, Any]],
    catalog_sha256: str,
    fingerprint: str,
) -> dict[str, Any]:
    counts = {
        "selected": len(records),
        "attempted": sum(
            record["status"] != "pending" and not record["resumed"]
            for record in records
        ),
        "resumed": sum(bool(record["resumed"]) for record in records),
        "passed": sum(record["status"] == "passed" for record in records),
        "failed": sum(record["status"] == "failed" for record in records),
        "pending": sum(record["status"] == "pending" for record in records),
    }
    accounted = (
        len(records) == selection["count"]
        and [record["slug"] for record in records] == selection["slugs"]
        and len({record["catalog_index"] for record in records}) == len(records)
    )
    passed = accounted and counts["passed"] == counts["selected"]
    return {
        "schema_version": RESULT_SCHEMA,
        "kind": "johnny-castaway-authentic-web-chapter-matrix",
        "passed": passed,
        "catalog": {
            "source": "src/jc_chapters.c",
            "sha256": catalog_sha256,
            "expected_count": EXPECTED_CHAPTER_COUNT,
            "actual_count": EXPECTED_CHAPTER_COUNT,
        },
        "selection": selection,
        "accounted": accounted,
        "input_fingerprint": fingerprint,
        "content": "explicit-user-owned-resource-pair",
        "counts": counts,
        "results": records,
    }


def write_summary(
    results_root: pathlib.Path,
    selection: dict[str, Any],
    records: list[dict[str, Any]],
    catalog_sha256: str,
    fingerprint: str,
) -> tuple[pathlib.Path, pathlib.Path, dict[str, Any]]:
    document = summary_document(
        selection=selection,
        records=records,
        catalog_sha256=catalog_sha256,
        fingerprint=fingerprint,
    )
    stem = f"summary-{selection['id']}"
    json_path = results_root / f"{stem}.json"
    csv_path = results_root / f"{stem}.csv"
    write_json(json_path, document)
    stream = io.StringIO(newline="")
    writer = csv.DictWriter(stream, fieldnames=CSV_FIELDS, extrasaction="ignore")
    writer.writeheader()
    writer.writerows(records)
    atomic_write(csv_path, stream.getvalue())
    return json_path, csv_path, document


def require_ignored_results_root(path: pathlib.Path) -> pathlib.Path:
    resolved = path.resolve()
    ignored_build = (ROOT / "build").resolve()
    try:
        resolved.relative_to(ignored_build)
    except ValueError as error:
        raise MatrixFailure(
            "--results must be beneath ignored build/ because it contains "
            "user-owned screenshots"
        ) from error
    return resolved


def paths_overlap(left: pathlib.Path, right: pathlib.Path) -> bool:
    try:
        left.relative_to(right)
        return True
    except ValueError:
        pass
    try:
        right.relative_to(left)
        return True
    except ValueError:
        return False


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "run tools/web_smoke_test.py once per selected authentic chapter; "
            "all selected scenes must pass"
        )
    )
    parser.add_argument(
        "--dist", type=pathlib.Path, default=ROOT / "build/web-player/dist"
    )
    parser.add_argument(
        "--content-dir",
        type=pathlib.Path,
        required=True,
        help="directory containing user-owned RESOURCE.MAP and RESOURCE.001",
    )
    parser.add_argument(
        "--results",
        type=pathlib.Path,
        default=ROOT / "build/web-chapter-matrix",
        help="ignored build/ directory for per-chapter artifacts and summaries",
    )
    parser.add_argument("--start", type=int, help="zero-based first catalog index")
    parser.add_argument("--count", type=int, help="number of catalog entries to run")
    parser.add_argument("--shard-index", type=int, help="zero-based shard index")
    parser.add_argument("--shard-count", type=int, help="number of balanced shards")
    parser.add_argument(
        "--resume",
        action="store_true",
        help="reuse only exact-input scenes with valid passing smoke results",
    )
    parser.add_argument(
        "--timeout", type=float, default=180.0, help="smoke runner wait timeout"
    )
    parser.add_argument(
        "--no-xvfb", action="store_true", help="pass --no-xvfb to the smoke runner"
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        catalog_source = CATALOG_PATH.read_text(encoding="utf-8")
        slugs = parse_chapter_catalog(catalog_source)
        catalog_sha256 = hashlib.sha256(catalog_source.encode("utf-8")).hexdigest()
        selection = select_catalog(
            slugs,
            start=args.start,
            count=args.count,
            shard_index=args.shard_index,
            shard_count=args.shard_count,
        )
        if args.timeout <= 0:
            raise MatrixFailure("--timeout must be positive")
        dist = args.dist.resolve()
        content_dir = args.content_dir.resolve()
        results_root = require_ignored_results_root(args.results)
        for left_name, left, right_name, right in (
            ("results", results_root, "distribution", dist),
            ("results", results_root, "content", content_dir),
            ("distribution", dist, "content", content_dir),
        ):
            if paths_overlap(left, right):
                raise MatrixFailure(
                    f"{left_name} and {right_name} directories must not overlap"
                )
        fingerprint = input_fingerprint(catalog_sha256, dist, content_dir)
        records = [
            empty_record(selection["start"] + offset, slug)
            for offset, slug in enumerate(selection["slugs"])
        ]
        json_path, csv_path, _ = write_summary(
            results_root, selection, records, catalog_sha256, fingerprint
        )
        for offset, slug in enumerate(selection["slugs"]):
            index = selection["start"] + offset
            scene_dir = results_root / "chapters" / slug
            record = (
                reusable_record(scene_dir, index, slug, fingerprint)
                if args.resume
                else None
            )
            if record is None:
                print(
                    f"[{offset + 1}/{selection['count']}] running {index:02d} {slug}",
                    flush=True,
                )
                record = run_scene(
                    dist=dist,
                    content_dir=content_dir,
                    scene_dir=scene_dir,
                    index=index,
                    slug=slug,
                    fingerprint=fingerprint,
                    timeout=args.timeout,
                    no_xvfb=args.no_xvfb,
                )
            else:
                print(
                    f"[{offset + 1}/{selection['count']}] resumed {index:02d} {slug}",
                    flush=True,
                )
            records[offset] = record
            json_path, csv_path, _ = write_summary(
                results_root, selection, records, catalog_sha256, fingerprint
            )
        _, _, summary = write_summary(
            results_root, selection, records, catalog_sha256, fingerprint
        )
        print(f"JSON summary: {json_path.relative_to(ROOT)}")
        print(f"CSV summary: {csv_path.relative_to(ROOT)}")
        if summary["passed"]:
            print(f"PASS: all {selection['count']} selected authentic chapters passed")
            return 0
        print(
            "FAIL: selected chapter matrix did not pass: "
            + json.dumps(summary["counts"], sort_keys=True),
            file=sys.stderr,
        )
        return 1
    except (MatrixFailure, OSError, UnicodeError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
