"""Recording manifest selection and safe, accurate extraction arguments."""
import importlib.util
import json
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

    def test_dry_run_writes_nothing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            video = root / "test.mp4"
            video.write_bytes(b"fixture")
            manifest = dict(version=1, video=video.name, complete=True,
                            chapters=[dict(id=1, title="menu", screen="main_menu", start_us=0, end_us=100000)])
            Path(str(video) + ".json").write_text(json.dumps(manifest))
            target = root / "clips"
            self.assertEqual(recording.main(["extract", str(video), "--output-dir", str(target), "--dry-run"]), 0)
            self.assertFalse(target.exists())


if __name__ == "__main__":
    unittest.main()
