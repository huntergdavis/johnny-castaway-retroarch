#!/usr/bin/env python3
"""Validate generated Nintendo, Vita, and PS2 frontend packages."""

from __future__ import annotations

import argparse
import datetime
import io
import pathlib
import re
import stat
import struct
import subprocess
import zipfile
import xml.etree.ElementTree as ET


RETROARCH_REVISION = "96a1b1a9cf3f9166affcfd7df4323aa58d5c281a"
CORE_OPTION_KEYS = (
    "johnny_castaway_audio_enabled",
    "johnny_castaway_audio_volume",
    "johnny_castaway_caption_background",
    "johnny_castaway_caption_opacity",
    "johnny_castaway_caption_position",
    "johnny_castaway_caption_size",
    "johnny_castaway_captions_enabled",
    "johnny_castaway_chapter",
    "johnny_castaway_display_source",
    "johnny_castaway_holiday_overlay",
    "johnny_castaway_initial_screen",
    "johnny_castaway_ocean_enabled",
    "johnny_castaway_ocean_volume",
    "johnny_castaway_playback_speed",
    "johnny_castaway_raft_stage",
    "johnny_castaway_simulated_day",
    "johnny_castaway_simulated_hour",
    "johnny_castaway_simulated_month",
    "johnny_castaway_story_calendar",
    "johnny_castaway_story_seed",
    "johnny_castaway_tide",
)
MARKERS = (
    b"Johnny Castaway",
    *(key.encode("ascii") for key in CORE_OPTION_KEYS),
)
MAX_MEMBER_COUNT = 4096
MAX_MEMBER_SIZE = 256 * 1024 * 1024
MAX_TOTAL_SIZE = 512 * 1024 * 1024
NORMALIZED_MODE = stat.S_IFREG | 0o644
FORBIDDEN = re.compile(
    r"(^|/)(resource\.(map|001)|sound[0-9]+\.wav|[^/]+\.(ads|ttm|bmp|scr|vag))$",
    re.IGNORECASE,
)
COMMON_PACKAGE_FILES = {
    "LICENSE",
    "CREDITS.md",
    "johnny_castaway_libretro.info",
    "README-INSTALL.md",
    "BUILD-PROVENANCE.txt",
    "CORE-BUILD-PROVENANCE.txt",
    "docs/PROVENANCE.md",
    "docs/THIRD_PARTY_NOTICES.md",
    "docs/FRONTEND_SDK_NOTICES.md",
    "docs/licenses/BigSoundBank-0266-CC0.md",
    "docs/licenses/frontend/libctru-Zlib.txt",
    "docs/licenses/frontend/libnx-ISC.txt",
    "docs/licenses/frontend/wut-Zlib.txt",
    "docs/licenses/RetroArch-GPL-3.0.txt",
}
PROJECT_FILE_MAP = {
    "LICENSE": "LICENSE",
    "CREDITS.md": "CREDITS.md",
    "johnny_castaway_libretro.info": "johnny_castaway_libretro.info",
    "README-INSTALL.md": "docs/INSTALLABLE_FRONTENDS.md",
    "docs/PROVENANCE.md": "docs/PROVENANCE.md",
    "docs/THIRD_PARTY_NOTICES.md": "docs/THIRD_PARTY_NOTICES.md",
    "docs/FRONTEND_SDK_NOTICES.md": "docs/FRONTEND_SDK_NOTICES.md",
    "docs/licenses/BigSoundBank-0266-CC0.md": (
        "docs/licenses/BigSoundBank-0266-CC0.md"
    ),
    "docs/licenses/frontend/libctru-Zlib.txt": (
        "docs/licenses/frontend/libctru-Zlib.txt"
    ),
    "docs/licenses/frontend/libnx-ISC.txt": (
        "docs/licenses/frontend/libnx-ISC.txt"
    ),
    "docs/licenses/frontend/wut-Zlib.txt": (
        "docs/licenses/frontend/wut-Zlib.txt"
    ),
}


def fail(message: str) -> None:
    raise SystemExit(f"frontend package check failed: {message}")


def safe_archive_name(name: str) -> str:
    if (
        not name
        or "\0" in name
        or "\\" in name
        or ":" in name
        or any(part in ("", ".", "..") for part in name.split("/"))
    ):
        fail(f"unsafe archive member: {name!r}")
    path = pathlib.PurePosixPath(name)
    if path.is_absolute():
        fail(f"unsafe archive member: {name!r}")
    return path.as_posix()


def safe_members(
    archive: zipfile.ZipFile,
    label: str,
    expected_mode: int | None = None,
) -> dict[str, zipfile.ZipInfo]:
    infos = archive.infolist()
    if len(infos) > MAX_MEMBER_COUNT:
        fail(f"{label} exceeds the {MAX_MEMBER_COUNT}-member limit")
    result: dict[str, zipfile.ZipInfo] = {}
    folded: dict[str, str] = {}
    total = 0
    for info in infos:
        raw_name = info.filename[:-1] if info.is_dir() else info.filename
        name = safe_archive_name(raw_name)
        case_name = name.casefold()
        previous = folded.get(case_name)
        if previous is not None:
            kind = "duplicate" if previous == name else "case-fold collision"
            fail(f"{label} has a {kind}: {previous!r} and {name!r}")
        folded[case_name] = name
        if info.flag_bits & 0x1:
            fail(f"{label} has an encrypted member: {name}")
        unix_mode = info.external_attr >> 16
        file_type = stat.S_IFMT(unix_mode)
        if file_type == stat.S_IFLNK:
            fail(f"{label} has a symlink member: {name}")
        if info.is_dir():
            if info.file_size:
                fail(f"{label} directory has payload data: {name}")
            continue
        if file_type not in (0, stat.S_IFREG):
            fail(f"{label} has a non-regular member: {name}")
        if expected_mode is not None and (
            info.create_system != 3 or unix_mode != expected_mode
        ):
            fail(f"{label} member has a non-normalized mode: {name}")
        if info.file_size < 0 or info.file_size > MAX_MEMBER_SIZE:
            fail(f"{label} member exceeds the {MAX_MEMBER_SIZE}-byte limit: {name}")
        total += info.file_size
        if total > MAX_TOTAL_SIZE:
            fail(f"{label} exceeds the {MAX_TOTAL_SIZE}-byte aggregate limit")
        if FORBIDDEN.search(name):
            fail(f"{label} contains original game data: {name}")
        result[name] = info
    bad = archive.testzip()
    if bad:
        fail(f"{label} has a corrupt member: {bad}")
    return result


def require_markers(payload: bytes, label: str, version: str) -> None:
    for marker in (*MARKERS, version.encode("ascii")):
        if marker not in payload:
            fail(f"{label} is missing linked Johnny marker: {marker.decode()}")


def require_package_files(
    member_names: set[str], install_paths: set[str]
) -> None:
    missing = (COMMON_PACKAGE_FILES | install_paths) - member_names
    if missing:
        fail("install ZIP is missing: " + ", ".join(sorted(missing)))


def read_sfo(payload: bytes) -> dict[str, bytes]:
    if len(payload) < 20 or payload[:4] != b"\x00PSF":
        fail("Vita param.sfo has an invalid header")
    key_offset, data_offset, count = struct.unpack_from("<III", payload, 8)
    if count > 256:
        fail("Vita param.sfo has an unreasonable entry count")
    values: dict[str, bytes] = {}
    for index in range(count):
        offset = 20 + index * 16
        if offset + 16 > len(payload):
            fail("Vita param.sfo entry table is truncated")
        key_rel, _format, length, maximum, data_rel = struct.unpack_from(
            "<HHIII", payload, offset
        )
        key_start = key_offset + key_rel
        key_end = payload.find(b"\0", key_start)
        value_start = data_offset + data_rel
        if (
            key_start >= len(payload)
            or key_end < key_start
            or length > maximum
            or value_start + length > len(payload)
        ):
            fail("Vita param.sfo contains an invalid entry")
        key = payload[key_start:key_end].decode("utf-8", "strict")
        values[key] = payload[value_start : value_start + length].rstrip(b"\0")
    return values


def fixed_utf8(payload: bytes, offset: int, size: int, label: str) -> str:
    field = payload[offset : offset + size]
    if len(field) != size or b"\0" not in field:
        fail(f"{label} is truncated or unterminated")
    value, padding = field.split(b"\0", 1)
    if any(padding):
        fail(f"{label} has nonzero string padding")
    try:
        return value.decode("utf-8", "strict")
    except UnicodeDecodeError as error:
        fail(f"{label} is not valid UTF-8: {error}")


def fixed_utf16le(payload: bytes, offset: int, units: int, label: str) -> str:
    field = payload[offset : offset + units * 2]
    if len(field) != units * 2:
        fail(f"{label} is truncated")
    values = struct.unpack(f"<{units}H", field)
    try:
        terminator = values.index(0)
    except ValueError:
        fail(f"{label} is unterminated")
    if any(values[terminator + 1 :]):
        fail(f"{label} has nonzero string padding")
    try:
        return field[: terminator * 2].decode("utf-16le", "strict")
    except UnicodeDecodeError as error:
        fail(f"{label} is not valid UTF-16LE: {error}")


def validate_nro(payload: bytes, version: str) -> None:
    if len(payload) < 0x80 or payload[0x10:0x14] != b"NRO0":
        fail("Switch artifact has no NRO0 header")
    executable_size = struct.unpack_from("<I", payload, 0x18)[0]
    if executable_size < 0x80 or executable_size + 0x38 > len(payload):
        fail("Switch NRO executable/asset boundary is invalid")
    aset = struct.unpack_from("<4sIQQQQQQ", payload, executable_size)
    magic, aset_version = aset[:2]
    if magic != b"ASET" or aset_version != 0:
        fail("Switch NRO has no supported ASET header")
    ranges: list[tuple[int, int, str]] = []
    for label, relative, size in (
        ("icon", aset[2], aset[3]),
        ("NACP", aset[4], aset[5]),
        ("RomFS", aset[6], aset[7]),
    ):
        if not size:
            if relative:
                fail(f"Switch ASET {label} has an offset without a size")
            continue
        start = executable_size + relative
        end = start + size
        if relative < 0x38 or end < start or end > len(payload):
            fail(f"Switch ASET {label} range is invalid")
        ranges.append((start, end, label))
    for previous, current in zip(sorted(ranges), sorted(ranges)[1:]):
        if previous[1] > current[0]:
            fail(f"Switch ASET {previous[2]} and {current[2]} ranges overlap")
    icon_offset, icon_size = aset[2], aset[3]
    icon_start = executable_size + icon_offset
    if not icon_size or payload[icon_start : icon_start + 3] != b"\xff\xd8\xff":
        fail("Switch ASET icon is missing or is not a JPEG")
    nacp_relative, nacp_size = aset[4], aset[5]
    if nacp_size != 0x4000:
        fail("Switch NACP has an invalid size")
    nacp_offset = executable_size + nacp_relative
    title = fixed_utf8(payload, nacp_offset, 0x200, "Switch NACP English title")
    author = fixed_utf8(
        payload, nacp_offset + 0x200, 0x100, "Switch NACP English author"
    )
    display_version = fixed_utf8(
        payload, nacp_offset + 0x3060, 0x10, "Switch NACP display version"
    )
    if title != "Johnny Castaway":
        fail(f"unexpected Switch NACP English title: {title!r}")
    if author != "Johnny Castaway contributors":
        fail(f"unexpected Switch NACP English author: {author!r}")
    if display_version != version:
        fail(f"unexpected Switch NACP display version: {display_version!r}")
    require_markers(payload[:executable_size], "Switch NRO executable", version)


def validate_3dsx(payload: bytes, version: str) -> None:
    if len(payload) < 0x20 or payload[:4] != b"3DSX":
        fail("Nintendo 3DS artifact has no 3DSX header")
    header_size, reloc_header_size = struct.unpack_from("<HH", payload, 4)
    if header_size < 0x20 or header_size > len(payload) or not reloc_header_size:
        fail("Nintendo 3DSX has an invalid header size")
    require_markers(payload, "Nintendo 3DSX", version)


def validate_smdh(payload: bytes) -> None:
    if len(payload) != 0x36C0 or payload[:4] != b"SMDH":
        fail("Nintendo 3DS metadata has no SMDH header")
    smdh_version, reserved = struct.unpack_from("<HH", payload, 4)
    if smdh_version != 0 or reserved != 0:
        fail("Nintendo 3DS SMDH header version is invalid")
    english = 8 + 0x200
    title = fixed_utf16le(payload, english, 0x40, "3DS SMDH English title")
    description = fixed_utf16le(
        payload, english + 0x80, 0x80, "3DS SMDH English description"
    )
    publisher = fixed_utf16le(
        payload, english + 0x180, 0x40, "3DS SMDH English publisher"
    )
    if title != "Johnny Castaway":
        fail(f"unexpected Nintendo 3DS SMDH English title: {title!r}")
    if description != "Johnny Castaway libretro":
        fail(f"unexpected Nintendo 3DS SMDH English description: {description!r}")
    if publisher != "Johnny Castaway contributors":
        fail(f"unexpected Nintendo 3DS SMDH English publisher: {publisher!r}")
    if not any(payload[0x2040:0x36C0]):
        fail("Nintendo 3DS SMDH icon data is empty")


def align(value: int, boundary: int = 64) -> int:
    return (value + boundary - 1) & ~(boundary - 1)


def title_version(version: str) -> int:
    fields = version.split(".")
    if len(fields) != 3 or any(not field.isdecimal() for field in fields):
        fail(f"frontend version is not numeric major.minor.micro: {version!r}")
    major, minor, micro = (int(field) for field in fields)
    if major > 63 or minor > 63 or micro > 15:
        fail(f"frontend version exceeds Nintendo 3DS limits: {version!r}")
    return (major << 10) | (minor << 4) | micro


def tmd_signature_size(signature_type: int) -> int:
    sizes = {
        0x00010000: 0x240,
        0x00010001: 0x140,
        0x00010002: 0x80,
        0x00010003: 0x240,
        0x00010004: 0x140,
        0x00010005: 0x80,
    }
    try:
        return sizes[signature_type]
    except KeyError:
        fail(f"Nintendo 3DS TMD has an unknown signature type: {signature_type:#x}")


def validate_cia(payload: bytes, version: str) -> None:
    if len(payload) < 0x2020:
        fail("Nintendo 3DS CIA is truncated")
    header_size = struct.unpack_from("<I", payload, 0)[0]
    cia_type, cia_version = struct.unpack_from("<HH", payload, 4)
    cert_size, ticket_size, tmd_size, meta_size = struct.unpack_from("<IIII", payload, 8)
    content_size = struct.unpack_from("<Q", payload, 24)[0]
    if (
        header_size != 0x2020
        or cia_type != 0
        or cia_version != 0
        or not ticket_size
        or not tmd_size
        or not content_size
    ):
        fail("Nintendo 3DS CIA header sizes are invalid")
    cert_offset = align(header_size)
    ticket_offset = cert_offset + align(cert_size)
    tmd_offset = ticket_offset + align(ticket_size)
    content_offset = tmd_offset + align(tmd_size)
    meta_offset = content_offset + align(content_size)
    if meta_offset + meta_size > len(payload):
        fail("Nintendo 3DS CIA sections exceed the file size")
    signature_type = struct.unpack_from(">I", payload, tmd_offset)[0]
    tmd_body = tmd_offset + tmd_signature_size(signature_type)
    if tmd_body + 0xA4 > tmd_offset + tmd_size:
        fail("Nintendo 3DS CIA TMD is truncated")
    expected_program_id = 0x0004000000000000 | (0x4A430 << 8)
    tmd_title_id = struct.unpack_from(">Q", payload, tmd_body + 0x4C)[0]
    tmd_version = struct.unpack_from(">H", payload, tmd_body + 0x9C)[0]
    content_count = struct.unpack_from(">H", payload, tmd_body + 0x9E)[0]
    if tmd_title_id != expected_program_id:
        fail(f"unexpected Nintendo 3DS TMD title ID: {tmd_title_id:#018x}")
    if tmd_version != title_version(version):
        fail(f"unexpected Nintendo 3DS TMD title version: {tmd_version:#06x}")
    if content_count != 1:
        fail(f"unexpected Nintendo 3DS TMD content count: {content_count}")
    if content_offset + 0x200 > len(payload):
        fail("Nintendo 3DS CIA has no complete NCCH header")
    if payload[content_offset + 0x100 : content_offset + 0x104] != b"NCCH":
        fail("Nintendo 3DS CIA content has no NCCH header")
    ncch_units = struct.unpack_from("<I", payload, content_offset + 0x104)[0]
    ncch_size = ncch_units * 0x200
    if not ncch_units or ncch_size != content_size:
        fail("Nintendo 3DS NCCH size does not match the CIA content size")
    ncch_program_id = struct.unpack_from("<Q", payload, content_offset + 0x118)[0]
    product_code = fixed_utf8(
        payload, content_offset + 0x150, 0x10, "Nintendo 3DS NCCH product code"
    )
    if ncch_program_id != expected_program_id:
        fail(f"unexpected Nintendo 3DS NCCH program ID: {ncch_program_id:#018x}")
    if product_code != "CTR-H-JCAST":
        fail(f"unexpected Nintendo 3DS NCCH product code: {product_code!r}")


def validate_vpk(payload: bytes, version: str) -> None:
    with zipfile.ZipFile(io.BytesIO(payload)) as archive:
        members = safe_members(archive, "Vita VPK")
        required = {"eboot.bin", "sce_sys/param.sfo"}
        missing = required - members.keys()
        if missing:
            fail("Vita VPK is missing: " + ", ".join(sorted(missing)))
        executable = archive.read("eboot.bin")
        if len(executable) < 4 or executable[:4] != b"SCE\0":
            fail("Vita eboot.bin has no SELF header")
        sfo = read_sfo(archive.read("sce_sys/param.sfo"))
        if sfo.get("TITLE_ID") != b"JCASTAWAY":
            fail(f"unexpected Vita TITLE_ID: {sfo.get('TITLE_ID')!r}")
        if sfo.get("TITLE") != b"Johnny Castaway":
            fail(f"unexpected Vita TITLE: {sfo.get('TITLE')!r}")


def validate_vita_audit_elf(payload: bytes, version: str) -> None:
    if len(payload) < 52 or payload[:4] != b"\x7fELF":
        fail("Vita audit artifact has no ELF header")
    if payload[4] != 1 or payload[5] != 1:
        fail("Vita audit ELF is not ELF32 little-endian")
    elf_type, machine = struct.unpack_from("<HH", payload, 16)
    if elf_type != 2 or machine != 40:
        fail("Vita audit ELF is not an executable for ARM")
    require_markers(payload, "Vita audit ELF", version)


def validate_ps2_elf(payload: bytes, version: str) -> None:
    if len(payload) < 52 or payload[:4] != b"\x7fELF":
        fail("PlayStation 2 artifact has no ELF header")
    if payload[4] != 1 or payload[5] != 1:
        fail("PlayStation 2 ELF is not ELF32 little-endian")
    elf_type, machine = struct.unpack_from("<HH", payload, 16)
    if elf_type != 2 or machine != 8:
        fail("PlayStation 2 ELF is not an executable for MIPS")
    require_markers(payload, "PlayStation 2 ELF", version)


def validate_dol(payload: bytes, version: str) -> None:
    if len(payload) < 0x100:
        fail("GameCube DOL is truncated")
    text_offsets = struct.unpack_from(">7I", payload, 0x00)
    data_offsets = struct.unpack_from(">11I", payload, 0x1C)
    text_sizes = struct.unpack_from(">7I", payload, 0x90)
    data_sizes = struct.unpack_from(">11I", payload, 0xAC)
    entry = struct.unpack_from(">I", payload, 0xE0)[0]
    sections = [
        (offset, size)
        for offset, size in zip(text_offsets + data_offsets, text_sizes + data_sizes)
        if size
    ]
    if not sections or not entry:
        fail("GameCube DOL has no loadable sections or entry point")
    for offset, size in sections:
        if offset < 0x100 or offset + size > len(payload):
            fail("GameCube DOL has an invalid section range")
    require_markers(payload, "GameCube DOL", version)


def validate_wiiu_rpx(payload: bytes) -> None:
    if len(payload) < 52 or payload[:4] != b"\x7fELF":
        fail("Wii U RPX has no ELF header")
    if payload[4] != 1 or payload[5] != 2:
        fail("Wii U RPX is not ELF32 big-endian")
    elf_type, machine = struct.unpack_from(">HH", payload, 16)
    if elf_type != 0xFE01 or machine != 20:
        fail("Wii U RPX is not a Cafe OS PowerPC executable")


def validate_wiiu_audit_elf(payload: bytes, version: str) -> None:
    if len(payload) < 52 or payload[:4] != b"\x7fELF":
        fail("Wii U audit artifact has no ELF header")
    if payload[4] != 1 or payload[5] != 2:
        fail("Wii U audit ELF is not ELF32 big-endian")
    elf_type, machine = struct.unpack_from(">HH", payload, 16)
    if elf_type != 2 or machine != 20:
        fail("Wii U audit ELF is not an executable for PowerPC")
    require_markers(payload, "Wii U audit ELF", version)


def validate_wiiu_meta(payload: bytes, version: str) -> None:
    try:
        root = ET.fromstring(payload)
    except ET.ParseError as error:
        fail(f"Wii U meta.xml is invalid: {error}")
    expected = {
        "name": "Johnny Castaway",
        "coder": "Johnny Castaway contributors",
        "version": version,
        "short_description": "Johnny Castaway for RetroArch",
        "long_description": (
            "Runs legally owned Johnny Castaway data with the statically linked "
            "libretro core."
        ),
    }
    if root.tag != "app" or root.attrib != {"version": "1"}:
        fail("Wii U meta.xml has an unexpected application identity")
    for name, value in expected.items():
        if root.findtext(name) != value:
            fail(f"Wii U meta.xml has an unexpected {name}")


def expected_zip_time(epoch: int) -> tuple[int, int, int, int, int, int]:
    value = datetime.datetime.fromtimestamp(epoch, datetime.timezone.utc)
    if value.year < 1980:
        value = datetime.datetime(1980, 1, 1, tzinfo=datetime.timezone.utc)
    if value.year > 2107:
        fail("SOURCE_DATE_EPOCH exceeds the ZIP timestamp range")
    second = value.second - value.second % 2
    return (value.year, value.month, value.day, value.hour, value.minute, second)


def git_bytes(project_root: pathlib.Path, expected_commit: str, path: str) -> bytes:
    result = subprocess.run(
        ["git", "-C", str(project_root), "show", f"{expected_commit}:{path}"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode:
        detail = result.stderr.decode("utf-8", "replace").strip()
        fail(f"cannot read expected project file {path}: {detail}")
    return result.stdout


def validate_project_files(
    package: zipfile.ZipFile,
    project_root: pathlib.Path,
    expected_commit: str,
    target: str,
    provenance: bytes,
) -> None:
    if not re.fullmatch(r"[0-9a-f]{40}", expected_commit):
        fail("--expected-commit must be a full lowercase 40-hex commit ID")
    if not project_root.is_dir():
        fail(f"--project-root is not a directory: {project_root}")
    resolved = subprocess.run(
        [
            "git",
            "-C",
            str(project_root),
            "rev-parse",
            "--verify",
            f"{expected_commit}^{{commit}}",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if resolved.returncode or resolved.stdout.strip() != expected_commit:
        fail("--expected-commit does not resolve exactly in --project-root")
    for packaged, source in PROJECT_FILE_MAP.items():
        if package.read(packaged) != git_bytes(project_root, expected_commit, source):
            fail(f"packaged project file differs from expected commit: {packaged}")
    expected_line = f"Johnny Castaway commit: {expected_commit} (clean)".encode()
    if expected_line not in provenance.splitlines():
        fail("build provenance does not name the exact clean expected commit")
    if target == "ps2":
        nested = "JohnnyCastaway/info/johnny_castaway_libretro.info"
        expected_info = git_bytes(
            project_root, expected_commit, "johnny_castaway_libretro.info"
        )
        if package.read(nested) != expected_info:
            fail("PS2 nested core metadata differs from expected commit")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--target",
        required=True,
        choices=("switch", "3ds", "gamecube", "wiiu", "vita", "ps2"),
    )
    parser.add_argument("--artifact-dir", required=True, type=pathlib.Path)
    parser.add_argument("--package", required=True, type=pathlib.Path)
    parser.add_argument("--version", required=True)
    parser.add_argument("--epoch", required=True, type=int)
    parser.add_argument("--project-root", type=pathlib.Path)
    parser.add_argument("--expected-commit")
    args = parser.parse_args()
    if (args.project_root is None) != (args.expected_commit is None):
        fail("--project-root and --expected-commit must be supplied together")

    raw_names = {
        "switch": ("JohnnyCastaway.nro",),
        "3ds": ("JohnnyCastaway.3dsx", "JohnnyCastaway.smdh", "JohnnyCastaway.cia"),
        "vita": ("JohnnyCastaway.vpk",),
        "ps2": ("JohnnyCastaway.elf",),
        "gamecube": ("JohnnyCastaway.dol",),
        "wiiu": ("JohnnyCastaway.rpx",),
    }[args.target]
    for name in raw_names:
        path = args.artifact_dir / name
        if not path.is_file() or not path.stat().st_size:
            fail(f"missing raw artifact: {path}")

    install_paths = {
        "switch": {"switch/JohnnyCastaway/JohnnyCastaway.nro": "JohnnyCastaway.nro"},
        "3ds": {
            "3ds/JohnnyCastaway/JohnnyCastaway.3dsx": "JohnnyCastaway.3dsx",
            "3ds/JohnnyCastaway/JohnnyCastaway.smdh": "JohnnyCastaway.smdh",
            "cia/JohnnyCastaway.cia": "JohnnyCastaway.cia",
        },
        "vita": {"JohnnyCastaway.vpk": "JohnnyCastaway.vpk"},
        "ps2": {"JohnnyCastaway/retroarch_ps2.elf": "JohnnyCastaway.elf"},
        "gamecube": {"apps/JohnnyCastaway/boot.dol": "JohnnyCastaway.dol"},
        "wiiu": {
            "wiiu/apps/JohnnyCastaway/JohnnyCastaway.rpx": "JohnnyCastaway.rpx",
            "wiiu/apps/JohnnyCastaway/meta.xml": "meta.xml",
        },
    }[args.target]

    expected_time = expected_zip_time(args.epoch)
    with zipfile.ZipFile(args.package) as package:
        members = safe_members(package, "install ZIP", NORMALIZED_MODE)
        require_package_files(set(members), set(install_paths))
        for info in members.values():
            if info.date_time != expected_time:
                fail(f"install ZIP member has a non-reproducible timestamp: {info.filename}")
        for packaged, raw in install_paths.items():
            if package.read(packaged) != (args.artifact_dir / raw).read_bytes():
                fail(f"packaged artifact differs from validated raw file: {packaged}")
        provenance = package.read("BUILD-PROVENANCE.txt")
        for expected in (
            RETROARCH_REVISION.encode(),
            f"Frontend version: {args.version}".encode(),
            b"Core archive SHA-256:",
        ):
            if expected not in provenance:
                fail(f"build provenance is missing: {expected.decode()}")
        info_text = package.read("johnny_castaway_libretro.info").decode("utf-8")
        if f'display_version = "{args.version}"' not in info_text:
            fail("packaged core metadata version is inconsistent")
        if args.project_root is not None and args.expected_commit is not None:
            validate_project_files(
                package,
                args.project_root,
                args.expected_commit,
                args.target,
                provenance,
            )

    if args.target == "switch":
        validate_nro((args.artifact_dir / raw_names[0]).read_bytes(), args.version)
    elif args.target == "3ds":
        validate_3dsx((args.artifact_dir / raw_names[0]).read_bytes(), args.version)
        validate_smdh((args.artifact_dir / raw_names[1]).read_bytes())
        validate_cia((args.artifact_dir / raw_names[2]).read_bytes(), args.version)
    elif args.target == "vita":
        validate_vpk((args.artifact_dir / raw_names[0]).read_bytes(), args.version)
        audit = args.artifact_dir / "retroarch_vita.unstripped.elf"
        if not audit.is_file():
            fail(f"missing Vita audit ELF: {audit}")
        validate_vita_audit_elf(audit.read_bytes(), args.version)
    elif args.target == "ps2":
        validate_ps2_elf((args.artifact_dir / raw_names[0]).read_bytes(), args.version)
        with zipfile.ZipFile(args.package) as package:
            members = safe_members(
                package, "PlayStation 2 install ZIP", NORMALIZED_MODE
            )
            required = {
                "JohnnyCastaway/cores/README.txt",
                "JohnnyCastaway/info/johnny_castaway_libretro.info",
                "JohnnyCastaway/retroarch/PLACE-CONTENT-HERE.txt",
            }
            missing = required - members.keys()
            if missing:
                fail("PlayStation 2 install ZIP is missing: " + ", ".join(sorted(missing)))
    elif args.target == "gamecube":
        validate_dol((args.artifact_dir / raw_names[0]).read_bytes(), args.version)
    else:
        validate_wiiu_rpx((args.artifact_dir / raw_names[0]).read_bytes())
        validate_wiiu_meta((args.artifact_dir / "meta.xml").read_bytes(), args.version)
        audit = args.artifact_dir / "retroarch_wiiu.elf"
        if not audit.is_file():
            fail(f"missing Wii U audit ELF: {audit}")
        validate_wiiu_audit_elf(audit.read_bytes(), args.version)

    print(f"Validated installable {args.target} frontend package: {args.package}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
