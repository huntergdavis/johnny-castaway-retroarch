#!/usr/bin/env python3
"""Adversarial tests for exact Web distribution provenance checks."""

from __future__ import annotations

import hashlib
import pathlib
import subprocess
import sys
import tempfile
import unittest
import zipfile

from web_smoke_test import (
    SmokeFailure,
    complete_scene_visual_only_result,
    frame_has_color_key_failure,
    frame_quality,
    validate_scene_visual_only_args,
)


ROOT = pathlib.Path(__file__).resolve().parents[1]
CHECKER = ROOT / "tools/check_web_dist.py"


class WebDistributionValidatorTests(unittest.TestCase):
    def make_distribution(self, root: pathlib.Path) -> None:
        required = (
            "style.css",
            "jc-web-player.js",
            "johnny_castaway_libretro.js",
            "WEB_PLAYER_NOTICE.md",
            "CREDITS.md",
            "docs/PROVENANCE.md",
            "docs/THIRD_PARTY_NOTICES.md",
            "licenses/BrowserFS-license.md",
            "licenses/johnny-castaway-retroarch-GPL-3.0",
            "licenses/BigSoundBank-0266-CC0.md",
            "licenses/RetroArch-GPL-3.0",
            "licenses/retroarch-assets-CC-BY-4.0",
        )
        for relative in required:
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(f"fixture {relative}\n", encoding="utf-8")

        (root / "index.html").write_text(
            '<link href="style.css"><script src="jc-web-player.js"></script>\n',
            encoding="utf-8",
        )
        browserfs = b"browserfs fixture\n"
        javascript = (root / "johnny_castaway_libretro.js").read_bytes()
        wasm = b"\0asmfixture"
        (root / "browserfs.min.js").write_bytes(browserfs)
        (root / "johnny_castaway_libretro.wasm").write_bytes(wasm)
        retroarch_license = (root / "licenses/RetroArch-GPL-3.0").read_bytes()
        assets_license = (
            root / "licenses/retroarch-assets-CC-BY-4.0"
        ).read_bytes()

        info = b'display_version = "0.1.3"\n'
        bundle_path = root / "assets/frontend/bundle.zip"
        bundle_path.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(bundle_path, "w") as bundle:
            bundle.writestr("assets/ozone/fixture", b"x")
            bundle.writestr("assets/pkg/fixture", b"x")
            bundle.writestr("assets/sounds/fixture", b"x")
            bundle.writestr("info/johnny_castaway_libretro.info", info)
        asset_bundle = bundle_path.read_bytes()

        (root / "BUILD-PROVENANCE.txt").write_text(
            "\n".join(
                (
                    "Johnny Castaway RetroArch Web Player build provenance",
                    f"Johnny Castaway commit: {'a' * 40} (clean)",
                    "Frontend version: 0.1.3",
                    "Tree state: clean",
                    f"Core metadata SHA-256: {hashlib.sha256(info).hexdigest()}",
                    f"BrowserFS SHA-256: {hashlib.sha256(browserfs).hexdigest()}",
                    f"JavaScript SHA-256: {hashlib.sha256(javascript).hexdigest()}",
                    f"WebAssembly SHA-256: {hashlib.sha256(wasm).hexdigest()}",
                    "Asset bundle SHA-256: "
                    f"{hashlib.sha256(asset_bundle).hexdigest()}",
                    "RetroArch license SHA-256: "
                    f"{hashlib.sha256(retroarch_license).hexdigest()}",
                    "retroarch-assets license SHA-256: "
                    f"{hashlib.sha256(assets_license).hexdigest()}",
                    "",
                )
            ),
            encoding="utf-8",
        )

    def run_checker(self, root: pathlib.Path) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(CHECKER), str(root)],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )

    def test_valid_self_consistent_distribution_passes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            self.make_distribution(root)
            result = self.run_checker(root)
            self.assertEqual(result.returncode, 0, result.stdout)

    def test_missing_exact_commit_field_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            self.make_distribution(root)
            provenance = root / "BUILD-PROVENANCE.txt"
            provenance.write_text(
                provenance.read_text(encoding="utf-8").replace(
                    f"Johnny Castaway commit: {'a' * 40} (clean)\n", ""
                ),
                encoding="utf-8",
            )
            result = self.run_checker(root)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("exact Johnny commit/version/tree/info fields", result.stdout)

    def test_tampered_wasm_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            self.make_distribution(root)
            (root / "johnny_castaway_libretro.wasm").write_bytes(b"\0asmtampered")
            result = self.run_checker(root)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("WebAssembly hash", result.stdout)

    def test_tampered_bundled_metadata_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            self.make_distribution(root)
            bundle_path = root / "assets/frontend/bundle.zip"
            with zipfile.ZipFile(bundle_path) as bundle:
                members = {
                    info.filename: bundle.read(info)
                    for info in bundle.infolist()
                    if info.filename != "info/johnny_castaway_libretro.info"
                }
            members["info/johnny_castaway_libretro.info"] = (
                b'display_version = "9.9.9"\n'
            )
            with zipfile.ZipFile(bundle_path, "w") as bundle:
                for name, payload in members.items():
                    bundle.writestr(name, payload)
            provenance = root / "BUILD-PROVENANCE.txt"
            provenance_text = provenance.read_text(encoding="utf-8")
            provenance.write_text(
                provenance_text.replace(
                    next(
                        line
                        for line in provenance_text.splitlines()
                        if line.startswith("Asset bundle SHA-256: ")
                    ),
                    "Asset bundle SHA-256: "
                    f"{hashlib.sha256(bundle_path.read_bytes()).hexdigest()}",
                ),
                encoding="utf-8",
            )
            result = self.run_checker(root)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("bundled core metadata hash", result.stdout)


class WebDiagnosticProbeSourceTests(unittest.TestCase):
    def test_scene_visual_only_requires_fixed_authentic_content(self) -> None:
        import argparse

        invalid = argparse.Namespace(
            scene_visual_only=True, chapter=None, content_dir=None
        )
        with self.assertRaisesRegex(
            SmokeFailure, "requires --chapter and --content-dir"
        ):
            validate_scene_visual_only_args(invalid)
        invalid.chapter = "fishing1"
        with self.assertRaisesRegex(
            SmokeFailure, "requires --chapter and --content-dir"
        ):
            validate_scene_visual_only_args(invalid)
        invalid.content_dir = pathlib.Path("fixture")
        validate_scene_visual_only_args(invalid)

    def test_scene_visual_only_result_has_explicit_reduced_schema(self) -> None:
        result = {
            "chapter": "fishing1",
            "scene_visual_only": True,
            "temporal_gameplay": {"passed": True},
        }
        complete_scene_visual_only_result(
            result,
            [pathlib.Path("game-01.png"), pathlib.Path("game-02.png")],
            ["a" * 64, "b" * 64],
            {"pageErrors": [], "rejections": []},
        )
        self.assertTrue(result["passed"])
        self.assertEqual(
            result["menu_navigation"],
            {"performed": False, "reason": "scene-visual-only"},
        )
        self.assertEqual(
            sorted(result["screenshots"]["gameplay_sequence_sha256"]),
            ["game-01.png", "game-02.png"],
        )
        self.assertNotIn("core_options_sha256", result["screenshots"])

        with self.assertRaisesRegex(SmokeFailure, "passing temporal evidence"):
            complete_scene_visual_only_result(
                {"temporal_gameplay": {"passed": False}},
                [pathlib.Path("game.png")],
                ["a" * 64],
                {},
            )

    def test_audio_unlock_is_visible_tracks_real_contexts_and_is_browser_tested(self) -> None:
        html = (ROOT / "web/index.html").read_text(encoding="utf-8")
        player = (ROOT / "web/jc-web-player.js").read_text(encoding="utf-8")
        harness = (ROOT / "tools/web_smoke_test.py").read_text(encoding="utf-8")
        provenance = (ROOT / "docs/PROVENANCE.md").read_text(encoding="utf-8")
        for marker in ("audio-state", "audio-unlock-button", "Enable Audio"):
            self.assertIn(marker, html)
        for marker in (
            "installAudioContextTracker",
            "class TrackedAudioContext extends AudioContextClass",
            "prototype.createBufferSource",
            "trackAudioContext(this)",
            "context.resume()",
            'context.state !== "closed"',
            "audioStateElement.dataset.contextCount",
            'addEventListener("pointerdown", unlockAudioFromGesture',
            'addEventListener("keydown", unlockAudioFromGesture',
            'addEventListener("touchstart", unlockAudioFromGesture',
        ):
            self.assertIn(marker, player)
        for marker in (
            "--test-audio-unlock",
            '"media.autoplay.default": 5 if block_autoplay else 0',
            '"media.autoplay.block-webaudio": block_autoplay',
            "audio-blocked-01.png",
            "canvas did not change while Web Audio autoplay was blocked",
            "Web AudioContext to resume after Enable Audio",
            "audio context to remain running after RetroArch reset",
        ):
            self.assertIn(marker, harness)
        self.assertIn("6316545c0c", provenance)

    def test_webgl_probe_is_query_gated_bounded_and_metadata_only(self) -> None:
        player = (ROOT / "web/jc-web-player.js").read_text(encoding="utf-8")
        start = player.index("function installWebGLSmokeProbe()")
        end = player.index("installWebGLSmokeProbe();", start)
        probe = player[start:end]
        self.assertIn('has("smoke")', probe)
        self.assertIn("window.__jcWebGLProbe?.installed", probe)
        self.assertIn("crypto.getRandomValues(randomSalt)", probe)
        self.assertNotIn("randomSalt:", probe)
        self.assertIn("Math.min(64, bytes.length)", probe)
        for marker in (
            "texImage2D",
            "texSubImage2D",
            "drawArrays",
            "drawElements",
            "clear",
            "webglcontextlost",
            "webglcontextrestored",
            "windowDistinctSampledVideoUploads",
            "windowRollingUploadSignature",
        ):
            self.assertIn(marker, probe)
        for forbidden in (
            "probe.pixels",
            "probe.frames",
            "probe.uploadBytes",
            "window.__jcWebGLPixels",
            "window.__jcWebGLFrames",
            ".push(bytes",
        ):
            self.assertNotIn(forbidden, probe)

        harness = (ROOT / "tools/web_smoke_test.py").read_text(encoding="utf-8")
        for marker in (
            "__jcResetWebGLProbe",
            "__jcStartForSmoke",
            '"webgl": webgl_metrics',
            '"distinct_sampled_video_uploads"',
            '"context_lost_events"',
        ):
            self.assertIn(marker, harness)
        self.assertNotIn('import("./jc-web-player.js")', harness)


class WebSmokeFrameQualityTests(unittest.TestCase):
    def test_color_key_variants_are_classified_as_magenta(self) -> None:
        pixels = bytes(
            (
                0xA8, 0x00, 0xA8,
                0xAD, 0x00, 0xAD,
                0xFF, 0x00, 0xFF,
                0x20, 0x80, 0xC0,
            )
        )
        quality = frame_quality((4, 1, 3, pixels))
        self.assertEqual(quality["magenta_ratio"], 0.75)
        self.assertEqual(quality["meaningful_ratio"], 0.25)

    def test_non_key_palette_colors_remain_meaningful(self) -> None:
        pixels = bytes(
            (
                0xFF, 0x69, 0xB4,
                0xEE, 0x82, 0xEE,
                0x80, 0x00, 0x80,
                180, 30, 150,
                0, 0, 0,
            )
        )
        quality = frame_quality((5, 1, 3, pixels))
        self.assertEqual(quality["magenta_ratio"], 0.0)
        self.assertEqual(quality["meaningful_ratio"], 0.8)

    def test_small_color_key_accent_does_not_dominate(self) -> None:
        pixels = bytes((0xA8, 0, 0xA8) + (0x20, 0x80, 0xC0) * 9)
        quality = frame_quality((10, 1, 3, pixels))
        self.assertAlmostEqual(quality["magenta_ratio"], 0.1)
        self.assertAlmostEqual(quality["meaningful_ratio"], 0.9)
        self.assertEqual(quality["renderer_key_component_pixels"], 1)
        self.assertFalse(frame_has_color_key_failure(quality))

    def test_residual_renderer_key_rectangle_fails_below_half_frame(self) -> None:
        width = 100
        height = 80
        pixels = bytearray((0x20, 0x80, 0xC0) * width * height)
        for y_position in range(20, 28):
            for x_position in range(10, 50):
                pixel = (y_position * width + x_position) * 3
                pixels[pixel : pixel + 3] = bytes((0xAD, 0, 0xAD))
        quality = frame_quality((width, height, 3, bytes(pixels)))
        self.assertAlmostEqual(quality["magenta_ratio"], 0.04)
        self.assertEqual(quality["renderer_key_rect_pixels"], 320)
        self.assertEqual(quality["renderer_key_rect_width"], 40)
        self.assertEqual(quality["renderer_key_rect_height"], 8)
        self.assertEqual(quality["renderer_key_component_pixels"], 320)
        self.assertEqual(quality["renderer_key_component_width"], 40)
        self.assertEqual(quality["renderer_key_component_height"], 8)
        self.assertTrue(frame_has_color_key_failure(quality))

    def test_legitimate_pink_rectangle_does_not_trip_sentinel_gate(self) -> None:
        width = 100
        height = 80
        pixels = bytearray((0x20, 0x80, 0xC0) * width * height)
        for y_position in range(20, 28):
            for x_position in range(10, 50):
                pixel = (y_position * width + x_position) * 3
                pixels[pixel : pixel + 3] = bytes((0xFF, 0x69, 0xB4))
        quality = frame_quality((width, height, 3, bytes(pixels)))
        self.assertEqual(quality["renderer_key_ratio"], 0.0)
        self.assertEqual(quality["renderer_key_rect_pixels"], 0)
        self.assertEqual(quality["renderer_key_component_pixels"], 0)
        self.assertFalse(frame_has_color_key_failure(quality))


if __name__ == "__main__":
    unittest.main(verbosity=2)
