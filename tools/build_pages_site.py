#!/usr/bin/env python3
"""Validate and assemble the GitHub Pages site from a pinned Web release ZIP."""

from __future__ import annotations

import argparse
import hashlib
from html.parser import HTMLParser
import io
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import sys
from typing import Any, Iterable
from urllib.parse import unquote, urlparse
import zipfile


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SITE_SOURCE = ROOT / "site"
DEFAULT_RELEASE_MANIFEST = DEFAULT_SITE_SOURCE / "release-v0.1.4.json"

ORIGINAL_DATA_NAMES = frozenset(("RESOURCE.MAP", "RESOURCE.001"))
WEB_ARCHIVE_ROOT = "johnny-castaway-retroarch-web"
MAX_ARCHIVE_FILES = 5000
MAX_ARCHIVE_ENTRY_SIZE = 48 * 1024 * 1024
MAX_ARCHIVE_TOTAL_SIZE = 96 * 1024 * 1024
HEX_40_RE = re.compile(r"[0-9a-f]{40}\Z")
HEX_64_RE = re.compile(r"[0-9a-f]{64}\Z")
RELEASE_BASE = (
    "https://github.com/huntergdavis/johnny-castaway-retroarch/releases/"
)

REQUIRED_SITE_FILES = frozenset(
    (
        "index.html",
        "styles.css",
        "release-v0.1.4.json",
        "SCREENSHOT_PROVENANCE.md",
        "assets/screenshots/johnny-island.png",
        "assets/visitor-airplane.png",
        "assets/story-options.png",
    )
)

REQUIRED_WEB_FILES = frozenset(
    (
        "BUILD-PROVENANCE.txt",
        "CREDITS.md",
        "WEB_PLAYER_NOTICE.md",
        "assets/frontend/bundle.zip",
        "browserfs.min.js",
        "docs/PROVENANCE.md",
        "docs/THIRD_PARTY_NOTICES.md",
        "index.html",
        "jc-web-player.js",
        "johnny_castaway_libretro.js",
        "johnny_castaway_libretro.wasm",
        "licenses/BigSoundBank-0266-CC0.md",
        "licenses/BrowserFS-license.md",
        "licenses/RetroArch-GPL-3.0",
        "licenses/johnny-castaway-retroarch-GPL-3.0",
        "licenses/retroarch-assets-CC-BY-4.0",
        "style.css",
    )
)

PROVENANCE_HASH_PATHS = {
    "BrowserFS SHA-256": "browserfs.min.js",
    "JavaScript SHA-256": "johnny_castaway_libretro.js",
    "WebAssembly SHA-256": "johnny_castaway_libretro.wasm",
    "Asset bundle SHA-256": "assets/frontend/bundle.zip",
    "RetroArch license SHA-256": "licenses/RetroArch-GPL-3.0",
    "retroarch-assets license SHA-256": "licenses/retroarch-assets-CC-BY-4.0",
}


class PagesFailure(RuntimeError):
    """Fail-closed Pages source or release validation error."""


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while True:
            block = source.read(1024 * 1024)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()


def _safe_relative_path(value: str, *, description: str) -> PurePosixPath:
    if not value or "\\" in value or value.startswith("/"):
        raise PagesFailure(f"unsafe {description}: {value!r}")
    path = PurePosixPath(value)
    if path.as_posix() != value or any(part in ("", ".", "..") for part in path.parts):
        raise PagesFailure(f"unsafe {description}: {value!r}")
    return path


def _is_private_path(path: PurePosixPath) -> bool:
    folded_parts = tuple(part.casefold() for part in path.parts)
    return (
        path.name.upper() in ORIGINAL_DATA_NAMES
        or path.suffix.casefold() == ".wav"
        or "local-content" in folded_parts
    )


def load_release_manifest(path: Path = DEFAULT_RELEASE_MANIFEST) -> dict[str, Any]:
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise PagesFailure(f"could not read release manifest {path}: {error}") from error

    if not isinstance(manifest, dict) or manifest.get("schema_version") != 1:
        raise PagesFailure("release manifest must use schema_version 1")
    release = manifest.get("release")
    screenshots = manifest.get("screenshots")
    if not isinstance(release, dict) or not isinstance(screenshots, list):
        raise PagesFailure("release manifest is missing release/screenshots records")

    tag = release.get("tag")
    source_commit = release.get("source_commit")
    release_url = release.get("url")
    assets = release.get("assets")
    if not isinstance(tag, str) or not re.fullmatch(r"v[0-9]+\.[0-9]+\.[0-9]+", tag):
        raise PagesFailure("release tag is not a semantic vX.Y.Z tag")
    if not isinstance(source_commit, str) or not HEX_40_RE.fullmatch(source_commit):
        raise PagesFailure("release source_commit is not a lowercase 40-hex SHA")
    expected_release_url = f"{RELEASE_BASE}tag/{tag}"
    if release_url != expected_release_url:
        raise PagesFailure("release URL does not match the pinned tag")
    if not isinstance(assets, list) or not assets:
        raise PagesFailure("release asset inventory is empty")

    names: set[str] = set()
    urls: set[str] = set()
    web_assets: list[dict[str, Any]] = []
    for asset in assets:
        if not isinstance(asset, dict):
            raise PagesFailure("release asset entry is not an object")
        name = asset.get("name")
        size = asset.get("size")
        digest = asset.get("sha256")
        url = asset.get("url")
        if not isinstance(name, str) or Path(name).name != name:
            raise PagesFailure(f"invalid release asset name: {name!r}")
        if name.casefold() in names:
            raise PagesFailure(f"duplicate release asset name: {name}")
        names.add(name.casefold())
        if not isinstance(size, int) or isinstance(size, bool) or size <= 0:
            raise PagesFailure(f"invalid release asset size: {name}")
        if not isinstance(digest, str) or not HEX_64_RE.fullmatch(digest):
            raise PagesFailure(f"invalid release asset SHA-256: {name}")
        expected_url = f"{RELEASE_BASE}download/{tag}/{name}"
        if url != expected_url or url in urls:
            raise PagesFailure(f"invalid or duplicate release asset URL: {name}")
        urls.add(url)
        if asset.get("role") == "pages_web_player":
            web_assets.append(asset)

    if len(web_assets) != 1 or not web_assets[0]["name"].endswith("-web.zip"):
        raise PagesFailure("manifest must identify exactly one Web player ZIP")

    screenshot_paths: set[str] = set()
    for screenshot in screenshots:
        if not isinstance(screenshot, dict):
            raise PagesFailure("screenshot entry is not an object")
        raw_path = screenshot.get("path")
        digest = screenshot.get("sha256")
        screenshot_commit = screenshot.get("source_commit")
        source_artifact = screenshot.get("source_artifact")
        description = screenshot.get("description")
        if not isinstance(raw_path, str):
            raise PagesFailure("screenshot path is missing")
        relative = _safe_relative_path(raw_path, description="screenshot path")
        if relative.suffix.casefold() != ".png":
            raise PagesFailure(f"screenshot is not a PNG: {raw_path}")
        if raw_path.casefold() in screenshot_paths:
            raise PagesFailure(f"duplicate screenshot path: {raw_path}")
        screenshot_paths.add(raw_path.casefold())
        if not isinstance(digest, str) or not HEX_64_RE.fullmatch(digest):
            raise PagesFailure(f"invalid screenshot SHA-256: {raw_path}")
        if screenshot_commit != source_commit:
            raise PagesFailure(f"screenshot commit does not match release: {raw_path}")
        if (
            not isinstance(source_artifact, str)
            or not source_artifact.startswith("build/web-smoke-v014-cd62e39")
        ):
            raise PagesFailure(f"screenshot source artifact is not release-bound: {raw_path}")
        if not isinstance(description, str) or len(description.strip()) < 20:
            raise PagesFailure(f"screenshot description is incomplete: {raw_path}")

    return manifest


class _SiteParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.ids: set[str] = set()
        self.duplicate_ids: set[str] = set()
        self.hrefs: list[str] = []
        self.local_resources: list[str] = []
        self.remote_resources: list[str] = []
        self.image_alts: list[str | None] = []
        self.image_dimensions: list[tuple[str | None, str | None]] = []
        self.script_count = 0
        self.form_count = 0
        self.main_count = 0
        self.nav_count = 0
        self.h1_count = 0

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        values = dict(attrs)
        element_id = values.get("id")
        if element_id:
            if element_id in self.ids:
                self.duplicate_ids.add(element_id)
            self.ids.add(element_id)
        if tag == "a" and values.get("href"):
            self.hrefs.append(values["href"] or "")
        if tag == "img":
            source = values.get("src")
            if source:
                self.local_resources.append(source)
            self.image_alts.append(values.get("alt"))
            self.image_dimensions.append((values.get("width"), values.get("height")))
        if tag == "link" and values.get("href"):
            resource = values["href"] or ""
            if urlparse(resource).scheme:
                self.remote_resources.append(resource)
            else:
                self.local_resources.append(resource)
        if tag in ("script", "iframe", "object", "embed"):
            self.script_count += 1
            source = values.get("src") or values.get("data")
            if source and urlparse(source).scheme:
                self.remote_resources.append(source)
        if tag == "form":
            self.form_count += 1
        if tag == "main":
            self.main_count += 1
        if tag == "nav":
            self.nav_count += 1
        if tag == "h1":
            self.h1_count += 1


def _validate_source_paths(site_source: Path) -> None:
    if not site_source.is_dir():
        raise PagesFailure(f"site source is not a directory: {site_source}")
    actual_files = {
        path.relative_to(site_source).as_posix()
        for path in site_source.rglob("*")
        if path.is_file()
    }
    missing = REQUIRED_SITE_FILES - actual_files
    if missing:
        raise PagesFailure("site source is missing: " + ", ".join(sorted(missing)))
    for raw_path in sorted(actual_files):
        relative = _safe_relative_path(raw_path, description="site source path")
        if _is_private_path(relative):
            raise PagesFailure(f"private content is forbidden in site source: {raw_path}")
        if relative.suffix.casefold() in (".js", ".wasm"):
            raise PagesFailure(f"site source may not vendor runtime code: {raw_path}")


def validate_site_source(
    site_source: Path = DEFAULT_SITE_SOURCE,
    manifest_path: Path = DEFAULT_RELEASE_MANIFEST,
) -> dict[str, Any]:
    manifest = load_release_manifest(manifest_path)
    _validate_source_paths(site_source)

    html_path = site_source / "index.html"
    try:
        html = html_path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise PagesFailure(f"could not read site index: {error}") from error
    parser = _SiteParser()
    try:
        parser.feed(html)
        parser.close()
    except Exception as error:
        raise PagesFailure(f"site HTML parser failed: {error}") from error

    if parser.duplicate_ids:
        raise PagesFailure("duplicate HTML ids: " + ", ".join(sorted(parser.duplicate_ids)))
    if (parser.main_count, parser.nav_count, parser.h1_count) != (1, 1, 1):
        raise PagesFailure("site must contain exactly one main, nav, and h1 element")
    if parser.script_count or parser.form_count:
        raise PagesFailure("static site source may not contain scripts, embeds, or forms")
    if parser.remote_resources:
        raise PagesFailure("remote runtime resources are forbidden: " + ", ".join(parser.remote_resources))
    if any(alt is None or not alt.strip() for alt in parser.image_alts):
        raise PagesFailure("every site image must have useful non-empty alt text")
    if any(width != "640" or height != "480" for width, height in parser.image_dimensions):
        raise PagesFailure("every capture must declare its exact 640x480 dimensions")

    for href in parser.hrefs:
        if href.startswith("#"):
            if len(href) == 1 or unquote(href[1:]) not in parser.ids:
                raise PagesFailure(f"broken in-page link: {href}")
        elif not urlparse(href).scheme and href not in ("play/", "./play/"):
            relative = _safe_relative_path(unquote(href), description="local site link")
            if not (site_source / relative).is_file():
                raise PagesFailure(f"broken local site link: {href}")

    for resource in parser.local_resources:
        relative = _safe_relative_path(unquote(resource), description="local site resource")
        if not (site_source / relative).is_file():
            raise PagesFailure(f"broken local site resource: {resource}")

    expected_asset_urls = {
        str(asset["url"]) for asset in manifest["release"]["assets"]
    }
    linked_asset_urls = {
        href
        for href in parser.hrefs
        if href.startswith(f"{RELEASE_BASE}download/")
    }
    if linked_asset_urls != expected_asset_urls:
        missing = expected_asset_urls - linked_asset_urls
        extra = linked_asset_urls - expected_asset_urls
        raise PagesFailure(
            "release asset links do not match the pinned inventory; "
            f"missing={sorted(missing)}, extra={sorted(extra)}"
        )

    css = (site_source / "styles.css").read_text(encoding="utf-8")
    if re.search(r"@import\b|https?://|url\s*\(", css, re.IGNORECASE):
        raise PagesFailure("site CSS may not import remote or vendored resources")
    for marker in (":focus-visible", "prefers-reduced-motion", "@media (max-width"):
        if marker not in css:
            raise PagesFailure(f"site CSS is missing accessibility/responsive marker: {marker}")

    for screenshot in manifest["screenshots"]:
        screenshot_path = site_source / str(screenshot["path"])
        if sha256_file(screenshot_path) != screenshot["sha256"]:
            raise PagesFailure(f"screenshot hash mismatch: {screenshot['path']}")

    normalized_html = " ".join(html.split())
    required_phrases = (
        "63 selectable scenes",
        "Closed captions",
        "Music &amp; sound",
        "36 holidays",
        "live chapter previews",
        "do <strong>not</strong> claim physical-device",
        "not sent to this project's server",
    )
    for phrase in required_phrases:
        if phrase not in normalized_html:
            raise PagesFailure(f"site is missing required release/legal statement: {phrase}")
    return manifest


def _validate_zip_members(
    infos: Iterable[zipfile.ZipInfo], *, archive_description: str
) -> dict[str, zipfile.ZipInfo]:
    files: dict[str, zipfile.ZipInfo] = {}
    folded_names: set[str] = set()
    total_size = 0
    for info in infos:
        if info.flag_bits & 0x1:
            raise PagesFailure(f"encrypted member in {archive_description}: {info.filename}")
        raw_name = info.filename.rstrip("/") if info.is_dir() else info.filename
        if not raw_name:
            continue
        relative = _safe_relative_path(raw_name, description=f"{archive_description} member")
        file_type = (info.external_attr >> 16) & 0o170000
        if file_type == stat.S_IFLNK:
            raise PagesFailure(f"symlink member in {archive_description}: {info.filename}")
        if _is_private_path(relative):
            raise PagesFailure(f"private content in {archive_description}: {info.filename}")
        if info.file_size > MAX_ARCHIVE_ENTRY_SIZE:
            raise PagesFailure(f"oversized member in {archive_description}: {info.filename}")
        total_size += info.file_size
        if total_size > MAX_ARCHIVE_TOTAL_SIZE:
            raise PagesFailure(f"uncompressed {archive_description} exceeds safety limit")
        if info.is_dir():
            continue
        folded = relative.as_posix().casefold()
        if folded in folded_names:
            raise PagesFailure(f"duplicate member in {archive_description}: {info.filename}")
        folded_names.add(folded)
        files[relative.as_posix()] = info
    if len(files) > MAX_ARCHIVE_FILES:
        raise PagesFailure(f"{archive_description} contains too many files")
    return files


def _parse_build_provenance(data: bytes) -> dict[str, str]:
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as error:
        raise PagesFailure("Web BUILD-PROVENANCE.txt is not UTF-8") from error
    values: dict[str, str] = {}
    for line in text.splitlines():
        if ": " in line:
            key, value = line.split(": ", 1)
            values[key] = value
    return values


def inspect_web_release(
    web_zip: Path, manifest: dict[str, Any]
) -> tuple[zipfile.ZipFile, dict[str, zipfile.ZipInfo]]:
    web_asset = next(
        asset
        for asset in manifest["release"]["assets"]
        if asset.get("role") == "pages_web_player"
    )
    if not web_zip.is_file():
        raise PagesFailure(f"Web release ZIP does not exist: {web_zip}")
    actual_size = web_zip.stat().st_size
    actual_digest = sha256_file(web_zip)
    if actual_size != web_asset["size"]:
        raise PagesFailure(
            f"Web release size mismatch: expected {web_asset['size']}, got {actual_size}"
        )
    if actual_digest != web_asset["sha256"]:
        raise PagesFailure(
            f"Web release SHA-256 mismatch: expected {web_asset['sha256']}, got {actual_digest}"
        )

    try:
        archive = zipfile.ZipFile(web_zip)
        all_files = _validate_zip_members(
            archive.infolist(), archive_description="Web release ZIP"
        )
        if archive.testzip() is not None:
            raise PagesFailure("Web release ZIP contains a corrupt member")
    except (OSError, zipfile.BadZipFile, RuntimeError) as error:
        raise PagesFailure(f"could not inspect Web release ZIP: {error}") from error

    roots = {PurePosixPath(name).parts[0] for name in all_files}
    if roots != {WEB_ARCHIVE_ROOT}:
        archive.close()
        raise PagesFailure(f"Web release ZIP has unexpected root(s): {sorted(roots)}")
    stripped = {
        PurePosixPath(name).relative_to(WEB_ARCHIVE_ROOT).as_posix(): info
        for name, info in all_files.items()
    }
    if set(stripped) != REQUIRED_WEB_FILES:
        missing = REQUIRED_WEB_FILES - set(stripped)
        extra = set(stripped) - REQUIRED_WEB_FILES
        archive.close()
        raise PagesFailure(
            "Web release file inventory is not exact; "
            f"missing={sorted(missing)}, extra={sorted(extra)}"
        )

    provenance = _parse_build_provenance(archive.read(stripped["BUILD-PROVENANCE.txt"]))
    expected_commit = manifest["release"]["source_commit"]
    expected_tag = manifest["release"]["tag"]
    expected_version = expected_tag.removeprefix("v")
    if provenance.get("Johnny Castaway commit") != f"{expected_commit} (clean)":
        archive.close()
        raise PagesFailure("Web release provenance is not the exact clean source commit")
    if provenance.get("Frontend version") != expected_version:
        archive.close()
        raise PagesFailure("Web release frontend version does not match the release tag")
    if provenance.get("Tree state") != "clean":
        archive.close()
        raise PagesFailure("Web release provenance does not report a clean tree")
    for label, relative_path in PROVENANCE_HASH_PATHS.items():
        expected_digest = provenance.get(label)
        if not expected_digest or not HEX_64_RE.fullmatch(expected_digest):
            archive.close()
            raise PagesFailure(f"Web release provenance is missing {label}")
        actual = sha256_bytes(archive.read(stripped[relative_path]))
        if actual != expected_digest:
            archive.close()
            raise PagesFailure(f"Web release embedded hash mismatch: {relative_path}")

    bundle_data = archive.read(stripped["assets/frontend/bundle.zip"])
    try:
        with zipfile.ZipFile(io.BytesIO(bundle_data)) as nested:
            nested_files = _validate_zip_members(
                nested.infolist(), archive_description="nested frontend bundle"
            )
            if nested.testzip() is not None:
                archive.close()
                raise PagesFailure("nested frontend bundle contains a corrupt member")
            if not nested_files:
                archive.close()
                raise PagesFailure("nested frontend bundle is empty")
    except (zipfile.BadZipFile, RuntimeError) as error:
        archive.close()
        raise PagesFailure(f"could not inspect nested frontend bundle: {error}") from error

    return archive, stripped


def _write_output_manifest(output: Path) -> None:
    manifest_path = output / "ASSET-MANIFEST.sha256"
    lines: list[str] = []
    for path in sorted(output.rglob("*")):
        if path.is_file() and path != manifest_path:
            lines.append(f"{sha256_file(path)}  {path.relative_to(output).as_posix()}\n")
    manifest_path.write_text("".join(lines), encoding="utf-8", newline="\n")


def assemble_pages_site(
    site_source: Path,
    manifest_path: Path,
    web_zip: Path,
    output: Path,
) -> tuple[int, int]:
    manifest = validate_site_source(site_source, manifest_path)
    archive, web_files = inspect_web_release(web_zip, manifest)
    if output.exists():
        archive.close()
        raise PagesFailure(f"refusing to replace existing Pages output: {output}")

    try:
        shutil.copytree(site_source, output, copy_function=shutil.copyfile)
        play_root = output / "play"
        play_root.mkdir(mode=0o755)
        for relative_name in sorted(web_files):
            target = play_root / Path(relative_name)
            target.parent.mkdir(parents=True, exist_ok=True, mode=0o755)
            target.write_bytes(archive.read(web_files[relative_name]))
            target.chmod(0o644)
        (output / ".nojekyll").write_bytes(b"")
        release = manifest["release"]
        web_asset = next(
            asset
            for asset in release["assets"]
            if asset.get("role") == "pages_web_player"
        )
        (output / "PAGES-BUILD-PROVENANCE.txt").write_text(
            "Johnny Castaway RetroArch GitHub Pages assembly\n"
            f"Web release: {release['tag']}\n"
            f"Web source commit: {release['source_commit']}\n"
            f"Web archive: {web_asset['name']}\n"
            f"Web archive SHA-256: {web_asset['sha256']}\n"
            "Original Sierra/Dynamix data included: no\n"
            "User-supplied WAV files included: no\n",
            encoding="utf-8",
            newline="\n",
        )
        for directory in output.rglob("*"):
            if directory.is_dir():
                directory.chmod(0o755)
        for path in output.rglob("*"):
            if path.is_file():
                path.chmod(0o644)
                relative = PurePosixPath(path.relative_to(output).as_posix())
                if _is_private_path(relative):
                    raise PagesFailure(f"private content reached Pages output: {relative}")
        _write_output_manifest(output)
    except Exception:
        # The caller selects a fresh ignored output. Leave a failed tree available for
        # diagnosis instead of deleting a path that may unexpectedly contain user data.
        raise
    finally:
        archive.close()

    site_count = sum(1 for path in site_source.rglob("*") if path.is_file())
    return site_count, len(web_files)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--site-source", type=Path, default=DEFAULT_SITE_SOURCE)
    parser.add_argument("--release-manifest", type=Path, default=DEFAULT_RELEASE_MANIFEST)
    parser.add_argument(
        "--validate-source-only",
        action="store_true",
        help="validate the tracked site, links, release manifest, and screenshots only",
    )
    parser.add_argument("--web-zip", type=Path, help="downloaded pinned Web release ZIP")
    parser.add_argument("--output", type=Path, help="new Pages artifact directory")
    args = parser.parse_args(argv)
    if args.validate_source_only:
        if args.web_zip is not None or args.output is not None:
            parser.error("--validate-source-only cannot be combined with --web-zip/--output")
    elif args.web_zip is None or args.output is None:
        parser.error("assembly requires both --web-zip and --output")
    return args


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    try:
        if args.validate_source_only:
            manifest = validate_site_source(args.site_source, args.release_manifest)
            print(
                "Validated Pages source: "
                f"{len(manifest['release']['assets'])} release assets, "
                f"{len(manifest['screenshots'])} authentic captures"
            )
        else:
            site_count, web_count = assemble_pages_site(
                args.site_source,
                args.release_manifest,
                args.web_zip,
                args.output,
            )
            print(
                f"Assembled Pages site at {args.output}: "
                f"{site_count} tracked site files, {web_count} release Web files"
            )
    except PagesFailure as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
