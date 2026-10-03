"""Recording manifest selection and safe, accurate extraction arguments."""
import importlib.util
import contextlib
import io
import json
import os
import subprocess
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location("recording", Path(__file__).parents[1] / "tools/recording.py")
recording = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(recording)


class RecordingToolTests(unittest.TestCase):
    def args(self, *extra):
        return recording.parser().parse_args(["extract", "capture.mp4", "--output-dir", "clips", *extra])

    def test_selection_distinguishes_matches_eras_and_repeated_visits(self):
        chapters = [dict(id=1, screen="main_menu", dialog="", match=0, phase="menu"),
                    dict(id=2, screen="game_session", dialog="", match=1, phase="gameplay", era_start=10000),
                    dict(id=3, screen="game_session", dialog="in_game_main", match=1, phase="dialog"),
                    dict(id=4, screen="game_session", dialog="", match=1, phase="gameplay", era_start=10000),
                    dict(id=5, screen="game_session", dialog="", match=2, phase="gameplay", era_start=10000)]
        selected = recording.select_chapters({"chapters": chapters}, self.args("--match", "1", "--era-start", "10000"))
        self.assertEqual([c["id"] for c in selected], [2, 4])
        selected = recording.select_chapters({"chapters": chapters}, self.args("--dialog", "in_game_main"))
        self.assertEqual([c["id"] for c in selected], [3])

    def test_encoding_uses_exact_time_and_argument_arrays(self):
        chapter = dict(start_us=166667, end_us=483333, title='Menu; $(echo unsafe) "title"')
        cmd = recording.command(Path("footage with spaces.mp4"), Path("clip.mp4"), chapter, self.args())
        self.assertEqual(cmd[cmd.index("-ss") + 1], "0.166667")
        self.assertEqual(cmd[cmd.index("-t") + 1], "0.316666")
        self.assertIn("title=" + chapter["title"], cmd)
        self.assertIn("libx264", cmd)
        self.assertNotIn("-noaccurate_seek", cmd)
        self.assertIn("-n", cmd)

    def test_webm_and_copy_are_explicit(self):
        chapter = dict(start_us=0, end_us=1000000, title="gameplay")
        cmd = recording.command(Path("in.mp4"), Path("out.webm"), chapter, self.args("--format", "webm"))
        self.assertIn("libvpx-vp9", cmd)
        self.assertIn("libopus", cmd)
        cmd = recording.command(Path("in.mp4"), Path("out.mp4"), chapter, self.args("--copy"))
        self.assertIn("-noaccurate_seek", cmd)
        self.assertIn("copy", cmd)

    def test_invalid_manifest_intervals_and_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "test.mp4.json"
            for manifest in [dict(version=2), dict(version=1, video="../secret.mp4"),
                             dict(version=1, video="test.mp4", chapters=[dict(id=1, start_us=5, end_us=4)]),
                             dict(version=1, video="test.mp4", chapters=[dict(id=1, start_us=0, end_us=10), dict(id=2, start_us=5, end_us=20)])]:
                path.write_text(json.dumps(manifest))
                with self.assertRaises(ValueError):
                    recording.load_recording(path)

    def test_malformed_schema_is_reported_without_type_errors(self):
        chapter = dict(id=1, start_us=0, end_us=10, title="menu", screen="main_menu")
        base = dict(version=1, video="test.mp4", complete=True, chapters=[chapter])
        malformed = [[], dict(base, video=5), dict(base, chapters={}),
                     dict(base, chapters=[dict(chapter, start_us=False)]),
                     dict(base, chapters=[dict(chapter, id="1")]),
                     dict(base, chapters=[dict(chapter, title=None)]),
                     dict(base, duration_us=5)]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "test.mp4.json"
            for manifest in malformed:
                path.write_text(json.dumps(manifest))
                with self.assertRaises(ValueError):
                    recording.load_recording(path)

    def test_dry_run_writes_nothing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            video = root / "test.mp4"
            video.write_bytes(b"fixture")
            manifest = dict(version=1, video=video.name, complete=True,
                            chapters=[dict(id=1, title="menu", screen="main_menu", start_us=0, end_us=100000)])
            Path(str(video) + ".json").write_text(json.dumps(manifest))
            target = root / "clips"
            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                self.assertEqual(recording.main(["extract", str(video), "--output-dir", str(target), "--dry-run"]), 0)
            self.assertEqual(json.loads(stdout.getvalue())[0], "ffmpeg")
            self.assertFalse(target.exists())


@unittest.skipUnless(os.environ.get("GLOB2_RECORDING_FIXTURE") and os.environ.get("GLOB2_TEST_FFPROBE"),
                     "Set a real recording fixture and FFprobe for decoded media checks")
class EncodedRecordingTests(unittest.TestCase):
    def setUp(self):
        self.video, self.manifest = recording.load_recording(Path(os.environ["GLOB2_RECORDING_FIXTURE"]))
        self.encoder = os.environ.get("GLOB2_TEST_FFMPEG", "ffmpeg")

    def test_embedded_chapters_codecs_and_chronological_journal(self):
        probe = json.loads(subprocess.check_output([
            os.environ["GLOB2_TEST_FFPROBE"], "-v", "error", "-show_streams", "-show_chapters", "-of", "json", str(self.video)]))
        streams = {stream["codec_type"]: stream for stream in probe["streams"]}
        self.assertEqual(streams["video"]["codec_name"], "h264")
        self.assertEqual(streams["audio"]["codec_name"], "aac")
        self.assertLess(abs(float(streams["video"]["duration"]) - float(streams["audio"]["duration"])), .05)
        self.assertEqual(len(probe["chapters"]), len(self.manifest["chapters"]))
        for actual, expected in zip(probe["chapters"], self.manifest["chapters"]):
            self.assertEqual(actual["tags"]["title"], expected["title"])
            self.assertLessEqual(abs(float(actual["start_time"]) - expected["start_us"] / 1e6), 1 / self.manifest["fps"])
        times = [json.loads(line)["time_us"] for line in Path(str(self.video) + ".events.jsonl").read_text().splitlines()]
        self.assertEqual(times, sorted(times))

    def test_extracted_chapters_begin_with_the_expected_decoded_frame(self):
        # Palette comes from GameplayRecordingTest's real section fixture.
        palette = {"main_menu": (200, 20, 10), "multiplayer_game": (20, 200, 10), "end_game": (10, 20, 200)}
        with tempfile.TemporaryDirectory() as directory:
            for chapter in self.manifest["chapters"]:
                expected = palette.get(chapter["screen"])
                if expected is None:
                    continue
                output = Path(directory) / (str(chapter["id"]) + ".mp4")
                args = recording.parser().parse_args(["extract", str(self.video), "--output-dir", directory, "--ffmpeg", self.encoder])
                subprocess.run(recording.command(self.video, output, chapter, args), check=True)
                rgb = subprocess.check_output([self.encoder, "-v", "error", "-i", str(output), "-frames:v", "1", "-f", "rawvideo", "-pix_fmt", "rgb24", "pipe:1"])
                offset = ((self.manifest["height"] // 2) * self.manifest["width"] + self.manifest["width"] // 2) * 3
                self.assertEqual(len(rgb), self.manifest["width"] * self.manifest["height"] * 3)
                for actual, target in zip(rgb[offset:offset+3], expected):
                    self.assertLess(abs(actual - target), 15)


if __name__ == "__main__":
    unittest.main()
