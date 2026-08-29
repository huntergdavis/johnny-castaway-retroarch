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
    EARLY_CHAPTER_ACTOR_REGIONS,
    EARLY_CHAPTER_CAPTURE_INTERVAL_SECONDS,
    EARLY_CHAPTER_FIRST_CAPTURE_DELAY_SECONDS,
    EARLY_CHAPTER_MINIMUM_ACTOR_MOTION_RATIO,
    SmokeFailure,
    analyze_late_ending_decoded_frames,
    chapter_start_log_count,
    complete_scene_visual_only_result,
    diagnostics_are_clean_running,
    evaluate_strict_audio_window,
    frame_has_color_key_failure,
    frame_quality,
    late_ending_color_metrics,
    late_ending_signature_failures,
    public_distribution_evidence,
    region_change_ratio,
    validate_early_chapter_motion_args,
    validate_late_ending_args,
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
    def test_early_chapter_motion_requires_fixed_authentic_content(self) -> None:
        import argparse

        validate_early_chapter_motion_args(
            argparse.Namespace(test_early_chapter_motion=False)
        )
        for chapter, content_dir in (
            (None, pathlib.Path("fixture")),
            ("stand16", None),
            ("fishing1", pathlib.Path("fixture")),
        ):
            invalid = argparse.Namespace(
                test_early_chapter_motion=True,
                chapter=chapter,
                content_dir=content_dir,
            )
            with self.assertRaisesRegex(
                SmokeFailure, "requires --chapter stand15 or stand16"
            ):
                validate_early_chapter_motion_args(invalid)
        for chapter in ("stand15", "stand16"):
            validate_early_chapter_motion_args(
                argparse.Namespace(
                    test_early_chapter_motion=True,
                    chapter=chapter,
                    content_dir=pathlib.Path("fixture"),
                )
            )

    def test_early_chapter_motion_source_is_reset_scoped_and_recorded(self) -> None:
        harness = (ROOT / "tools/web_smoke_test.py").read_text(encoding="utf-8")
        for marker in (
            "--test-early-chapter-motion",
            'driver.click(driver.find("#reset"))',
            "chapter_start_log_count(current) > reset_log_count_before",
            '"reset_before_capture": True',
            '"chapter_start_log_count_before_reset"',
            '"chapter_start_log_count_after_reset"',
            '"first_capture_delay_seconds"',
            '"capture_interval_seconds"',
            '"actor_region"',
            '"actor_change_ratios"',
            '"minimum_actor_motion_ratio"',
        ):
            self.assertIn(marker, harness)

        player = (ROOT / "web/jc-web-player.js").read_text(encoding="utf-8")
        self.assertIn('typeof moduleInstance._cmd_reset === "function"', player)
        self.assertIn("moduleInstance._cmd_reset();", player)
        self.assertIn('sendCommand("RESET")', player)

    def test_late_ending_requires_authentic_johnny1(self) -> None:
        import argparse

        valid = argparse.Namespace(
            test_late_ending=True,
            chapter="johnny1",
            content_dir=pathlib.Path("fixture"),
        )
        validate_late_ending_args(valid)
        for chapter, content_dir in (
            (None, pathlib.Path("fixture")),
            ("fishing1", pathlib.Path("fixture")),
            ("johnny1", None),
        ):
            invalid = argparse.Namespace(
                test_late_ending=True,
                chapter=chapter,
                content_dir=content_dir,
            )
            with self.assertRaisesRegex(
                SmokeFailure, "requires --chapter johnny1 and --content-dir"
            ):
                validate_late_ending_args(invalid)

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
        self.assertIn('audio_latency = "384"', player)
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


class WebSmokeEarlyChapterMotionTests(unittest.TestCase):
    def test_public_distribution_evidence_binds_public_files_only(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jc-web-evidence-") as temporary:
            dist = pathlib.Path(temporary)
            (dist / "BUILD-PROVENANCE.txt").write_text(
                "Johnny Castaway commit: " + "a" * 40 + " (clean)\n",
                encoding="utf-8",
            )
            (dist / "index.html").write_text("first", encoding="utf-8")
            before = public_distribution_evidence(dist)
            local_content = dist / "local-content"
            local_content.mkdir()
            (local_content / "RESOURCE.MAP").write_bytes(b"private")
            after_private = public_distribution_evidence(dist)
            self.assertEqual(before, after_private)
            (dist / "index.html").write_text("second", encoding="utf-8")
            after_public = public_distribution_evidence(dist)
            self.assertNotEqual(
                before["public_tree_sha256"], after_public["public_tree_sha256"]
            )
            self.assertEqual(after_public["johnny_commit"], "a" * 40)
            self.assertEqual(after_public["tree_state"], "clean")

    def test_fixed_chapter_start_logs_prove_reset_completion(self) -> None:
        self.assertEqual(chapter_start_log_count(None), 0)
        self.assertEqual(chapter_start_log_count({"consoleErrors": "bad"}), 0)
        self.assertEqual(
            chapter_start_log_count(
                {
                    "consoleErrors": [
                        "RetroArch startup",
                        "RetroArch: [libretro INFO] Johnny Castaway chapter: first",
                        "RetroArch: [libretro INFO] Johnny Castaway chapter: second",
                    ]
                }
            ),
            2,
        )

    def test_post_reset_diagnostics_must_be_clean_and_running(self) -> None:
        clean = {
            "status": "Running. Use RetroArch menu to inspect core options.",
            "statusError": False,
            "pageErrors": [],
            "rejections": [],
        }
        self.assertTrue(diagnostics_are_clean_running(clean))
        for changed in (
            {"status": "Ready: files selected"},
            {"statusError": True},
            {"pageErrors": ["boom"]},
            {"rejections": ["boom"]},
        ):
            candidate = dict(clean)
            candidate.update(changed)
            self.assertFalse(diagnostics_are_clean_running(candidate))
        self.assertFalse(diagnostics_are_clean_running(None))

    def test_actor_regions_are_normalized_and_require_local_motion(self) -> None:
        self.assertEqual(
            EARLY_CHAPTER_ACTOR_REGIONS["stand16"],
            (500 / 640, 220 / 480, 580 / 640, 330 / 480),
        )
        self.assertEqual(
            EARLY_CHAPTER_ACTOR_REGIONS["stand15"],
            (270 / 640, 210 / 480, 350 / 640, 335 / 480),
        )
        self.assertEqual(EARLY_CHAPTER_FIRST_CAPTURE_DELAY_SECONDS, 0.25)
        self.assertEqual(EARLY_CHAPTER_CAPTURE_INTERVAL_SECONDS, 0.5)
        self.assertEqual(EARLY_CHAPTER_MINIMUM_ACTOR_MOTION_RATIO, 0.005)

        width, height, channels = 640, 480, 3
        baseline = bytes(width * height * channels)
        outside_motion = bytearray(baseline)
        for y_position in range(20, 30):
            for x_position in range(20, 30):
                pixel = (y_position * width + x_position) * channels
                outside_motion[pixel : pixel + channels] = b"\xff\xff\xff"

        original = (width, height, channels, baseline)
        outside_changed = (width, height, channels, bytes(outside_motion))
        for chapter, x_position in (("stand15", 300), ("stand16", 510)):
            actor_motion = bytearray(baseline)
            for y_position in range(230, 240):
                for changed_x in range(x_position, x_position + 10):
                    pixel = (y_position * width + changed_x) * channels
                    actor_motion[pixel : pixel + channels] = b"\xff\xff\xff"
            actor_changed = (width, height, channels, bytes(actor_motion))
            actor_region = EARLY_CHAPTER_ACTOR_REGIONS[chapter]
            self.assertGreaterEqual(
                region_change_ratio(original, actor_changed, actor_region),
                EARLY_CHAPTER_MINIMUM_ACTOR_MOTION_RATIO,
            )
            self.assertEqual(
                region_change_ratio(original, outside_changed, actor_region),
                0.0,
            )


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


class WebSmokeStrictAudioTests(unittest.TestCase):
    @staticmethod
    def audio_window(**overrides: object) -> dict[str, object]:
        window: dict[str, object] = {
            "installed": True,
            "contexts": [{"state": "running", "sampleRate": 48000}],
            "observedWindowElapsedMs": 5000.0,
            "windowQueuedBuffers": 500,
            "windowEndedBuffers": 450,
            "windowFramesQueued": 240000,
            "windowScheduledSeconds": 5.0,
            "windowBufferFramesMin": 480,
            "windowBufferFramesMax": 480,
            "windowPositiveGapCount": 0,
            "windowMaxPositiveGapMs": 0.0,
            "windowMaxQueueIntervalMs": 60.0,
        }
        window.update(overrides)
        return window

    def test_continuous_window_requires_strict_cadence_and_zero_gaps(self) -> None:
        metrics, failures = evaluate_strict_audio_window(
            self.audio_window(), "gameplay"
        )
        self.assertEqual(failures, [])
        self.assertEqual(metrics["mode"], "continuous")
        self.assertEqual(metrics["scheduled_to_wall_ratio"], 1.0)
        self.assertEqual(metrics["positive_gap_count"], 0)

        _, failures = evaluate_strict_audio_window(
            self.audio_window(
                windowScheduledSeconds=5.11,
                windowEndedBuffers=449,
                windowPositiveGapCount=1,
                windowMaxPositiveGapMs=0.25,
            ),
            "gameplay",
        )
        self.assertTrue(
            any("scheduled duration is inconsistent" in failure for failure in failures)
        )
        self.assertTrue(any("ended only 449/500" in failure for failure in failures))
        self.assertTrue(any("is not gap-free" in failure for failure in failures))

    def test_menu_accepts_exact_silence_and_rejects_partial_scheduling(self) -> None:
        silence = self.audio_window(
            windowQueuedBuffers=0,
            windowEndedBuffers=0,
            windowFramesQueued=0,
            windowScheduledSeconds=0.0,
            windowBufferFramesMin=None,
            windowBufferFramesMax=None,
            windowMaxQueueIntervalMs=0.0,
        )
        metrics, failures = evaluate_strict_audio_window(
            silence, "stable menu", allow_paused=True
        )
        self.assertEqual(failures, [])
        self.assertEqual(metrics["mode"], "paused")

        partial = dict(silence)
        partial["windowEndedBuffers"] = 1
        _, failures = evaluate_strict_audio_window(
            partial, "stable menu", allow_paused=True
        )
        self.assertTrue(any("pause is partial" in failure for failure in failures))

        partial = dict(silence)
        partial["windowPositiveGapCount"] = 1
        partial["windowMaxPositiveGapMs"] = 0.5
        _, failures = evaluate_strict_audio_window(
            partial, "stable menu", allow_paused=True
        )
        self.assertTrue(
            any("pause recorded scheduler gaps" in failure for failure in failures)
        )


class WebSmokeFrameQualityComponentTests(unittest.TestCase):
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


class WebSmokeLateEndingTests(unittest.TestCase):
    @staticmethod
    def clean_frame() -> tuple[int, int, int, bytes]:
        width = height = 100
        pixels = bytearray(width * height * 3)
        for y_position in range(20):
            for x_position in range(30):
                pixel = (y_position * width + x_position) * 3
                pixels[pixel : pixel + 3] = bytes((224, 24, 24))
        return width, height, 3, bytes(pixels)

    def test_clean_stable_title_sequence_passes(self) -> None:
        frame = self.clean_frame()
        evidence = analyze_late_ending_decoded_frames(
            [frame] * 4,
            [f"late-ending-{index:02d}.png" for index in range(4)],
            [str(index) * 64 for index in range(4)],
        )
        self.assertTrue(evidence["passed"])
        self.assertEqual(evidence["acceptance_failures"], [])
        metrics = late_ending_color_metrics(frame)
        self.assertGreaterEqual(metrics["black_ratio"], 0.70)
        self.assertGreaterEqual(metrics["bright_red_ratio"], 0.05)
        self.assertEqual(late_ending_signature_failures(metrics), [])

    def test_persistent_frog_clock_is_rejected(self) -> None:
        width, height, channels, source = self.clean_frame()
        pixels = bytearray(source)
        for x_position in range(38, 58):
            pixel = (20 * width + x_position) * channels
            pixels[pixel : pixel + 3] = bytes((16, 192, 32))
        failures = late_ending_signature_failures(
            late_ending_color_metrics((width, height, channels, bytes(pixels)))
        )
        self.assertIn("frog clock/saved overlay persists over the ending", failures)

    def test_purple_lower_band_is_rejected(self) -> None:
        width, height, channels, source = self.clean_frame()
        pixels = bytearray(source)
        for y_position in range(72, 74):
            for x_position in range(100):
                pixel = (y_position * width + x_position) * channels
                pixels[pixel : pixel + 3] = bytes((0xAD, 0, 0xAD))
        failures = late_ending_signature_failures(
            late_ending_color_metrics((width, height, channels, bytes(pixels)))
        )
        self.assertIn("lower band leaks purple/color-key pixels", failures)

    def test_blank_canvas_is_rejected(self) -> None:
        failures = late_ending_signature_failures(
            late_ending_color_metrics((100, 100, 3, bytes(100 * 100 * 3)))
        )
        self.assertIn("blank or lost canvas", failures)
        self.assertIn("ending lacks the bright-red title signature", failures)

    def test_materially_changing_title_sequence_is_rejected(self) -> None:
        width, height, channels, source = self.clean_frame()
        changed = bytearray(source)
        for y_position in range(50, 60):
            for x_position in range(50, 80):
                pixel = (y_position * width + x_position) * channels
                changed[pixel : pixel + 3] = bytes((24, 96, 192))
        frames = [self.clean_frame(), (width, height, channels, bytes(changed))]
        frames.extend([frames[-1], frames[-1]])
        with self.assertRaisesRegex(SmokeFailure, "not stable across the full canvas"):
            analyze_late_ending_decoded_frames(
                frames,
                [f"late-ending-{index:02d}.png" for index in range(4)],
                [str(index) * 64 for index in range(4)],
            )

if __name__ == "__main__":
    unittest.main(verbosity=2)
