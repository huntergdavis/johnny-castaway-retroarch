#!/usr/bin/env python3
"""Exercise the generated RetroArch Web Player in a real Firefox browser.

This deliberately uses only Python's standard library plus the W3C WebDriver
HTTP protocol.  By default the content files come from tests/test_libretro.c's
synthetic fixture generator; an explicit --content-dir enables a local-only
automatic-story menu check with user-owned data.
"""

from __future__ import annotations

import argparse
import base64
import contextlib
import functools
import hashlib
import http.server
import json
import os
import pathlib
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request
import zlib
from typing import Any


ROOT = pathlib.Path(__file__).resolve().parents[1]
WEBDRIVER_ELEMENT = "element-6066-11e4-a52e-4f735466cecf"
RETROARCH_MENU_OK = "z"
WEBDRIVER_KEY_END = "\ue010"
WEBDRIVER_KEY_HOME = "\ue011"
WEBDRIVER_KEY_DOWN = "\ue015"
EARLY_CHAPTER_NOTIFICATION_SETTLE_SECONDS = 6.0
EARLY_CHAPTER_FIRST_CAPTURE_DELAY_SECONDS = 0.25
EARLY_CHAPTER_CAPTURE_INTERVAL_SECONDS = 0.5
EARLY_CHAPTER_ACTOR_REGION = (
    500.0 / 640.0,
    220.0 / 480.0,
    580.0 / 640.0,
    330.0 / 480.0,
)
EARLY_CHAPTER_MINIMUM_ACTOR_MOTION_RATIO = 0.005
# Exact output of make_synthetic_content() in tests/test_libretro.c.  Keeping
# this browser fixture self-contained prevents unrelated host-test assertions
# from blocking it.  Both payloads are generated project data, not game data.
SYNTHETIC_MAP_B64 = (
    "AAAAAAAAUkVTT1VSQ0UuMDAxAAUAIQMAAAAAAAA0AAAAIQMAADQAAABVAwAAaAAAAIkDAAB1"
    "AAAA8QMAAA=="
)
SYNTHETIC_ARCHIVE_B64 = (
    "Sk9ITkNBU1QuUEFMABADAABQQUw6AAAAAFZHQToAAAAAAAAAPwAAAD8AAAA/Pz8/AAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAASU5UUk8uU0NS"
    "AAAAACMAAABTQ1I6AAAAAERJTToEAAAAAgACAEJJTjoHAAAAAAIAAAAREUlTTEFORDIuU0NS"
    "AAAjAAAAU0NSOgAAAABESU06BAAAAAIAAgBCSU46BwAAAAACAAAAIjNGSVNISU5HLkFEUwAA"
    "VwAAAFZFUjoFAAAAMS4yMABBRFM6AAAAAFJFUzoAAAAAAQADAEZJU0guVFRNAFNDUjoTAAAA"
    "AA4AAAABAAUgAwABAAAAAAAQFVRBRzoAAAAAAQABAHN0YXJ0AEZJU0guVFRNAAAAAABkAAAA"
    "VkVSOgUAAAAxLjIwAFBBRzoBAAAAAABUVDM6LQAAAAAoAAAAEREBACEQAQAf8ElOVFJPLlND"
    "UgBf8EpPSE5DQVNULlBBTAAA8A8QAVRUSToAAAAAVEFHOgAAAAABAAEAb25lAA=="
)
SYNTHETIC_SHA256 = {
    "RESOURCE.MAP": "604d0158a6f0ec5a7ede1aee6ccd163f93889778705e8f5ae25f5880ad4301fe",
    "RESOURCE.001": "6db50124d8a0fcdd48812c526688115886d935727780a59bad86a82cfbca807c",
}


class SmokeFailure(RuntimeError):
    """A browser smoke-test assertion failed."""


class WebDriver:
    """Small W3C WebDriver client sufficient for this smoke test."""

    def __init__(self, endpoint: str) -> None:
        self.endpoint = endpoint.rstrip("/")
        self.session_id: str | None = None

    def request(
        self, method: str, path: str, payload: dict[str, Any] | None = None
    ) -> Any:
        data = None if payload is None else json.dumps(payload).encode("utf-8")
        request = urllib.request.Request(
            f"{self.endpoint}{path}",
            data=data,
            method=method,
            headers={"Content-Type": "application/json; charset=utf-8"},
        )
        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                result = json.load(response)
        except urllib.error.HTTPError as error:
            body = error.read().decode("utf-8", "replace")
            try:
                detail = json.loads(body).get("value", {})
                message = detail.get("message", body)
            except json.JSONDecodeError:
                message = body
            raise SmokeFailure(
                f"WebDriver {method} {path} failed ({error.code}): {message}"
            ) from error

        value = result.get("value")
        # A page script may legitimately return {"error": false}.  WebDriver
        # protocol errors instead carry a string error code and message.
        if (
            isinstance(value, dict)
            and isinstance(value.get("error"), str)
            and "message" in value
        ):
            raise SmokeFailure(f"WebDriver error: {value['message']}")
        return value

    def new_session(
        self, firefox_binary: str, headless: bool, block_autoplay: bool = False
    ) -> None:
        arguments = ["-headless"] if headless else []
        preferences = {
            "devtools.console.stdout.content": True,
            "media.autoplay.default": 5 if block_autoplay else 0,
            "media.autoplay.block-webaudio": block_autoplay,
            "webgl.disabled": False,
            "webgl.force-enabled": True,
        }
        value = self.request(
            "POST",
            "/session",
            {
                "capabilities": {
                    "alwaysMatch": {
                        "browserName": "firefox",
                        "acceptInsecureCerts": False,
                        "moz:firefoxOptions": {
                            "binary": firefox_binary,
                            "args": arguments,
                            "prefs": preferences,
                        },
                    }
                }
            },
        )
        if not isinstance(value, dict) or not value.get("sessionId"):
            raise SmokeFailure("geckodriver did not return a Firefox session id")
        self.session_id = value["sessionId"]

    @property
    def session_path(self) -> str:
        if not self.session_id:
            raise SmokeFailure("Firefox WebDriver session was not created")
        return f"/session/{self.session_id}"

    def navigate(self, url: str) -> None:
        self.request("POST", f"{self.session_path}/url", {"url": url})

    def find(self, selector: str) -> str:
        value = self.request(
            "POST",
            f"{self.session_path}/element",
            {"using": "css selector", "value": selector},
        )
        if not isinstance(value, dict) or WEBDRIVER_ELEMENT not in value:
            raise SmokeFailure(f"WebDriver did not find {selector}")
        return value[WEBDRIVER_ELEMENT]

    def upload(self, element: str, paths: list[pathlib.Path]) -> None:
        text = "\n".join(str(path.resolve()) for path in paths)
        self.request(
            "POST",
            f"{self.session_path}/element/{element}/value",
            {"text": text, "value": list(text)},
        )

    def click(self, element: str) -> None:
        self.request("POST", f"{self.session_path}/element/{element}/click", {})

    def key_press(
        self, value: str, hold_ms: int = 120, release_ms: int = 350
    ) -> None:
        """Send one key edge through RetroArch's frame-polled Web input.

        The release interval must outlast a slow browser callback. Otherwise
        adjacent synthetic taps can appear as one continuous hold and engage
        RetroArch's menu auto-repeat. Keep the press itself below the default
        256 ms menu repeat delay.
        """
        self.request(
            "POST",
            f"{self.session_path}/actions",
            {
                "actions": [
                    {
                        "type": "key",
                        "id": "keyboard",
                        "actions": [
                            {"type": "keyDown", "value": value},
                            {"type": "pause", "duration": hold_ms},
                            {"type": "keyUp", "value": value},
                            {"type": "pause", "duration": release_ms},
                        ],
                    }
                ]
            },
        )

    def execute(self, script: str, *arguments: Any) -> Any:
        return self.request(
            "POST",
            f"{self.session_path}/execute/sync",
            {"script": script, "args": list(arguments)},
        )

    def screenshot(self, element: str, destination: pathlib.Path) -> str:
        encoded = self.request(
            "GET", f"{self.session_path}/element/{element}/screenshot"
        )
        if not isinstance(encoded, str):
            raise SmokeFailure("WebDriver did not return a PNG screenshot")
        image = base64.b64decode(encoded, validate=True)
        destination.write_bytes(image)
        return hashlib.sha256(image).hexdigest()

    def close(self) -> None:
        if self.session_id:
            with contextlib.suppress(Exception):
                self.request("DELETE", self.session_path)
            self.session_id = None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="smoke-test the generated player in real Firefox"
    )
    parser.add_argument(
        "--dist", type=pathlib.Path, default=ROOT / "build/web-player/dist"
    )
    parser.add_argument(
        "--artifacts", type=pathlib.Path, default=ROOT / "build/web-smoke"
    )
    content_source = parser.add_mutually_exclusive_group()
    content_source.add_argument(
        "--content-dir",
        type=pathlib.Path,
        help=(
            "use user-owned RESOURCE.MAP/RESOURCE.001 for an optional "
            "Automatic Story menu check"
        ),
    )
    content_source.add_argument(
        "--staged-local-content",
        action="store_true",
        help=(
            "exercise automatic loading from dist/local-content without a browser "
            "file upload; local files are excluded from the pristine distribution check"
        ),
    )
    parser.add_argument(
        "--chapter",
        help=(
            "runner-only fixed chapter for --content-dir scripted-motion acceptance; "
            "the value must be present in src/jc_chapters.c"
        ),
    )
    parser.add_argument("--timeout", type=float, default=45.0)
    parser.add_argument(
        "--require-browser",
        action="store_true",
        help="fail instead of skipping when Firefox/geckodriver/Xvfb is unavailable",
    )
    parser.add_argument(
        "--no-xvfb",
        action="store_true",
        help="use Firefox native headless mode instead of automatically using Xvfb",
    )
    parser.add_argument(
        "--test-audio-unlock",
        action="store_true",
        help=(
            "with --staged-local-content, block Firefox WebAudio autoplay and "
            "prove that visuals run before the Enable Audio gesture"
        ),
    )
    parser.add_argument(
        "--scene-visual-only",
        action="store_true",
        help=(
            "with --chapter and --content-dir, stop after strict temporal "
            "gameplay, frame-quality, WebGL, and audio acceptance"
        ),
    )
    parser.add_argument(
        "--test-late-ending",
        action="store_true",
        help=(
            "with --chapter johnny1 and --content-dir, wait for the final "
            "The End replacement and require clean stable title frames"
        ),
    )
    parser.add_argument(
        "--test-early-chapter-motion",
        action="store_true",
        help=(
            "with --chapter and --content-dir, let startup notifications expire, "
            "reset the fixed chapter, and prove early actor motion before a short "
            "chapter reaches its terminal hold"
        ),
    )
    return parser.parse_args()


def validate_scene_visual_only_args(args: argparse.Namespace) -> None:
    if args.scene_visual_only and (not args.chapter or args.content_dir is None):
        raise SmokeFailure(
            "--scene-visual-only requires --chapter and --content-dir"
        )


def validate_late_ending_args(args: argparse.Namespace) -> None:
    if not args.test_late_ending:
        return
    if args.chapter != "johnny1" or args.content_dir is None:
        raise SmokeFailure(
            "--test-late-ending requires --chapter johnny1 and --content-dir"
        )


def validate_early_chapter_motion_args(args: argparse.Namespace) -> None:
    if not args.test_early_chapter_motion:
        return
    if not args.chapter or args.content_dir is None:
        raise SmokeFailure(
            "--test-early-chapter-motion requires --chapter and --content-dir"
        )


def complete_scene_visual_only_result(
    result: dict[str, Any],
    game_paths: list[pathlib.Path],
    game_hashes: list[str],
    final_state: dict[str, Any],
) -> None:
    temporal = result.get("temporal_gameplay")
    if not isinstance(temporal, dict) or temporal.get("passed") is not True:
        raise SmokeFailure("scene visual-only result lacks passing temporal evidence")
    result["screenshots"] = {
        "game_sha256": game_hashes[0],
        "gameplay_sequence_sha256": {
            path.name: digest for path, digest in zip(game_paths, game_hashes)
        },
    }
    late_ending = result.get("late_ending")
    if isinstance(late_ending, dict):
        result["screenshots"]["late_ending_sequence_sha256"] = (
            late_ending.get("frame_sha256", {})
        )
    result["menu_navigation"] = {
        "performed": False,
        "reason": "scene-visual-only",
    }
    result["final"] = final_state
    result["passed"] = True


def wait_until(description: str, timeout: float, operation: Any) -> Any:
    deadline = time.monotonic() + timeout
    last_value: Any = None
    while time.monotonic() < deadline:
        last_value = operation()
        if last_value:
            return last_value
        time.sleep(0.2)
    raise SmokeFailure(f"timed out waiting for {description}; last value: {last_value!r}")


def available_port() -> int:
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


def wait_for_port(process: subprocess.Popen[bytes], port: int, timeout: float) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise SmokeFailure(f"geckodriver exited with status {process.returncode}")
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                return
        except OSError:
            time.sleep(0.1)
    raise SmokeFailure("timed out waiting for geckodriver")


def prepare_synthetic_content(directory: pathlib.Path) -> tuple[pathlib.Path, pathlib.Path]:
    directory.mkdir(parents=True, exist_ok=True)
    map_path = directory / "RESOURCE.MAP"
    archive_path = directory / "RESOURCE.001"
    for path, encoded in (
        (map_path, SYNTHETIC_MAP_B64),
        (archive_path, SYNTHETIC_ARCHIVE_B64),
    ):
        data = base64.b64decode(encoded, validate=True)
        digest = hashlib.sha256(data).hexdigest()
        if digest != SYNTHETIC_SHA256[path.name]:
            raise SmokeFailure(f"embedded synthetic fixture checksum failed: {path.name}")
        path.write_bytes(data)
    return map_path, archive_path


def browser_programs(args: argparse.Namespace) -> tuple[str, str, str | None]:
    firefox = shutil.which("firefox")
    geckodriver = shutil.which("geckodriver")
    xvfb_run = (
        None
        if args.no_xvfb or os.environ.get("DISPLAY")
        else shutil.which("xvfb-run")
    )
    missing = [
        name
        for name, program in (("Firefox", firefox), ("geckodriver", geckodriver))
        if not program
    ]
    if not args.no_xvfb and not os.environ.get("DISPLAY") and not xvfb_run:
        missing.append("Xvfb/xvfb-run")
    if missing:
        message = "browser smoke test skipped; missing " + ", ".join(missing)
        if args.require_browser:
            raise SmokeFailure(message)
        print(f"SKIP: {message}")
        raise SystemExit(0)
    assert firefox and geckodriver
    # Ubuntu's /usr/bin/firefox is a shell wrapper.  W3C moz:firefoxOptions
    # requires the actual executable when an explicit binary is supplied.
    snap_firefox = pathlib.Path("/snap/firefox/current/usr/lib/firefox/firefox")
    with pathlib.Path(firefox).open("rb") as firefox_stream:
        is_launcher = firefox_stream.read(2) == b"#!"
    if is_launcher and snap_firefox.is_file():
        firefox = str(snap_firefox)
    snap_geckodriver = pathlib.Path(
        "/snap/firefox/current/usr/lib/firefox/geckodriver"
    )
    if (
        pathlib.Path(geckodriver).resolve() == pathlib.Path("/usr/bin/snap")
        and snap_geckodriver.is_file()
    ):
        geckodriver = str(snap_geckodriver)
    return firefox, geckodriver, xvfb_run


def rerun_under_xvfb(args: argparse.Namespace, xvfb_run: str | None) -> int | None:
    if not xvfb_run:
        return None
    environment = os.environ.copy()
    environment["LIBGL_ALWAYS_SOFTWARE"] = "1"
    command = [
        xvfb_run,
        "-a",
        "-s",
        "-screen 0 1440x1080x24",
        sys.executable,
        str(pathlib.Path(__file__).resolve()),
        *sys.argv[1:],
    ]
    return subprocess.run(command, cwd=ROOT, env=environment).returncode


def start_web_server(dist: pathlib.Path) -> tuple[http.server.ThreadingHTTPServer, str]:
    sys.path.insert(0, str(ROOT / "tools"))
    from serve_web import WebPlayerHandler  # pylint: disable=import-outside-toplevel

    handler = functools.partial(WebPlayerHandler, directory=str(dist))
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server, f"http://127.0.0.1:{server.server_port}/"


def validate_staged_local_content(dist: pathlib.Path) -> list[pathlib.Path]:
    """Validate release files separately from an explicit ignored local-data overlay."""

    local_content = dist / "local-content"
    required_names = {"RESOURCE.MAP", "RESOURCE.001"}
    permitted_names = required_names | {
        f"sound{sample_id}.wav"
        for sample_id in range(25)
        if sample_id not in (11, 13)
    }
    if local_content.is_symlink():
        raise SmokeFailure("staged local-content directory must not be a symbolic link")
    entries = sorted(local_content.iterdir()) if local_content.is_dir() else []
    if any(not path.is_file() or path.is_symlink() for path in entries):
        raise SmokeFailure(
            "staged local content must contain regular files only"
        )
    files = entries
    names = {path.name for path in files}
    missing = sorted(required_names - names)
    unexpected = sorted(names - permitted_names)
    if missing:
        raise SmokeFailure(
            "staged local content is missing: " + ", ".join(missing)
        )
    if unexpected:
        raise SmokeFailure(
            "staged local content has unexpected files: " + ", ".join(unexpected)
        )
    with tempfile.TemporaryDirectory(prefix="jc-web-pristine-") as temporary:
        pristine = pathlib.Path(temporary) / "dist"
        shutil.copytree(
            dist,
            pristine,
            ignore=shutil.ignore_patterns("local-content"),
        )
        subprocess.run(
            [sys.executable, str(ROOT / "tools/check_web_dist.py"), str(pristine)],
            cwd=ROOT,
            check=True,
        )
    return files


def diagnostics(driver: WebDriver) -> dict[str, Any]:
    value = driver.execute(
        """
        const smoke = window.__jcSmoke || {};
        const canvas = document.querySelector('#canvas');
        const status = document.querySelector('#status');
        return {
          status: status ? status.textContent : null,
          statusError: status ? status.classList.contains('error') : null,
          canvas: canvas ? {width: canvas.width, height: canvas.height} : null,
          menuEnabled: !document.querySelector('#menu').disabled,
          startEnabled: !document.querySelector('#start').disabled,
          audioGate: (() => {
            const state = document.querySelector('#audio-state');
            const button = document.querySelector('#audio-unlock-button');
            return {
              state: state ? state.dataset.state : null,
              text: state ? state.textContent : null,
              contextCount: state ? Number(state.dataset.contextCount || 0) : 0,
              sampleRate: state ? Number(state.dataset.sampleRate || 0) : 0,
              unlockEnabled: button ? !button.disabled : false,
            };
          })(),
          pageErrors: smoke.pageErrors || [],
          rejections: smoke.rejections || [],
          consoleErrors: smoke.consoleErrors || [],
          audioProbe: window.__jcAudioProbe || null,
          webglProbe: window.__jcWebGLProbe || null,
        };
        """
    )
    if not isinstance(value, dict):
        raise SmokeFailure("browser diagnostics were not an object")
    return value


def diagnostics_are_clean_running(value: Any) -> bool:
    """Return whether a post-command player snapshot is running and error-free."""
    return bool(
        isinstance(value, dict)
        and str(value.get("status", "")).startswith("Running.")
        and not value.get("statusError")
        and not value.get("pageErrors")
        and not value.get("rejections")
    )


def stable_screenshot(
    driver: WebDriver,
    element: str,
    destination: pathlib.Path,
) -> str:
    """Capture a settled, non-black frame after Ozone's page transition."""

    # Ozone briefly renders a black transition frame when a page is pushed.
    # Its background remains animated after settling, so exact PNG equality is
    # not a valid stability signal.
    time.sleep(2.0)
    result = driver.screenshot(element, destination)
    if destination.stat().st_size < 20_000:
        raise SmokeFailure(
            f"rendered menu screenshot is unexpectedly small: {destination}"
        )
    return result


def png_pixels(path: pathlib.Path) -> tuple[int, int, int, bytes]:
    """Decode an 8-bit RGB/RGBA WebDriver PNG using only the stdlib."""
    encoded = path.read_bytes()
    if not encoded.startswith(b"\x89PNG\r\n\x1a\n"):
        raise SmokeFailure(f"screenshot is not a PNG: {path}")
    position = 8
    width = height = bit_depth = color_type = interlace = 0
    compressed = bytearray()
    while position < len(encoded):
        length = struct.unpack(">I", encoded[position : position + 4])[0]
        chunk_type = encoded[position + 4 : position + 8]
        payload = encoded[position + 8 : position + 8 + length]
        position += 12 + length
        if chunk_type == b"IHDR":
            width, height, bit_depth, color_type, _, _, interlace = struct.unpack(
                ">IIBBBBB", payload
            )
        elif chunk_type == b"IDAT":
            compressed.extend(payload)
        elif chunk_type == b"IEND":
            break
    if bit_depth != 8 or color_type not in (2, 6) or interlace:
        raise SmokeFailure(
            f"unsupported WebDriver PNG format: depth={bit_depth}, "
            f"color={color_type}, interlace={interlace}"
        )
    channels = 3 if color_type == 2 else 4
    stride = width * channels
    filtered = zlib.decompress(compressed)
    rows = bytearray()
    previous = bytearray(stride)
    offset = 0
    for _ in range(height):
        filter_type = filtered[offset]
        current = bytearray(filtered[offset + 1 : offset + 1 + stride])
        offset += stride + 1
        for index in range(stride):
            left = current[index - channels] if index >= channels else 0
            above = previous[index]
            upper_left = previous[index - channels] if index >= channels else 0
            if filter_type == 1:
                current[index] = (current[index] + left) & 0xFF
            elif filter_type == 2:
                current[index] = (current[index] + above) & 0xFF
            elif filter_type == 3:
                current[index] = (current[index] + ((left + above) // 2)) & 0xFF
            elif filter_type == 4:
                estimate = left + above - upper_left
                distances = (
                    abs(estimate - left),
                    abs(estimate - above),
                    abs(estimate - upper_left),
                )
                predictor = (left, above, upper_left)[distances.index(min(distances))]
                current[index] = (current[index] + predictor) & 0xFF
            elif filter_type != 0:
                raise SmokeFailure(f"unsupported PNG filter {filter_type}: {path}")
        rows.extend(current)
        previous = current
    return width, height, channels, bytes(rows)


def assert_content_changed(
    before: pathlib.Path,
    after: pathlib.Path,
    description: str,
    minimum_ratio: float = 0.005,
) -> float:
    """Require a material menu-content change while ignoring header clocks."""
    width, height, channels, first = png_pixels(before)
    next_width, next_height, next_channels, second = png_pixels(after)
    if (next_width, next_height, next_channels) != (width, height, channels):
        raise SmokeFailure(f"screenshot geometry changed while checking {description}")
    first_row = min(80, height)
    last_row = max(first_row, height - 60)
    changed = total = 0
    for y_position in range(first_row, last_row):
        row_start = y_position * width * channels
        for x_position in range(width):
            pixel = row_start + x_position * channels
            total += 1
            if any(
                abs(first[pixel + channel] - second[pixel + channel]) > 12
                for channel in range(3)
            ):
                changed += 1
    ratio = changed / total if total else 0.0
    if ratio < minimum_ratio:
        raise SmokeFailure(
            f"{description} changed only {ratio:.3%} of menu-content pixels"
        )
    return ratio


def region_change_ratio(
    before: tuple[int, int, int, bytes],
    after: tuple[int, int, int, bytes],
    region: tuple[float, float, float, float],
) -> float:
    """Measure material RGB pixel changes inside a normalized canvas region."""
    width, height, channels, first = before
    next_width, next_height, next_channels, second = after
    if (next_width, next_height, next_channels) != (width, height, channels):
        raise SmokeFailure("gameplay screenshot geometry changed during sampling")
    left, top, right, bottom = region
    x_start = max(0, min(width, round(left * width)))
    x_end = max(x_start, min(width, round(right * width)))
    y_start = max(0, min(height, round(top * height)))
    y_end = max(y_start, min(height, round(bottom * height)))
    changed = total = 0
    for y_position in range(y_start, y_end):
        row_start = y_position * width * channels
        for x_position in range(x_start, x_end):
            pixel = row_start + x_position * channels
            total += 1
            if (
                abs(first[pixel] - second[pixel]) > 12
                or abs(first[pixel + 1] - second[pixel + 1]) > 12
                or abs(first[pixel + 2] - second[pixel + 2]) > 12
            ):
                changed += 1
    return changed / total if total else 0.0


def _largest_solid_rectangle(
    mask: list[bool], width: int, height: int
) -> tuple[int, int, int]:
    """Return pixel area, width, and height of the largest true rectangle."""
    heights = [0] * width
    best_area = best_width = best_height = 0
    for y_position in range(height):
        row = y_position * width
        for x_position in range(width):
            heights[x_position] = (
                heights[x_position] + 1 if mask[row + x_position] else 0
            )
        stack: list[tuple[int, int]] = []
        for x_position in range(width + 1):
            current_height = heights[x_position] if x_position < width else 0
            start = x_position
            while stack and stack[-1][1] > current_height:
                rectangle_start, rectangle_height = stack.pop()
                rectangle_width = x_position - rectangle_start
                rectangle_area = rectangle_width * rectangle_height
                if rectangle_area > best_area:
                    best_area = rectangle_area
                    best_width = rectangle_width
                    best_height = rectangle_height
                start = rectangle_start
            if not stack or stack[-1][1] < current_height:
                stack.append((start, current_height))
    return best_area, best_width, best_height


def _largest_component(
    mask: list[bool], width: int, height: int
) -> tuple[int, int, int]:
    """Return area and bounds of the largest four-connected true component."""
    seen = bytearray(width * height)
    best_area = best_width = best_height = 0
    for seed, present in enumerate(mask):
        if not present or seen[seed]:
            continue
        queue = [seed]
        seen[seed] = 1
        cursor = 0
        area = 0
        minimum_x = maximum_x = seed % width
        minimum_y = maximum_y = seed // width
        while cursor < len(queue):
            position = queue[cursor]
            cursor += 1
            y_position, x_position = divmod(position, width)
            area += 1
            minimum_x = min(minimum_x, x_position)
            maximum_x = max(maximum_x, x_position)
            minimum_y = min(minimum_y, y_position)
            maximum_y = max(maximum_y, y_position)
            for neighbor in (
                position - 1,
                position + 1,
                position - width,
                position + width,
            ):
                if neighbor < 0 or neighbor >= len(mask):
                    continue
                neighbor_y, neighbor_x = divmod(neighbor, width)
                if abs(neighbor_x - x_position) + abs(neighbor_y - y_position) != 1:
                    continue
                if mask[neighbor] and not seen[neighbor]:
                    seen[neighbor] = 1
                    queue.append(neighbor)
        if area > best_area:
            best_area = area
            best_width = maximum_x - minimum_x + 1
            best_height = maximum_y - minimum_y + 1
    return best_area, best_width, best_height


def frame_quality(
    frame: tuple[int, int, int, bytes]
) -> dict[str, float | int]:
    """Measure blank/lost canvas and indexed-renderer color-key leakage."""
    width, height, channels, pixels = frame
    total = len(pixels) // channels
    non_black = magenta = meaningful = renderer_key = 0
    renderer_key_mask: list[bool] = []
    for pixel in range(0, len(pixels), channels):
        red, green, blue = pixels[pixel : pixel + 3]
        opaque = channels == 3 or pixels[pixel + 3] > 16
        is_non_black = opaque and max(red, green, blue) > 12
        # The indexed renderer's key is 0xA800A8. XRGB8888/WebGL paths may
        # preserve that value or quantize it through RGB565 (observed as
        # #AD00AD in Firefox). Keep the older bright-magenta guard as well,
        # since a lost GL texture may be cleared to that diagnostic color.
        red_blue_min = min(red, blue)
        is_color_key = (
            red_blue_min >= 144
            and green <= 48
            and abs(red - blue) <= 24
            and red_blue_min - green >= 112
        )
        is_renderer_key = (
            opaque
            and 164 <= red <= 176
            and 164 <= blue <= 176
            and green <= 4
            and abs(red - blue) <= 2
        )
        non_black += int(is_non_black)
        magenta += int(opaque and is_color_key)
        renderer_key += int(is_renderer_key)
        meaningful += int(is_non_black and not is_color_key)
        renderer_key_mask.append(is_renderer_key)
    rectangle_pixels, rectangle_width, rectangle_height = (
        _largest_solid_rectangle(renderer_key_mask, width, height)
        if total and width * height == total
        else (0, 0, 0)
    )
    component_pixels, component_width, component_height = (
        _largest_component(renderer_key_mask, width, height)
        if total and width * height == total
        else (0, 0, 0)
    )
    return {
        "non_black_ratio": non_black / total if total else 0.0,
        "magenta_ratio": magenta / total if total else 0.0,
        "renderer_key_ratio": renderer_key / total if total else 0.0,
        "renderer_key_rect_ratio": rectangle_pixels / total if total else 0.0,
        "renderer_key_rect_pixels": rectangle_pixels,
        "renderer_key_rect_width": rectangle_width,
        "renderer_key_rect_height": rectangle_height,
        "renderer_key_component_pixels": component_pixels,
        "renderer_key_component_width": component_width,
        "renderer_key_component_height": component_height,
        "meaningful_ratio": meaningful / total if total else 0.0,
    }


def frame_has_color_key_failure(quality: dict[str, float | int]) -> bool:
    """Reject lost frames or a material rectangular renderer-key leak."""
    return bool(
        quality["magenta_ratio"] >= 0.50
        or (
            quality["renderer_key_component_pixels"] >= 256
            and quality["renderer_key_component_width"] >= 16
            and quality["renderer_key_component_height"] >= 4
        )
    )


def evaluate_strict_audio_window(
    audio: Any, description: str, *, allow_paused: bool = False
) -> tuple[dict[str, Any], list[str]]:
    """Evaluate one idle five-second WebAudio window for audible underruns."""
    failures: list[str] = []
    if not isinstance(audio, dict) or not audio.get("installed"):
        failures.append(f"{description} Web Audio probe disappeared: {audio!r}")
        audio = {}
    contexts = audio.get("contexts") or []
    running_contexts = [
        context
        for context in contexts
        if context.get("state") == "running" and context.get("sampleRate", 0) > 0
    ]
    if not running_contexts:
        failures.append(
            f"{description} Web AudioContext is not running: {contexts!r}"
        )
    sample_rate = running_contexts[0]["sampleRate"] if running_contexts else 0
    queued = int(audio.get("windowQueuedBuffers", 0))
    ended = int(audio.get("windowEndedBuffers", 0))
    frames = int(audio.get("windowFramesQueued", 0))
    scheduled_seconds = float(audio.get("windowScheduledSeconds", 0.0))
    elapsed = float(audio.get("observedWindowElapsedMs", 0.0)) / 1000.0
    cadence_ratio = scheduled_seconds / elapsed if elapsed > 0 else 0.0
    positive_gaps = int(audio.get("windowPositiveGapCount", 0))
    maximum_gap_ms = float(audio.get("windowMaxPositiveGapMs", 0.0))
    if allow_paused and queued == 0:
        if ended != 0 or frames != 0 or scheduled_seconds != 0.0:
            failures.append(
                f"{description} Web Audio pause is partial: "
                f"queued={queued}, ended={ended}, frames={frames}, "
                f"scheduled={scheduled_seconds:.3f}s"
            )
        if positive_gaps != 0 or maximum_gap_ms != 0.0:
            failures.append(
                f"{description} Web Audio pause recorded scheduler gaps: "
                f"{positive_gaps}, max {maximum_gap_ms:.3f} ms"
            )
        return (
            {
                "mode": "paused",
                "sample_rate": sample_rate,
                "observation_elapsed_seconds": elapsed,
                "queued_buffers": 0,
                "ended_buffers": ended,
                "queued_frames": 0,
                "scheduled_seconds": 0.0,
                "scheduled_to_wall_ratio": 0.0,
                "buffer_frames_min": 0,
                "buffer_frames_max": 0,
                "positive_gap_count": positive_gaps,
                "maximum_positive_gap_ms": maximum_gap_ms,
                "maximum_queue_interval_ms": audio.get(
                    "windowMaxQueueIntervalMs"
                ),
            },
            failures,
        )
    minimum_buffers = max(100, round(elapsed * 95))
    if queued < minimum_buffers:
        failures.append(
            f"{description} Web Audio queued only {queued} buffers "
            f"over {elapsed:.2f}s (need {minimum_buffers})"
        )
    if not 0.98 <= cadence_ratio <= 1.02:
        failures.append(
            f"{description} Web Audio scheduled duration is inconsistent "
            f"with wall time: {scheduled_seconds:.3f}s over {elapsed:.3f}s"
        )
    if ended < queued * 0.90:
        failures.append(
            f"{description} Web Audio ended only {ended}/{queued} "
            "scheduled buffers"
        )
    buffer_min = int(audio.get("windowBufferFramesMin") or 0)
    buffer_max = int(audio.get("windowBufferFramesMax") or 0)
    minimum_expected = sample_rate * 0.008
    maximum_expected = sample_rate * 0.012
    if not (
        minimum_expected <= buffer_min <= maximum_expected
        and minimum_expected <= buffer_max <= maximum_expected
    ):
        failures.append(
            f"{description} Web Audio buffer cadence is not the pinned "
            f"10 ms block size: {buffer_min}..{buffer_max} frames at "
            f"{sample_rate} Hz"
        )
    if positive_gaps != 0:
        failures.append(
            f"{description} Web Audio scheduling is not gap-free: "
            f"{positive_gaps}/{queued}, max {maximum_gap_ms:.3f} ms"
        )
    return (
        {
            "mode": "continuous",
            "sample_rate": sample_rate,
            "observation_elapsed_seconds": elapsed,
            "queued_buffers": queued,
            "ended_buffers": ended,
            "queued_frames": frames,
            "scheduled_seconds": scheduled_seconds,
            "scheduled_to_wall_ratio": cadence_ratio,
            "buffer_frames_min": buffer_min,
            "buffer_frames_max": buffer_max,
            "positive_gap_count": positive_gaps,
            "maximum_positive_gap_ms": maximum_gap_ms,
            "maximum_queue_interval_ms": audio.get("windowMaxQueueIntervalMs"),
        },
        failures,
    )


def capture_strict_audio_window(
    driver: WebDriver, description: str, *, allow_paused: bool = False
) -> tuple[dict[str, Any], list[str]]:
    probe = driver.execute(
        """
        return typeof window.__jcResetAudioProbe === 'function'
          ? window.__jcResetAudioProbe() : null;
        """
    )
    if not isinstance(probe, dict) or not probe.get("installed"):
        return {}, [f"{description} Web Audio probe is unavailable: {probe!r}"]
    time.sleep(5.0)
    snapshot = driver.execute(
        """
        if (!window.__jcAudioProbe) return null;
        const copy = JSON.parse(JSON.stringify(window.__jcAudioProbe));
        copy.observedWindowElapsedMs =
          performance.now() - window.__jcAudioProbe.windowStartedAtMs;
        return copy;
        """
    )
    return evaluate_strict_audio_window(
        snapshot, description, allow_paused=allow_paused
    )


LATE_ENDING_CLOCK_REGION = (0.38, 0.17, 0.56, 0.43)
LATE_ENDING_LOWER_BAND_TOP = 0.72


def late_ending_color_metrics(
    frame: tuple[int, int, int, bytes]
) -> dict[str, float]:
    """Measure the authentic THEEND signature and stale-clock/key regions."""
    width, height, channels, pixels = frame
    total = width * height
    if total <= 0 or channels not in (3, 4) or len(pixels) != total * channels:
        raise SmokeFailure("late-ending frame has invalid pixel geometry")

    clock_left, clock_top, clock_right, clock_bottom = LATE_ENDING_CLOCK_REGION
    clock_x_start = round(clock_left * width)
    clock_x_end = round(clock_right * width)
    clock_y_start = round(clock_top * height)
    clock_y_end = round(clock_bottom * height)
    lower_y_start = round(LATE_ENDING_LOWER_BAND_TOP * height)
    clock_total = max(
        1, (clock_x_end - clock_x_start) * (clock_y_end - clock_y_start)
    )
    lower_total = max(1, width * (height - lower_y_start))

    black = bright_red = meaningful = clock_green = lower_color_key = 0
    for y_position in range(height):
        row_start = y_position * width * channels
        for x_position in range(width):
            pixel = row_start + x_position * channels
            red, green, blue = pixels[pixel : pixel + 3]
            opaque = channels == 3 or pixels[pixel + 3] > 16
            is_color_key = (
                opaque
                and min(red, blue) >= 144
                and green <= 48
                and abs(red - blue) <= 24
                and min(red, blue) - green >= 112
            )
            is_non_black = opaque and max(red, green, blue) > 12
            black += int(not opaque or max(red, green, blue) <= 20)
            bright_red += int(
                opaque and red >= 192 and green <= 80 and blue <= 80
            )
            meaningful += int(is_non_black and not is_color_key)
            if (
                clock_x_start <= x_position < clock_x_end
                and clock_y_start <= y_position < clock_y_end
            ):
                clock_green += int(
                    opaque
                    and green >= 96
                    and green >= red * 1.20
                    and green >= blue * 1.20
                )
            if y_position >= lower_y_start:
                lower_color_key += int(is_color_key)

    return {
        "black_ratio": black / total,
        "bright_red_ratio": bright_red / total,
        "meaningful_ratio": meaningful / total,
        "clock_green_ratio": clock_green / clock_total,
        "lower_color_key_ratio": lower_color_key / lower_total,
    }


def late_ending_signature_failures(metrics: dict[str, float]) -> list[str]:
    failures: list[str] = []
    if metrics["meaningful_ratio"] < 0.05:
        failures.append("blank or lost canvas")
    if metrics["black_ratio"] < 0.70:
        failures.append("ending is not black-backed")
    if metrics["bright_red_ratio"] < 0.05:
        failures.append("ending lacks the bright-red title signature")
    if metrics["clock_green_ratio"] >= 0.012:
        failures.append("frog clock/saved overlay persists over the ending")
    if metrics["lower_color_key_ratio"] >= 0.02:
        failures.append("lower band leaks purple/color-key pixels")
    return failures


def analyze_late_ending_frames(
    paths: list[pathlib.Path], hashes: list[str]
) -> dict[str, Any]:
    frames = [png_pixels(path) for path in paths]
    return analyze_late_ending_decoded_frames(
        frames, [path.name for path in paths], hashes
    )


def analyze_late_ending_decoded_frames(
    frames: list[tuple[int, int, int, bytes]],
    names: list[str],
    hashes: list[str],
) -> dict[str, Any]:
    if len(frames) < 4 or len(frames) != len(names) or len(frames) != len(hashes):
        raise SmokeFailure("late-ending acceptance requires four captured frames")
    geometry = frames[0][:3]
    failures: list[str] = []
    geometry_consistent = all(frame[:3] == geometry for frame in frames[1:])
    if not geometry_consistent:
        failures.append("late-ending canvas geometry changed or disappeared")

    qualities = [frame_quality(frame) for frame in frames]
    metrics = [late_ending_color_metrics(frame) for frame in frames]
    for name, quality, color_metrics in zip(names, qualities, metrics):
        for failure in late_ending_signature_failures(color_metrics):
            failures.append(f"{name}: {failure}")
        if frame_has_color_key_failure(quality):
            failures.append(
                f"{name}: renderer color-key leak "
                f"({quality['renderer_key_component_pixels']} px/"
                f"{quality['renderer_key_component_width']}x"
                f"{quality['renderer_key_component_height']})"
            )

    full_change_ratios: list[float] = []
    clock_change_ratios: list[float] = []
    if geometry_consistent:
        full_change_ratios = [
            region_change_ratio(frames[index - 1], frames[index], (0, 0, 1, 1))
            for index in range(1, len(frames))
        ]
        clock_change_ratios = [
            region_change_ratio(
                frames[index - 1], frames[index], LATE_ENDING_CLOCK_REGION
            )
            for index in range(1, len(frames))
        ]
        if max(full_change_ratios, default=1.0) > 0.02:
            failures.append("late-ending frames are not stable across the full canvas")
        if max(clock_change_ratios, default=1.0) > 0.02:
            failures.append("late-ending clock region is not stable")

    evidence = {
        "frame_sha256": {
            name: digest for name, digest in zip(names, hashes)
        },
        "frame_quality": {
            name: quality for name, quality in zip(names, qualities)
        },
        "color_metrics": {
            name: color_metrics for name, color_metrics in zip(names, metrics)
        },
        "clock_region": LATE_ENDING_CLOCK_REGION,
        "lower_band_top": LATE_ENDING_LOWER_BAND_TOP,
        "full_frame_change_ratios": full_change_ratios,
        "clock_region_change_ratios": clock_change_ratios,
        "acceptance_failures": failures,
        "passed": not failures,
    }
    if failures:
        raise SmokeFailure("; ".join(failures))
    return evidence


def capture_late_ending(
    driver: WebDriver,
    canvas_element: str,
    artifacts: pathlib.Path,
    timeout: float,
) -> tuple[list[pathlib.Path], list[str], dict[str, Any]]:
    """Wait for Johnny1's THEEND replacement, then prove it stays clean."""
    paths = [artifacts / f"late-ending-{index:02d}.png" for index in range(4)]
    hashes: list[str] = []
    deadline = time.monotonic() + timeout
    started = time.monotonic()
    polls = 0
    last_metrics: dict[str, float] | None = None
    while time.monotonic() < deadline:
        candidate_hash = driver.screenshot(canvas_element, paths[0])
        polls += 1
        last_metrics = late_ending_color_metrics(png_pixels(paths[0]))
        if not late_ending_signature_failures(last_metrics):
            hashes.append(candidate_hash)
            break
        time.sleep(0.5)
    if not hashes:
        raise SmokeFailure(
            "timed out waiting for clean Johnny1 The End replacement; "
            f"last metrics: {last_metrics!r}"
        )

    for path in paths[1:]:
        time.sleep(1.0)
        hashes.append(driver.screenshot(canvas_element, path))
    evidence = analyze_late_ending_frames(paths, hashes)
    evidence["wait_elapsed_seconds"] = time.monotonic() - started
    evidence["poll_count"] = polls
    return paths, hashes, evidence


def capture_temporal_gameplay(
    driver: WebDriver,
    canvas_element: str,
    artifacts: pathlib.Path,
    evidence: dict[str, Any],
    *,
    require_playfield_motion: bool,
    require_water_motion: bool,
    test_early_chapter_motion: bool = False,
    timeout: float = 45.0,
) -> tuple[list[pathlib.Path], list[str], dict[str, Any]]:
    """Capture settled gameplay and require authentic playfield/water motion."""
    # RetroArch's content and configuration notifications obscure the center
    # and bottom of the first frame. Let both expire before evidence capture.
    settle_seconds = EARLY_CHAPTER_NOTIFICATION_SETTLE_SECONDS
    time.sleep(settle_seconds)
    capture_interval_seconds = 1.0
    reset_state: dict[str, Any] | None = None
    if test_early_chapter_motion:
        driver.click(driver.find("#reset"))
        reset_state = wait_until(
            "clean Running state after fixed chapter reset",
            timeout,
            lambda: (
                current
                if diagnostics_are_clean_running(
                    current := diagnostics(driver)
                )
                else None
            ),
        )
        time.sleep(EARLY_CHAPTER_FIRST_CAPTURE_DELAY_SECONDS)
        capture_interval_seconds = EARLY_CHAPTER_CAPTURE_INTERVAL_SECONDS
    paths: list[pathlib.Path] = []
    hashes: list[str] = []
    started = time.monotonic()
    for index in range(5):
        name = "game.png" if index == 0 else f"game-{index:02d}.png"
        path = artifacts / name
        paths.append(path)
        hashes.append(driver.screenshot(canvas_element, path))
        if index != 4:
            time.sleep(capture_interval_seconds)
    elapsed = time.monotonic() - started

    # Screenshot readback can stall the browser's main thread. Measure audio
    # independently during an idle interval, then freeze a deep copy before
    # the comparatively expensive PNG decoding below.
    probe = driver.execute(
        """
        return {
          audio: typeof window.__jcResetAudioProbe === 'function'
            ? window.__jcResetAudioProbe() : null,
          webgl: typeof window.__jcResetWebGLProbe === 'function'
            ? window.__jcResetWebGLProbe() : null,
        };
        """
    )
    audio_probe = probe.get("audio") if isinstance(probe, dict) else None
    if not isinstance(audio_probe, dict) or not audio_probe.get("installed"):
        raise SmokeFailure(f"Web Audio scheduling probe is unavailable: {probe!r}")
    time.sleep(5.0)
    probe_snapshot = driver.execute(
        """
        const snapshot = probe => {
          if (!probe) return null;
          const copy = JSON.parse(JSON.stringify(probe));
          copy.observedWindowElapsedMs = performance.now() - probe.windowStartedAtMs;
          return copy;
        };
        return {
          audio: snapshot(window.__jcAudioProbe),
          webgl: snapshot(window.__jcWebGLProbe),
        };
        """
    )
    audio = (
        probe_snapshot.get("audio") if isinstance(probe_snapshot, dict) else None
    )
    webgl = (
        probe_snapshot.get("webgl") if isinstance(probe_snapshot, dict) else None
    )

    decoded_frames = [png_pixels(path) for path in paths]
    geometry = decoded_frames[0][:3]
    failures: list[str] = []
    geometry_changed = any(
        frame[:3] != geometry for frame in decoded_frames[1:]
    )
    if geometry_changed:
        failures.append("gameplay canvas geometry changed or disappeared")
    frame_qualities = [frame_quality(frame) for frame in decoded_frames]
    for path, quality in zip(paths, frame_qualities):
        if frame_has_color_key_failure(quality):
            failures.append(
                f"settled gameplay frame leaks renderer color-key magenta: "
                f"{path.name} ({quality['magenta_ratio']:.3%} broad key, "
                f"{quality['renderer_key_ratio']:.3%} sentinel, largest "
                f"component {quality['renderer_key_component_pixels']} px/"
                f"{quality['renderer_key_component_width']}x"
                f"{quality['renderer_key_component_height']}, rectangle "
                f"{quality['renderer_key_rect_width']}x"
                f"{quality['renderer_key_rect_height']})"
            )
        if quality["meaningful_ratio"] < 0.05:
            failures.append(
                f"settled gameplay frame is blank or lost: {path.name} "
                f"({quality['meaningful_ratio']:.3%} meaningful pixels)"
            )

    playfield_region = (0.05, 0.14, 0.95, 0.84)
    water_region = (0.05, 0.56, 0.95, 0.82)
    playfield_ratios = [] if geometry_changed else [
        region_change_ratio(
            decoded_frames[index - 1], decoded_frames[index], playfield_region
        )
        for index in range(1, len(decoded_frames))
    ]
    water_ratios = [] if geometry_changed else [
        region_change_ratio(
            decoded_frames[index - 1], decoded_frames[index], water_region
        )
        for index in range(1, len(decoded_frames))
    ]
    actor_ratios = (
        []
        if geometry_changed or not test_early_chapter_motion
        else [
            region_change_ratio(
                decoded_frames[index - 1],
                decoded_frames[index],
                EARLY_CHAPTER_ACTOR_REGION,
            )
            for index in range(1, len(decoded_frames))
        ]
    )
    distinct_frames = len(set(hashes))
    if distinct_frames < 3:
        failures.append(
            f"settled gameplay produced only {distinct_frames} distinct frames"
        )
    if require_playfield_motion and max(playfield_ratios, default=0.0) < 0.0005:
        failures.append(
            "settled gameplay lacks a material playfield pixel transition"
        )
    if require_water_motion and max(water_ratios, default=0.0) < 0.0002:
        failures.append(
            "settled gameplay lacks a material lower water-band transition"
        )
    if (
        test_early_chapter_motion
        and max(actor_ratios, default=0.0)
        < EARLY_CHAPTER_MINIMUM_ACTOR_MOTION_RATIO
    ):
        failures.append(
            "early fixed chapter lacks a material actor-region pixel transition"
        )

    audio_metrics, audio_failures = evaluate_strict_audio_window(
        audio, "gameplay"
    )
    failures.extend(audio_failures)

    if not isinstance(webgl, dict) or not webgl.get("installed"):
        failures.append(f"WebGL diagnostic probe disappeared: {webgl!r}")
        webgl = {}
    webgl_elapsed = float(webgl.get("observedWindowElapsedMs", 0.0)) / 1000.0
    contexts_created = int(webgl.get("contextsCreated", 0))
    texture_uploads = int(webgl.get("windowTexImage2DCalls", 0)) + int(
        webgl.get("windowTexSubImage2DCalls", 0)
    )
    typed_uploads = int(webgl.get("windowTypedArrayUploads", 0))
    video_uploads = int(webgl.get("windowVideoUploadCandidates", 0))
    draw_calls = int(webgl.get("windowDrawArraysCalls", 0)) + int(
        webgl.get("windowDrawElementsCalls", 0)
    )
    context_losses = int(webgl.get("contextLostEvents", 0))
    if contexts_created < 1:
        failures.append("WebGL probe did not observe a rendering context")
    if texture_uploads < 1 or video_uploads < 1:
        failures.append(
            "WebGL probe observed no typed video-sized texture upload"
        )
    if draw_calls < 1:
        failures.append("WebGL probe observed no draw call")
    if context_losses:
        failures.append(f"WebGL context was lost {context_losses} time(s)")

    rolling_signature = int(webgl.get("windowRollingUploadSignature", 0))
    webgl_metrics = {
        "observation_elapsed_seconds": webgl_elapsed,
        "contexts_created": contexts_created,
        "tex_image_2d_calls": int(webgl.get("windowTexImage2DCalls", 0)),
        "tex_sub_image_2d_calls": int(
            webgl.get("windowTexSubImage2DCalls", 0)
        ),
        "typed_array_uploads": typed_uploads,
        "video_upload_candidates": video_uploads,
        "distinct_sampled_video_uploads": int(
            webgl.get("windowDistinctSampledVideoUploads", 0)
        ),
        "sampled_upload_bytes": int(webgl.get("windowSampledUploadBytes", 0)),
        "rolling_upload_signature": f"{rolling_signature:08x}",
        "signature_kind": webgl.get("signatureKind"),
        "draw_arrays_calls": int(webgl.get("windowDrawArraysCalls", 0)),
        "draw_elements_calls": int(webgl.get("windowDrawElementsCalls", 0)),
        "clear_calls": int(webgl.get("windowClearCalls", 0)),
        "context_lost_events": context_losses,
        "context_restored_events": int(webgl.get("contextRestoredEvents", 0)),
    }

    temporal = {
        "settle_seconds": settle_seconds,
        "sample_elapsed_seconds": elapsed,
        "distinct_frames": distinct_frames,
        "frame_sha256": {
            path.name: digest for path, digest in zip(paths, hashes)
        },
        "frame_quality": {
            path.name: quality for path, quality in zip(paths, frame_qualities)
        },
        "required_playfield_motion": require_playfield_motion,
        "required_water_motion": require_water_motion,
        "playfield_region": playfield_region,
        "water_region": water_region,
        "playfield_change_ratios": playfield_ratios,
        "water_change_ratios": water_ratios,
        "audio": audio_metrics,
        "webgl": webgl_metrics,
        "acceptance_failures": failures,
        "passed": not failures,
    }
    if test_early_chapter_motion:
        temporal["early_chapter_motion"] = {
            "reset_before_capture": True,
            "reset_status": reset_state.get("status") if reset_state else None,
            "reset_status_error": (
                reset_state.get("statusError") if reset_state else None
            ),
            "reset_page_error_count": len(
                reset_state.get("pageErrors", []) if reset_state else []
            ),
            "reset_rejection_count": len(
                reset_state.get("rejections", []) if reset_state else []
            ),
            "first_capture_delay_seconds": (
                EARLY_CHAPTER_FIRST_CAPTURE_DELAY_SECONDS
            ),
            "capture_interval_seconds": capture_interval_seconds,
            "actor_region": EARLY_CHAPTER_ACTOR_REGION,
            "actor_change_ratios": actor_ratios,
            "minimum_actor_motion_ratio": (
                EARLY_CHAPTER_MINIMUM_ACTOR_MOTION_RATIO
            ),
        }
    evidence["temporal_gameplay"] = temporal
    if failures:
        raise SmokeFailure("; ".join(failures))
    return paths, hashes, temporal


def run_smoke(args: argparse.Namespace, firefox: str, geckodriver: str) -> int:
    dist = args.dist.resolve()
    artifacts = args.artifacts.resolve()
    artifacts.mkdir(parents=True, exist_ok=True)
    required = [
        dist / "index.html",
        dist / "johnny_castaway_libretro.js",
        dist / "johnny_castaway_libretro.wasm",
    ]
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise SmokeFailure("generated Web distribution is incomplete: " + ", ".join(missing))
    validate_scene_visual_only_args(args)
    validate_late_ending_args(args)
    validate_early_chapter_motion_args(args)
    if args.chapter and args.content_dir is None:
        raise SmokeFailure("--chapter requires --content-dir")
    if args.test_audio_unlock and not args.staged_local_content:
        raise SmokeFailure("--test-audio-unlock requires --staged-local-content")
    if args.chapter:
        chapter_source = (ROOT / "src/jc_chapters.c").read_text(encoding="utf-8")
        chapter_marker = f'CHAPTER("{args.chapter}",'
        if chapter_marker not in chapter_source:
            raise SmokeFailure(
                f"--chapter value is not in src/jc_chapters.c: {args.chapter}"
            )

    if args.staged_local_content:
        staged_files = validate_staged_local_content(dist)
        local_content = dist / "local-content"
        map_path = local_content / "RESOURCE.MAP"
        archive_path = local_content / "RESOURCE.001"
        expected_index_log = "Johnny Castaway: indexed "
        core_options = None
        content_description = "server-local user-owned data"
    else:
        subprocess.run(
            [sys.executable, str(ROOT / "tools/check_web_dist.py"), str(dist)],
            cwd=ROOT,
            check=True,
        )
    if not args.staged_local_content and args.content_dir is None:
        map_path, archive_path = prepare_synthetic_content(artifacts / "synthetic")
        expected_index_log = "indexed 5 resources"
        core_options = 'johnny_castaway_chapter = "fishing1"\n'
        content_description = "checksum-verified synthetic fixture"
    elif not args.staged_local_content:
        content_directory = args.content_dir.resolve()
        map_path = content_directory / "RESOURCE.MAP"
        archive_path = content_directory / "RESOURCE.001"
        missing_content = [
            str(path) for path in (map_path, archive_path) if not path.is_file()
        ]
        if missing_content:
            raise SmokeFailure(
                "user-owned content directory is incomplete: "
                + ", ".join(missing_content)
            )
        expected_index_log = "Johnny Castaway: indexed "
        core_options = (
            f'johnny_castaway_chapter = "{args.chapter}"\n'
            if args.chapter
            else None
        )
        content_description = "user-owned local data"
        if args.chapter:
            content_description += f" (fixed {args.chapter} chapter)"
    server, url = start_web_server(dist)
    url += "?smoke=1"
    port = available_port()
    gecko_log = artifacts / "geckodriver.log"
    gecko_environment = os.environ.copy()
    gecko_environment.setdefault("LIBGL_ALWAYS_SOFTWARE", "1")
    gecko = geckodriver
    gecko_log_stream = gecko_log.open("wb")
    gecko_process = subprocess.Popen(
        [gecko, "--host", "127.0.0.1", "--port", str(port), "--log", "debug"],
        cwd=ROOT,
        env=gecko_environment,
        stdout=gecko_log_stream,
        stderr=subprocess.STDOUT,
    )
    driver = WebDriver(f"http://127.0.0.1:{port}")
    result: dict[str, Any] = {
        "url": url,
        "content": content_description,
        "smoke_probe_enabled": True,
        "chapter": args.chapter or "automatic",
        "autoplay_blocked": args.test_audio_unlock,
        "scene_visual_only": args.scene_visual_only,
        "test_late_ending": args.test_late_ending,
    }
    if args.test_early_chapter_motion:
        result["test_early_chapter_motion"] = True
    if args.staged_local_content:
        result["staged_local_content"] = [path.name for path in staged_files]
    elif args.content_dir is None:
        result["synthetic_content"] = [
            str(map_path.relative_to(ROOT)),
            str(archive_path.relative_to(ROOT)),
        ]
    try:
        wait_for_port(gecko_process, port, 10)
        driver.new_session(
            firefox,
            headless=not bool(os.environ.get("DISPLAY")),
            block_autoplay=args.test_audio_unlock,
        )
        driver.navigate(url)
        driver.execute(
            """
            window.__jcSmoke = {pageErrors: [], rejections: [], consoleErrors: []};
            window.addEventListener('error', event => {
              window.__jcSmoke.pageErrors.push(String(event.message || event.error));
            });
            window.addEventListener('unhandledrejection', event => {
              window.__jcSmoke.rejections.push(String(event.reason));
            });
            const originalError = console.error.bind(console);
            console.error = (...values) => {
              window.__jcSmoke.consoleErrors.push(values.map(String).join(' '));
              originalError(...values);
            };
            """
        )

        if not args.staged_local_content:
            file_input = driver.find("#content-files")
            driver.upload(file_input, [map_path, archive_path])
            wait_until(
                "both content files to be accepted",
                args.timeout,
                lambda: diagnostics(driver).get("status", "").startswith("Ready:"),
            )
            driver.execute(
                """
                if (typeof window.__jcStartForSmoke !== "function") {
                  throw new Error("smoke-only player start hook is unavailable");
                }
                window.__jcStartForSmoke(undefined, arguments[0]);
                return true;
                """,
                core_options,
            )
        state = wait_until(
            "RetroArch startup",
            args.timeout,
            lambda: (
                current
                if (
                    (current := diagnostics(driver)).get("statusError")
                    or (
                        current.get("status", "").startswith("Running.")
                        and any(
                            expected_index_log in line
                            for line in current.get("consoleErrors", [])
                        )
                    )
                )
                else None
            ),
        )
        result["startup"] = state
        if state.get("statusError"):
            raise SmokeFailure(state.get("status", "browser reported a startup error"))
        canvas = state.get("canvas") or {}
        if canvas.get("width", 0) < 640 or canvas.get("height", 0) < 480:
            raise SmokeFailure(f"unexpected canvas geometry: {canvas}")
        if not state.get("menuEnabled"):
            raise SmokeFailure("RetroArch menu button remained disabled")
        if state.get("pageErrors") or state.get("rejections"):
            raise SmokeFailure(
                f"page errors: {state.get('pageErrors')}; rejections: {state.get('rejections')}"
            )
        joined_console = "\n".join(state.get("consoleErrors", []))
        for expected in (
            "SET_CORE_OPTIONS_V2",
            expected_index_log,
            "Geometry: 640x480",
        ):
            if expected not in joined_console:
                raise SmokeFailure(f"RetroArch log did not contain {expected!r}")
        if (
            args.staged_local_content
            and "optional sound effects: 23 loaded" not in joined_console
        ):
            raise SmokeFailure(
                "server-local auto-load did not load all 23 optional sound effects"
            )
        fatal_messages = (
            "[EMSCRIPTEN/WebGL] Failed",
            "[libretro ERROR]",
            "Failed to load content",
            "Aborted(",
            "cannot access property \"GLctx\"",
        )
        for fatal_message in fatal_messages:
            if fatal_message in joined_console:
                raise SmokeFailure(
                    f"RetroArch log contains fatal error: {fatal_message}"
                )

        canvas_element = driver.find("#canvas")
        if args.test_audio_unlock:
            blocked_state = wait_until(
                "browser-blocked Web AudioContext",
                args.timeout,
                lambda: (
                    current
                    if (
                        (current := diagnostics(driver))
                        .get("audioGate", {})
                        .get("state")
                        == "locked"
                        and current.get("audioGate", {}).get("unlockEnabled")
                        and current.get("audioGate", {}).get("contextCount", 0) > 0
                    )
                    else None
                ),
            )
            driver.execute(
                """
                if (typeof window.__jcResetWebGLProbe === 'function') {
                  window.__jcResetWebGLProbe();
                }
                return true;
                """
            )
            blocked_first_path = artifacts / "audio-blocked-01.png"
            blocked_second_path = artifacts / "audio-blocked-02.png"
            blocked_first_hash = driver.screenshot(
                canvas_element, blocked_first_path
            )
            time.sleep(1.5)
            blocked_second_hash = driver.screenshot(
                canvas_element, blocked_second_path
            )
            blocked_after_visuals = diagnostics(driver)
            blocked_context = blocked_after_visuals.get("audioGate") or {}
            if blocked_context.get("state") != "locked":
                raise SmokeFailure(
                    "Web AudioContext did not remain suspended before interaction"
                )
            if blocked_first_hash == blocked_second_hash:
                raise SmokeFailure(
                    "canvas did not change while Web Audio autoplay was blocked"
                )
            blocked_webgl = blocked_after_visuals.get("webglProbe") or {}
            if int(blocked_webgl.get("windowVideoUploadCandidates", 0)) < 1:
                raise SmokeFailure(
                    "no video-sized WebGL upload occurred while audio was suspended"
                )
            if int(blocked_webgl.get("windowDrawArraysCalls", 0)) < 1:
                raise SmokeFailure(
                    "no WebGL draw occurred while audio was suspended"
                )
            result["audio_unlock"] = {
                "blocked_state": blocked_state.get("audioGate"),
                "blocked_context": blocked_context,
                "visuals_while_suspended": {
                    "first_sha256": blocked_first_hash,
                    "second_sha256": blocked_second_hash,
                    "video_uploads": int(
                        blocked_webgl.get("windowVideoUploadCandidates", 0)
                    ),
                    "draw_calls": int(
                        blocked_webgl.get("windowDrawArraysCalls", 0)
                    ),
                },
            }
            driver.click(driver.find("#audio-unlock-button"))
            unlocked_state = wait_until(
                "Web AudioContext to resume after Enable Audio",
                args.timeout,
                lambda: (
                    current
                    if (
                        (current := diagnostics(driver))
                        .get("audioGate", {})
                        .get("state")
                        == "running"
                        and current.get("audioGate", {}).get("contextCount", 0) > 0
                    )
                    else None
                ),
            )
            result["audio_unlock"]["unlocked_state"] = unlocked_state.get(
                "audioGate"
            )
            driver.click(driver.find("#reset"))
            reset_state = wait_until(
                "audio context to remain running after RetroArch reset",
                args.timeout,
                lambda: (
                    current
                    if (
                        (current := diagnostics(driver))
                        .get("audioGate", {})
                        .get("state")
                        == "running"
                        and current.get("audioGate", {}).get("contextCount") == 1
                        and current.get("status", "").startswith("Running.")
                    )
                    else None
                ),
            )
            result["audio_unlock"]["post_reset_state"] = reset_state.get(
                "audioGate"
            )
            result["audio_unlock"]["passed"] = True
        authentic_content = args.content_dir is not None or args.staged_local_content
        if authentic_content:
            game_paths, game_hashes, temporal = capture_temporal_gameplay(
                driver,
                canvas_element,
                artifacts,
                result,
                require_playfield_motion=bool(args.chapter),
                require_water_motion=not bool(args.chapter),
                test_early_chapter_motion=args.test_early_chapter_motion,
                timeout=args.timeout,
            )
            result["temporal_gameplay"] = temporal
        else:
            time.sleep(6.0)
            game_paths = [artifacts / "game.png"]
            game_hashes = [driver.screenshot(canvas_element, game_paths[0])]
        if args.test_late_ending:
            _, _, late_ending = capture_late_ending(
                driver, canvas_element, artifacts, args.timeout
            )
            result["late_ending"] = late_ending
        game_hash = game_hashes[0]
        if args.scene_visual_only:
            final_state = diagnostics(driver)
            if final_state.get("pageErrors") or final_state.get("rejections"):
                raise SmokeFailure(
                    "post-gameplay page errors: "
                    f"{final_state.get('pageErrors')}; "
                    f"rejections: {final_state.get('rejections')}"
                )
            final_console = "\n".join(final_state.get("consoleErrors", []))
            for fatal_message in fatal_messages:
                if fatal_message in final_console:
                    raise SmokeFailure(
                        "post-gameplay RetroArch log contains fatal error: "
                        f"{fatal_message}"
                    )
            complete_scene_visual_only_result(
                result, game_paths, game_hashes, final_state
            )
            (artifacts / "result.json").write_text(
                json.dumps(result, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            print(
                "PASS: strict scene visual/audio/WebGL gate passed for "
                f"{args.chapter}"
            )
            print(f"Artifacts: {artifacts.relative_to(ROOT)}")
            return 0
        driver.click(driver.find("#menu"))
        menu_hash = wait_until(
            "RetroArch menu to change the canvas",
            10,
            lambda: (
                current
                if (current := driver.screenshot(canvas_element, artifacts / "menu.png"))
                != game_hash
                else None
            ),
        )
        menu_hash = stable_screenshot(
            driver, canvas_element, artifacts / "menu.png"
        )
        if menu_hash == game_hash:
            raise SmokeFailure("RetroArch menu returned to the gameplay frame")

        # Quick Menu ordering is stable for loaded content. Home makes the
        # starting point explicit, then four Down presses select Core Options:
        # Resume, Reset, Close Content, Save States, Core Options.
        driver.key_press(WEBDRIVER_KEY_HOME)
        for _ in range(4):
            driver.key_press(WEBDRIVER_KEY_DOWN)
        core_row_path = artifacts / "core-options-selected.png"
        core_row_hash = stable_screenshot(
            driver,
            canvas_element,
            core_row_path,
        )
        visual_changes = {
            "quick_menu_to_core_options_row": assert_content_changed(
                artifacts / "menu.png",
                core_row_path,
                "keyboard navigation to Core Options",
            )
        }

        driver.key_press(RETROARCH_MENU_OK)
        core_options_path = artifacts / "core-options.png"
        core_options_hash = stable_screenshot(
            driver, canvas_element, core_options_path
        )
        visual_changes["core_options_row_to_page"] = assert_content_changed(
            core_row_path,
            core_options_path,
            "Core Options page navigation",
        )

        # Web RetroArch defaults game_specific_options=true, so its override
        # manager precedes the categories. Story is the first core-supplied
        # category after that frontend-owned row.
        driver.key_press(WEBDRIVER_KEY_HOME)
        driver.key_press(WEBDRIVER_KEY_DOWN)
        driver.key_press(RETROARCH_MENU_OK)
        story_top_path = artifacts / "story-options-top.png"
        story_top_hash = stable_screenshot(
            driver, canvas_element, story_top_path
        )
        visual_changes["core_options_to_story"] = assert_content_changed(
            core_options_path,
            story_top_path,
            "Story core-option category navigation",
        )

        simulated_hashes: dict[str, str] = {}
        automatic_story_content = authentic_content and not args.chapter
        if automatic_story_content:
            # Automatic Story category ordering is declared by the core:
            # Playback/Chapter, Holiday, Seed, Calendar. Select Calendar,
            # choose its second value (Simulated), then capture the options
            # that become visible only in deterministic calendar mode.
            driver.key_press(WEBDRIVER_KEY_HOME)
            for _ in range(3):
                driver.key_press(WEBDRIVER_KEY_DOWN)
            driver.key_press(RETROARCH_MENU_OK)
            calendar_path = artifacts / "story-calendar-values.png"
            calendar_hash = stable_screenshot(
                driver,
                canvas_element,
                calendar_path,
            )
            driver.key_press(WEBDRIVER_KEY_HOME)
            driver.key_press(WEBDRIVER_KEY_DOWN)
            driver.key_press(RETROARCH_MENU_OK)
            simulated_top_path = artifacts / "story-simulated-top.png"
            simulated_top_hash = stable_screenshot(
                driver,
                canvas_element,
                simulated_top_path,
            )
            visual_changes["story_to_calendar_values"] = assert_content_changed(
                story_top_path,
                calendar_path,
                "Story Calendar value navigation",
            )
            visual_changes["calendar_values_to_simulated_story"] = (
                assert_content_changed(
                    calendar_path,
                    simulated_top_path,
                    "Simulated Calendar option expansion",
                )
            )
            simulated_hashes = {
                "story_calendar_values_sha256": calendar_hash,
                "story_simulated_top_sha256": simulated_top_hash,
            }

        driver.key_press(WEBDRIVER_KEY_END)
        story_bottom_path = artifacts / "story-options-bottom.png"
        story_bottom_hash = stable_screenshot(driver, canvas_element, story_bottom_path)
        visual_changes["story_top_to_last_entry"] = assert_content_changed(
            story_top_path,
            story_bottom_path,
            "Story option navigation to its last entry",
        )

        result["screenshots"] = {
            "game_sha256": game_hash,
            "gameplay_sequence_sha256": {
                path.name: digest for path, digest in zip(game_paths, game_hashes)
            },
            "menu_sha256": menu_hash,
            "core_options_selected_sha256": core_row_hash,
            "core_options_sha256": core_options_hash,
            "story_options_top_sha256": story_top_hash,
            "story_options_bottom_sha256": story_bottom_hash,
            **simulated_hashes,
        }
        result["menu_navigation"] = {
            "route": (
                "Quick Menu/Home/Down*4/Core Options/Home/Down*1/Story"
                + ("/Calendar/Simulated" if automatic_story_content else "")
                + "/End"
            ),
            "stable_frames": True,
            "visual_change_ratios": visual_changes,
            "automatic_story_controls": automatic_story_content,
        }
        if automatic_story_content:
            result["menu_navigation"]["expected_story_controls"] = [
                "Story Seed",
                "Story Calendar",
                "Simulated Month/Day/Time",
                "Playback Speed",
                "Tide",
                "Raft Stage",
            ]
        menu_audio, menu_audio_failures = capture_strict_audio_window(
            driver, "stable menu", allow_paused=True
        )
        result["menu_audio"] = {
            **menu_audio,
            "acceptance_failures": menu_audio_failures,
            "passed": not menu_audio_failures,
        }
        if menu_audio_failures:
            raise SmokeFailure("; ".join(menu_audio_failures))
        final_state = diagnostics(driver)
        if final_state.get("pageErrors") or final_state.get("rejections"):
            raise SmokeFailure(
                "post-navigation page errors: "
                f"{final_state.get('pageErrors')}; "
                f"rejections: {final_state.get('rejections')}"
            )
        final_console = "\n".join(final_state.get("consoleErrors", []))
        for fatal_message in fatal_messages:
            if fatal_message in final_console:
                raise SmokeFailure(
                    "post-navigation RetroArch log contains fatal error: "
                    f"{fatal_message}"
                )
        result["final"] = final_state
        result["passed"] = True
        (artifacts / "result.json").write_text(
            json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        print(f"PASS: Firefox loaded RetroArch with {content_description} at {url}")
        print(
            f"PASS: canvas is {canvas['width']}x{canvas['height']}; "
            "Core Options and Story category rendered"
        )
        print(f"Artifacts: {artifacts.relative_to(ROOT)}")
        return 0
    except Exception as error:
        with contextlib.suppress(Exception):
            result["final"] = diagnostics(driver)
        result["passed"] = False
        result["error"] = str(error)
        (artifacts / "result.json").write_text(
            json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        raise
    finally:
        driver.close()
        gecko_process.terminate()
        with contextlib.suppress(subprocess.TimeoutExpired):
            gecko_process.wait(timeout=5)
        if gecko_process.poll() is None:
            gecko_process.kill()
        gecko_log_stream.close()
        server.shutdown()
        server.server_close()


def main() -> int:
    args = parse_args()
    try:
        firefox, geckodriver, xvfb_run = browser_programs(args)
        rerun = rerun_under_xvfb(args, xvfb_run)
        if rerun is not None:
            return rerun
        return run_smoke(args, firefox, geckodriver)
    except (SmokeFailure, subprocess.CalledProcessError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
