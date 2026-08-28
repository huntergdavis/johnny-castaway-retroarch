#!/usr/bin/env python3
"""Exercise the generated RetroArch Web Player in a real Firefox browser.

This deliberately uses only Python's standard library plus the W3C WebDriver
HTTP protocol.  The content files come from tests/test_libretro.c's synthetic
fixture generator; original Johnny Castaway data is neither needed nor read.
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
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
from typing import Any


ROOT = pathlib.Path(__file__).resolve().parents[1]
WEBDRIVER_ELEMENT = "element-6066-11e4-a52e-4f735466cecf"
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

    def new_session(self, firefox_binary: str, headless: bool) -> None:
        arguments = ["-headless"] if headless else []
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
                            "prefs": {
                                "devtools.console.stdout.content": True,
                                "media.autoplay.default": 0,
                                "webgl.disabled": False,
                                "webgl.force-enabled": True,
                            },
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
    return parser.parse_args()


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
          pageErrors: smoke.pageErrors || [],
          rejections: smoke.rejections || [],
          consoleErrors: smoke.consoleErrors || [],
        };
        """
    )
    if not isinstance(value, dict):
        raise SmokeFailure("browser diagnostics were not an object")
    return value


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

    subprocess.run(
        [sys.executable, str(ROOT / "tools/check_web_dist.py"), str(dist)],
        cwd=ROOT,
        check=True,
    )
    map_path, archive_path = prepare_synthetic_content(artifacts / "synthetic")
    server, url = start_web_server(dist)
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
        "synthetic_content": [str(map_path.relative_to(ROOT)), str(archive_path.relative_to(ROOT))],
    }
    try:
        wait_for_port(gecko_process, port, 10)
        driver.new_session(firefox, headless=not bool(os.environ.get("DISPLAY")))
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

        file_input = driver.find("#content-files")
        driver.upload(file_input, [map_path, archive_path])
        wait_until(
            "both synthetic files to be accepted",
            args.timeout,
            lambda: diagnostics(driver).get("status", "").startswith("Ready:"),
        )
        driver.click(driver.find("#start"))
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
                            "indexed 5 resources" in line
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
        for expected in ("SET_CORE_OPTIONS_V2", "indexed 5 resources", "640x480"):
            if expected not in joined_console:
                raise SmokeFailure(f"RetroArch log did not contain {expected!r}")
        for fatal in (
            "[EMSCRIPTEN/WebGL] Failed",
            "Aborted(",
            "cannot access property \"GLctx\"",
        ):
            if fatal in joined_console:
                raise SmokeFailure(f"RetroArch log contains fatal error: {fatal}")

        canvas_element = driver.find("#canvas")
        game_hash = driver.screenshot(canvas_element, artifacts / "game.png")
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
        result["screenshots"] = {"game_sha256": game_hash, "menu_sha256": menu_hash}
        result["final"] = diagnostics(driver)
        result["passed"] = True
        (artifacts / "result.json").write_text(
            json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        print(f"PASS: Firefox loaded RetroArch and indexed 5 synthetic resources at {url}")
        print(f"PASS: canvas is {canvas['width']}x{canvas['height']} and menu screenshot changed")
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
