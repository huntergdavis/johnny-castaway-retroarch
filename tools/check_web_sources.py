#!/usr/bin/env python3
"""Validate local-only Web Player sources and known upstream pins."""

from __future__ import annotations

import argparse
import html.parser
import pathlib
import subprocess
import urllib.parse


RETROARCH_REVISION = "96a1b1a9cf3f9166affcfd7df4323aa58d5c281a"
ASSETS_REVISION = "73106363e14e34c08a5854b4cfbc29f184e3b783"
ORIGINAL_DATA_NAMES = {"RESOURCE.MAP", "RESOURCE.001"}
GENERATED_LOCAL_REFERENCES = {"browserfs.min.js"}
REQUIRED_FILES = (
    "scripts/build-web-player.sh",
    "scripts/serve-web.sh",
    "tools/check_web_dist.py",
    "tools/serve_web.py",
    "web/index.html",
    "web/jc-web-player.js",
    "web/style.css",
    "web/WEB_PLAYER_NOTICE.md",
    "web/licenses/BrowserFS-license.md",
    "docs/licenses/BigSoundBank-0266-CC0.md",
)


class References(html.parser.HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.values: list[str] = []

    def handle_starttag(
        self, tag: str, attrs: list[tuple[str, str | None]]
    ) -> None:
        del tag
        for name, value in attrs:
            if name in {"href", "src"} and value and not value.startswith("#"):
                self.values.append(value)


def fail(message: str) -> None:
    raise SystemExit(f"web source check failed: {message}")


def tracked_files(root: pathlib.Path) -> list[pathlib.Path]:
    result = subprocess.run(
        ["git", "-C", str(root), "ls-files", "-z"],
        check=True,
        stdout=subprocess.PIPE,
    )
    return [root / item.decode() for item in result.stdout.split(b"\0") if item]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", nargs="?", default=".", type=pathlib.Path)
    args = parser.parse_args()
    root = args.root.resolve()

    for relative in REQUIRED_FILES:
        if not (root / relative).is_file():
            fail(f"missing {relative}")

    forbidden = [
        path.relative_to(root)
        for path in tracked_files(root)
        if path.name.upper() in ORIGINAL_DATA_NAMES
    ]
    if forbidden:
        fail("original game data is tracked: " + ", ".join(map(str, forbidden)))

    parser_instance = References()
    parser_instance.feed((root / "web/index.html").read_text(encoding="utf-8"))
    for reference in parser_instance.values:
        parsed = urllib.parse.urlparse(reference)
        if parsed.scheme or parsed.netloc or reference.startswith("//"):
            fail(f"web/index.html has an external dependency: {reference}")
        if (
            not (root / "web" / parsed.path).is_file()
            and parsed.path not in GENERATED_LOCAL_REFERENCES
        ):
            fail(f"web/index.html references missing local file: {reference}")

    build_script = (root / "scripts/build-web-player.sh").read_text(encoding="utf-8")
    notice = (root / "web/WEB_PLAYER_NOTICE.md").read_text(encoding="utf-8")
    for revision, project in (
        (RETROARCH_REVISION, "RetroArch"),
        (ASSETS_REVISION, "retroarch-assets"),
    ):
        if revision not in build_script:
            fail(f"{project} pin is missing from build-web-player.sh")
        if revision not in notice:
            fail(f"{project} pin is missing from WEB_PLAYER_NOTICE.md")

    player_script = (root / "web/jc-web-player.js").read_text(encoding="utf-8")
    if "http://" in player_script or "https://" in player_script:
        fail("web/jc-web-player.js contains a remote URL")

    print("Web source check passed: local-only references, pinned provenance, no game data")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
