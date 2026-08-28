#!/usr/bin/env python3
"""Check that a shared libretro core exports exactly the public C ABI."""

from __future__ import annotations

import argparse
import pathlib
import re
import subprocess


REQUIRED_EXPORTS = {
    "retro_api_version",
    "retro_cheat_reset",
    "retro_cheat_set",
    "retro_deinit",
    "retro_get_memory_data",
    "retro_get_memory_size",
    "retro_get_region",
    "retro_get_system_av_info",
    "retro_get_system_info",
    "retro_init",
    "retro_load_game",
    "retro_load_game_special",
    "retro_reset",
    "retro_run",
    "retro_serialize",
    "retro_serialize_size",
    "retro_set_audio_sample",
    "retro_set_audio_sample_batch",
    "retro_set_controller_port_device",
    "retro_set_environment",
    "retro_set_input_poll",
    "retro_set_input_state",
    "retro_set_video_refresh",
    "retro_unload_game",
    "retro_unserialize",
}


def run_tool(tool: str, arguments: list[str]) -> str:
    result = subprocess.run(
        [tool, *arguments],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    return result.stdout


def elf_exports(tool: str, binary: pathlib.Path) -> set[str]:
    output = run_tool(tool, ["-D", "--defined-only", str(binary)])
    return {
        line.split()[-1]
        for line in output.splitlines()
        if line.split() and line.split()[-1].startswith("retro_")
    }


def pe_exports(tool: str, binary: pathlib.Path) -> set[str]:
    output = run_tool(tool, ["-p", str(binary)])
    marker = "[Ordinal/Name Pointer] Table"
    if marker not in output:
        raise SystemExit("export check failed: PE name pointer table is missing")
    export_table = output.split(marker, 1)[1].split("The Function Table", 1)[0]
    return set(re.findall(r"\b(retro_[a-z0-9_]+)\s*$", export_table, re.MULTILINE))


def macho_exports(tool: str, binary: pathlib.Path) -> set[str]:
    output = run_tool(tool, ["-gU", str(binary)])
    exports: set[str] = set()
    for line in output.splitlines():
        fields = line.split()
        if not fields:
            continue
        symbol = fields[-1]
        if symbol.startswith("_retro_"):
            exports.add(symbol[1:])
    return exports


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--format", choices=("elf", "pe", "macho"), required=True)
    parser.add_argument("--tool", required=True)
    parser.add_argument("binary", type=pathlib.Path)
    args = parser.parse_args()

    if not args.binary.is_file():
        raise SystemExit(f"export check failed: missing binary: {args.binary}")

    if args.format == "elf":
        exports = elf_exports(args.tool, args.binary)
    elif args.format == "pe":
        exports = pe_exports(args.tool, args.binary)
    else:
        exports = macho_exports(args.tool, args.binary)
    missing = REQUIRED_EXPORTS - exports
    unexpected = exports - REQUIRED_EXPORTS
    if missing or unexpected:
        if missing:
            print("missing exports:", ", ".join(sorted(missing)))
        if unexpected:
            print("unexpected public retro_ exports:", ", ".join(sorted(unexpected)))
        return 1

    print(f"Export check passed: {args.binary} ({len(exports)} symbols)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
