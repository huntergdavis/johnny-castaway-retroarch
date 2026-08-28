#!/usr/bin/env python3
"""Serve a generated RetroArch Web Player from localhost."""

from __future__ import annotations

import argparse
import functools
import http.server
import pathlib
import webbrowser


class WebPlayerHandler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        ".js": "text/javascript; charset=utf-8",
        ".wasm": "application/wasm",
    }

    def end_headers(self) -> None:
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Serve the Johnny Castaway RetroArch Web Player"
    )
    parser.add_argument("--directory", required=True, help="generated dist directory")
    parser.add_argument("--bind", default="127.0.0.1", help="listen address")
    parser.add_argument("--port", type=int, default=8080, help="listen port")
    parser.add_argument(
        "--open", action="store_true", help="open the local URL in a browser"
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    directory = pathlib.Path(args.directory).resolve()
    required = directory / "index.html"
    if not required.is_file():
        raise SystemExit(
            f"error: {required} is missing; run scripts/build-web-player.sh first"
        )

    handler = functools.partial(WebPlayerHandler, directory=str(directory))
    server = http.server.ThreadingHTTPServer((args.bind, args.port), handler)
    host_for_url = "127.0.0.1" if args.bind in {"0.0.0.0", "::"} else args.bind
    url = f"http://{host_for_url}:{server.server_port}/"
    print(f"Serving {directory}")
    print(f"Open {url}")
    print("Press Ctrl-C to stop.")
    if args.open:
        webbrowser.open(url)

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping server.")
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
