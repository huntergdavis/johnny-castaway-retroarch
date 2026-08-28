#!/usr/bin/env python3
"""Adversarial tests for installable frontend package validation."""

from __future__ import annotations

import io
import pathlib
import re
import stat
import struct
import subprocess
import tempfile
import unittest
import warnings
import zipfile
from unittest import mock

import check_installable_frontend as validator


VERSION = "0.1.2"
PROGRAM_ID = 0x0004000004A43000


def put_utf8(payload: bytearray, offset: int, size: int, value: str) -> None:
    encoded = value.encode("utf-8")
    if len(encoded) >= size:
        raise AssertionError("fixture string is too large")
    payload[offset : offset + len(encoded)] = encoded


def put_utf16(payload: bytearray, offset: int, units: int, value: str) -> None:
    encoded = value.encode("utf-16le")
    if len(encoded) >= units * 2:
        raise AssertionError("fixture string is too large")
    payload[offset : offset + len(encoded)] = encoded


def marker_blob() -> bytes:
    return b"\0".join((*validator.MARKERS, VERSION.encode("ascii")))


def vita_sfo_fixture() -> bytes:
    keys = b"TITLE\0TITLE_ID\0"
    title = b"Johnny Castaway\0"
    title_id = b"JCASTAWAY\0"
    key_offset = 20 + 2 * 16
    data_offset = key_offset + len(keys)
    payload = bytearray(data_offset + len(title) + len(title_id))
    payload[:4] = b"\0PSF"
    struct.pack_into("<I", payload, 4, 0x00000101)
    struct.pack_into("<III", payload, 8, key_offset, data_offset, 2)
    struct.pack_into("<HHIII", payload, 20, 0, 0x0204, len(title), len(title), 0)
    struct.pack_into(
        "<HHIII",
        payload,
        36,
        len(b"TITLE\0"),
        0x0204,
        len(title_id),
        len(title_id),
        len(title),
    )
    payload[key_offset:data_offset] = keys
    payload[data_offset:] = title + title_id
    return bytes(payload)


def vita_vpk_fixture() -> bytes:
    return zip_bytes(
        [
            ("eboot.bin", vita_self_fixture()),
            ("sce_sys/param.sfo", vita_sfo_fixture()),
        ]
    )


def vita_self_fixture() -> bytes:
    elf_offset = 0xA0
    header_len = 0x1000
    payload = bytearray(header_len + 52)
    payload[:4] = b"SCE\0"
    struct.pack_into("<IHHI", payload, 4, 3, 0xC0, 1, 0x600)
    struct.pack_into("<QQQ", payload, 16, header_len, 52, len(payload))
    struct.pack_into("<QQQ", payload, 48, 4, 0x80, elf_offset)
    payload[elf_offset : elf_offset + 4] = b"\x7fELF"
    payload[elf_offset + 4] = 1
    payload[elf_offset + 5] = 1
    return bytes(payload)


def vita_audit_elf_fixture() -> bytearray:
    markers = marker_blob()
    payload = bytearray(52 + len(markers))
    payload[:4] = b"\x7fELF"
    payload[4] = 1
    payload[5] = 1
    struct.pack_into("<HH", payload, 16, 2, 40)
    payload[52:] = markers
    return payload


def nro_fixture() -> bytearray:
    markers = marker_blob()
    executable_size = 0x80 + len(markers)
    icon = b"\xff\xd8\xffauthentic-icon"
    nacp = bytearray(0x4000)
    put_utf8(nacp, 0, 0x200, "Johnny Castaway")
    put_utf8(nacp, 0x200, 0x100, "Johnny Castaway contributors")
    put_utf8(nacp, 0x3060, 0x10, VERSION)
    aset_size = 0x38
    icon_offset = aset_size
    nacp_offset = icon_offset + len(icon)
    payload = bytearray(executable_size + nacp_offset + len(nacp))
    payload[0x10:0x14] = b"NRO0"
    struct.pack_into("<I", payload, 0x18, executable_size)
    payload[0x80 : 0x80 + len(markers)] = markers
    struct.pack_into(
        "<4sIQQQQQQ",
        payload,
        executable_size,
        b"ASET",
        0,
        icon_offset,
        len(icon),
        nacp_offset,
        len(nacp),
        0,
        0,
    )
    asset = executable_size
    payload[asset + icon_offset : asset + nacp_offset] = icon
    payload[asset + nacp_offset :] = nacp
    return payload


def smdh_fixture() -> bytearray:
    payload = bytearray(0x36C0)
    payload[:4] = b"SMDH"
    english = 8 + 0x200
    put_utf16(payload, english, 0x40, "Johnny Castaway")
    put_utf16(payload, english + 0x80, 0x80, "Johnny Castaway libretro")
    put_utf16(
        payload, english + 0x180, 0x40, "Johnny Castaway contributors"
    )
    payload[0x2040] = 1
    return payload


def cia_fixture() -> bytearray:
    header_size = 0x2020
    ticket_size = 0x40
    tmd_size = 0x200
    content_size = 0x400
    ticket_offset = validator.align(header_size)
    tmd_offset = ticket_offset + validator.align(ticket_size)
    content_offset = tmd_offset + validator.align(tmd_size)
    payload = bytearray(content_offset + content_size)
    struct.pack_into("<I", payload, 0, header_size)
    struct.pack_into("<IIII", payload, 8, 0, ticket_size, tmd_size, 0)
    struct.pack_into("<Q", payload, 24, content_size)
    struct.pack_into(">I", payload, tmd_offset, 0x00010004)
    tmd_body = tmd_offset + 0x140
    struct.pack_into(">Q", payload, tmd_body + 0x4C, PROGRAM_ID)
    struct.pack_into(">H", payload, tmd_body + 0x9C, validator.title_version(VERSION))
    struct.pack_into(">H", payload, tmd_body + 0x9E, 1)
    payload[content_offset + 0x100 : content_offset + 0x104] = b"NCCH"
    struct.pack_into("<I", payload, content_offset + 0x104, 2)
    struct.pack_into("<Q", payload, content_offset + 0x118, PROGRAM_ID)
    put_utf8(payload, content_offset + 0x150, 0x10, "CTR-H-JCAST")
    return payload


def zip_bytes(entries: list[tuple[str, bytes]], mode: int = 0o644) -> bytes:
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w") as archive:
        for name, payload in entries:
            info = zipfile.ZipInfo(name)
            info.create_system = 3
            info.external_attr = (stat.S_IFREG | mode) << 16
            archive.writestr(info, payload)
    return output.getvalue()


class MetadataParserTests(unittest.TestCase):
    def test_real_structure_fixtures_pass(self) -> None:
        validator.validate_nro(bytes(nro_fixture()), VERSION)
        validator.validate_smdh(bytes(smdh_fixture()))
        validator.validate_cia(bytes(cia_fixture()), VERSION)
        validator.validate_vpk(vita_vpk_fixture(), VERSION)
        validator.validate_vita_audit_elf(bytes(vita_audit_elf_fixture()), VERSION)

    def test_vita_linkage_markers_are_required_in_audit_elf_not_self(self) -> None:
        vpk = vita_vpk_fixture()
        with zipfile.ZipFile(io.BytesIO(vpk)) as archive:
            self.assertNotIn(b"Johnny Castaway", archive.read("eboot.bin"))
        validator.validate_vpk(vpk, VERSION)
        audit = vita_audit_elf_fixture()
        marker_at = audit.find(b"Johnny Castaway")
        self.assertGreaterEqual(marker_at, 0)
        audit[marker_at : marker_at + len(b"Johnny Castaway")] = b"X" * len(
            b"Johnny Castaway"
        )
        with self.assertRaisesRegex(SystemExit, "Vita audit ELF.*linked Johnny marker"):
            validator.validate_vita_audit_elf(bytes(audit), VERSION)

    def test_vita_self_header_truncation_and_offsets_are_rejected(self) -> None:
        with self.assertRaisesRegex(SystemExit, "complete SELF header"):
            validator.validate_vita_self(b"SCE\0")
        mutations = (
            (4, struct.pack("<I", 2), "version or type"),
            (16, struct.pack("<Q", 0x2000), "length fields"),
            (24, struct.pack("<Q", 0), "length fields"),
            (56, struct.pack("<Q", 0x90), "app-info offset"),
            (64, struct.pack("<Q", 0x2000), "embedded ELF offset"),
            (0xA0, b"NOPE", "ELF32 little-endian"),
        )
        for offset, replacement, message in mutations:
            with self.subTest(offset=offset):
                payload = bytearray(vita_self_fixture())
                payload[offset : offset + len(replacement)] = replacement
                with self.assertRaisesRegex(SystemExit, message):
                    validator.validate_vita_self(bytes(payload))

    def test_fake_minimal_metadata_fixtures_are_rejected(self) -> None:
        fake_nro = bytearray(0x200)
        fake_nro[0x10:0x14] = b"NRO0"
        fake_nro.extend(marker_blob())
        fake_nro.extend(b"Johnny Castaway")
        fake_smdh = bytearray(0x2000)
        fake_smdh[:4] = b"SMDH"
        fake_smdh[8:8 + len("Johnny Castaway".encode("utf-16le"))] = (
            "Johnny Castaway".encode("utf-16le")
        )
        fake_cia = bytearray(0x3000)
        struct.pack_into("<I", fake_cia, 0, 0x2020)
        struct.pack_into("<IIII", fake_cia, 8, 0, 1, 1, 0)
        struct.pack_into("<Q", fake_cia, 24, 1)
        fake_cia[-12:] = b"CTR-H-JCAST\0"
        for callback in (
            lambda: validator.validate_nro(bytes(fake_nro), VERSION),
            lambda: validator.validate_smdh(bytes(fake_smdh)),
            lambda: validator.validate_cia(bytes(fake_cia), VERSION),
        ):
            with self.assertRaises(SystemExit):
                callback()

    def test_nro_exact_metadata_and_executable_version_are_required(self) -> None:
        payload = nro_fixture()
        executable_size = struct.unpack_from("<I", payload, 0x18)[0]
        aset = struct.unpack_from("<4sIQQQQQQ", payload, executable_size)
        nacp = executable_size + aset[4]
        mutations = (
            (nacp, b"Wrong Title\0"),
            (nacp + 0x200, b"Wrong Author\0"),
            (nacp + 0x3060, b"9.9.9\0"),
        )
        for offset, value in mutations:
            with self.subTest(offset=offset):
                candidate = bytearray(payload)
                field_size = 0x10 if offset == nacp + 0x3060 else 0x100
                candidate[offset : offset + field_size] = b"\0" * field_size
                candidate[offset : offset + len(value)] = value
                with self.assertRaises(SystemExit):
                    validator.validate_nro(bytes(candidate), VERSION)
        candidate = bytearray(payload)
        version_at = candidate.find(VERSION.encode(), 0x80, executable_size)
        self.assertGreaterEqual(version_at, 0)
        candidate[version_at : version_at + len(VERSION)] = b"9.9.9"
        with self.assertRaisesRegex(SystemExit, "linked Johnny marker"):
            validator.validate_nro(bytes(candidate), VERSION)

    def test_smdh_exact_english_fields_are_required(self) -> None:
        payload = smdh_fixture()
        english = 8 + 0x200
        for offset in (english, english + 0x80, english + 0x180):
            candidate = bytearray(payload)
            candidate[offset : offset + 4] = b"N\0o\0"
            with self.assertRaises(SystemExit):
                validator.validate_smdh(bytes(candidate))

    def test_cia_exact_product_program_and_version_are_required(self) -> None:
        payload = cia_fixture()
        header_size = struct.unpack_from("<I", payload, 0)[0]
        ticket_size, tmd_size = struct.unpack_from("<II", payload, 12)
        tmd_offset = validator.align(header_size) + validator.align(ticket_size)
        body = tmd_offset + 0x140
        content_offset = tmd_offset + validator.align(tmd_size)
        mutations = (
            (body + 0x4C, struct.pack(">Q", PROGRAM_ID + 1)),
            (body + 0x9C, struct.pack(">H", 0xFFFF)),
            (content_offset + 0x118, struct.pack("<Q", PROGRAM_ID + 1)),
            (content_offset + 0x150, b"CTR-H-WRONG\0"),
        )
        for offset, replacement in mutations:
            with self.subTest(offset=offset):
                candidate = bytearray(payload)
                candidate[offset : offset + len(replacement)] = replacement
                with self.assertRaises(SystemExit):
                    validator.validate_cia(bytes(candidate), VERSION)


class MarkerAndArchiveTests(unittest.TestCase):
    def test_core_build_provenance_is_required(self) -> None:
        self.assertIn("CORE-BUILD-PROVENANCE.txt", validator.COMMON_PACKAGE_FILES)
        members = set(validator.COMMON_PACKAGE_FILES)
        members.remove("CORE-BUILD-PROVENANCE.txt")
        with self.assertRaisesRegex(SystemExit, "CORE-BUILD-PROVENANCE.txt"):
            validator.require_package_files(members, set())

    def test_markers_are_exactly_the_registered_21_option_keys(self) -> None:
        project = pathlib.Path(__file__).resolve().parents[1]
        source = (project / "src/libretro_core.c").read_text(encoding="utf-8")
        registered = set(re.findall(r'"(johnny_castaway_[a-z_]+)"', source))
        marker_keys = {
            marker.decode("ascii")
            for marker in validator.MARKERS
            if marker.startswith(b"johnny_castaway_")
        }
        self.assertEqual(len(registered), 21)
        self.assertEqual(set(validator.CORE_OPTION_KEYS), registered)
        self.assertEqual(marker_keys, registered)

    def test_embedded_core_version_is_a_required_marker(self) -> None:
        with self.assertRaisesRegex(SystemExit, VERSION):
            validator.require_markers(b"\0".join(validator.MARKERS), "fixture", VERSION)
        validator.require_markers(marker_blob(), "fixture", VERSION)

    def test_validator_rejects_unsafe_and_case_colliding_names_preflight(self) -> None:
        fixtures = (
            ([('C:/drive', b'x')], "unsafe archive member"),
            ([('../escape', b'x')], "unsafe archive member"),
            ([('back\\slash', b'x')], "unsafe archive member"),
            ([('README', b'x'), ('readme', b'y')], "case-fold collision"),
        )
        for entries, message in fixtures:
            with self.subTest(entries=entries):
                data = zip_bytes(entries)
                with zipfile.ZipFile(io.BytesIO(data)) as archive:
                    with mock.patch.object(
                        archive, "testzip", side_effect=AssertionError("too early")
                    ):
                        with self.assertRaisesRegex(SystemExit, message):
                            validator.safe_members(archive, "fixture")

    def test_validator_rejects_duplicate_and_non_normalized_mode(self) -> None:
        output = io.BytesIO()
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", UserWarning)
            with zipfile.ZipFile(output, "w") as archive:
                archive.writestr("same", b"one")
                archive.writestr("same", b"two")
        with zipfile.ZipFile(io.BytesIO(output.getvalue())) as archive:
            with self.assertRaisesRegex(SystemExit, "duplicate"):
                validator.safe_members(archive, "fixture")
        data = zip_bytes([("file", b"payload")], mode=0o755)
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            with self.assertRaisesRegex(SystemExit, "non-normalized mode"):
                validator.safe_members(
                    archive, "fixture", validator.NORMALIZED_MODE
                )

    def test_validator_member_and_size_bounds_are_preflighted(self) -> None:
        data = zip_bytes([("one", b"1"), ("two", b"2")])
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            with mock.patch.object(validator, "MAX_MEMBER_COUNT", 1):
                with self.assertRaisesRegex(SystemExit, "member limit"):
                    validator.safe_members(archive, "fixture")
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            with mock.patch.object(validator, "MAX_TOTAL_SIZE", 1):
                with self.assertRaisesRegex(SystemExit, "aggregate limit"):
                    validator.safe_members(archive, "fixture")


class LegalIdentityTests(unittest.TestCase):
    def test_project_files_and_ps2_nested_info_match_exact_commit(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            subprocess.run(
                ["git", "-C", str(root), "config", "user.email", "test@example.invalid"],
                check=True,
            )
            subprocess.run(
                ["git", "-C", str(root), "config", "user.name", "Validator Test"],
                check=True,
            )
            package_entries: list[tuple[str, bytes]] = []
            for packaged, source in validator.PROJECT_FILE_MAP.items():
                path = root / source
                path.parent.mkdir(parents=True, exist_ok=True)
                payload = f"source:{source}\n".encode()
                path.write_bytes(payload)
                package_entries.append((packaged, payload))
            info = (root / "johnny_castaway_libretro.info").read_bytes()
            package_entries.append(
                ("JohnnyCastaway/info/johnny_castaway_libretro.info", info)
            )
            subprocess.run(["git", "-C", str(root), "add", "."], check=True)
            subprocess.run(
                ["git", "-C", str(root), "commit", "-qm", "fixture"], check=True
            )
            commit = subprocess.run(
                ["git", "-C", str(root), "rev-parse", "HEAD"],
                check=True,
                text=True,
                stdout=subprocess.PIPE,
            ).stdout.strip()
            provenance = f"Johnny Castaway commit: {commit} (clean)\n".encode()
            data = zip_bytes(package_entries)
            with zipfile.ZipFile(io.BytesIO(data)) as package:
                validator.validate_project_files(
                    package, root, commit, "ps2", provenance
                )
            bad_entries = list(package_entries)
            bad_entries[0] = (bad_entries[0][0], b"modified")
            with zipfile.ZipFile(io.BytesIO(zip_bytes(bad_entries))) as package:
                with self.assertRaisesRegex(SystemExit, "differs from expected commit"):
                    validator.validate_project_files(
                        package, root, commit, "ps2", provenance
                    )

    def test_expected_commit_must_be_full_and_provenance_clean(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            data = zip_bytes(
                [(name, b"fixture") for name in validator.PROJECT_FILE_MAP]
            )
            with zipfile.ZipFile(io.BytesIO(data)) as package:
                with self.assertRaisesRegex(SystemExit, "full lowercase 40-hex"):
                    validator.validate_project_files(
                        package, root, "abc", "switch", b"dirty"
                    )


def main() -> int:
    suite = unittest.defaultTestLoader.loadTestsFromModule(__import__(__name__))
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
