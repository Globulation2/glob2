# SPDX-License-Identifier: GPL-3.0-or-later
import array
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
import wave
from glob2music.community import convert, inspect, tag_opus, comments, decode, RATE


class CommunityTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.metadata = dict(
            id="4f59b80a-a421-4237-8a96-c05e99a9a554",
            origin="https://music.example",
            title="Moss & 雨",
            artist="Composer",
            description="A shared timeline",
            license="CC0-1.0",
            credits="Composer",
            sources=[],
            tags=["forest"],
            aiGenerated=False,
        )
        self.sources = {}
        for index, mood in enumerate(("calm", "building", "combat")):
            path = self.root / f"{mood}.wav"
            with wave.open(str(path), "wb") as f:
                f.setparams((1, 2, 24000, 0, "NONE", "none"))
                f.writeframes(
                    array.array(
                        "h",
                        [
                            int(2500 * ((n % 120) / 60 - 1))
                            for n in range(24000 * 10 + index * 120)
                        ],
                    ).tobytes()
                )
            self.sources[mood] = str(path)

    def tearDown(self):
        self.tmp.cleanup()

    def test_explicit_repair_and_roundtrip(self):
        found = inspect(self.sources, self.root / "inspection")
        self.assertFalse(found["equal"])
        with self.assertRaisesRegex(ValueError, "explicitly"):
            convert(self.sources, self.metadata, {}, self.root / "reject")
        for repair, expected in [("trim", 480000), ("pad", 480480)]:
            out = self.root / repair
            result = convert(self.sources, self.metadata, {"repair": repair}, out)
            self.assertEqual(result["frames"], expected)
            for track in result["tracks"]:
                probe = json.loads(
                    subprocess.check_output(
                        [
                            "ffprobe",
                            "-v",
                            "error",
                            "-show_streams",
                            "-of",
                            "json",
                            str(out / track["file"]),
                        ]
                    )
                )["streams"][0]
                self.assertEqual(probe["tags"]["GLOB2_RELEASE"], self.metadata["id"])
                self.assertEqual(probe["tags"]["ALBUM"], self.metadata["title"])
                self.assertEqual(
                    int(probe["duration_ts"]), expected + 9912
                )  # circular pre-skip includes encoder delay

    def test_large_comment_packet_preserves_decode(self):
        out = self.root / "tagged"
        convert(self.sources, self.metadata, {"repair": "trim"}, out)
        path = out / "a1.opus"
        before = subprocess.check_output(
            ["ffmpeg", "-v", "error", "-i", str(path), "-f", "s16le", "-"]
        )
        # Exercise continued comment pages, not just short text tags.
        metadata = {**self.metadata, "description": "x" * 100000}
        tag_opus(path, comments(metadata, "calm", 480000, None))
        after = subprocess.check_output(
            ["ffmpeg", "-v", "error", "-i", str(path), "-f", "s16le", "-"]
        )
        self.assertEqual(before, after)

    def test_corrupt_input(self):
        Path(self.sources["calm"]).write_bytes(b"not audio")
        with self.assertRaises(ValueError):
            inspect(self.sources, self.root / "bad")

    def test_common_containers_and_multistream_rejection(self):
        for extension, codec in [
            ("flac", "flac"),
            ("mp3", "libmp3lame"),
            ("m4a", "aac"),
            ("ogg", "libvorbis"),
            ("opus", "libopus"),
        ]:
            with self.subTest(format=extension):
                target = self.root / ("source." + extension)
                subprocess.run(
                    [
                        "ffmpeg",
                        "-v",
                        "error",
                        "-y",
                        "-i",
                        self.sources["calm"],
                        "-c:a",
                        codec,
                        str(target),
                    ],
                    check=True,
                )
                frames = decode(target, self.root / "decoded.pcm")
                self.assertGreaterEqual(frames, 480000)
                self.assertLess(
                    frames, 482000
                )  # source formats can contain encoder padding
        multi = self.root / "multi.mka"
        subprocess.run(
            [
                "ffmpeg",
                "-v",
                "error",
                "-y",
                "-i",
                self.sources["calm"],
                "-map",
                "0:a",
                "-map",
                "0:a",
                "-c:a",
                "flac",
                str(multi),
            ],
            check=True,
        )
        with self.assertRaisesRegex(ValueError, "exactly one"):
            decode(multi, self.root / "multi.pcm")
        playlist = self.root / "playlist.txt"
        playlist.write_text("ffconcat version 1.0\nfile 'calm.wav'\n")
        with self.assertRaises(ValueError):
            decode(playlist, self.root / "playlist.pcm")

    def test_embedded_cover(self):
        # An ordinary single-image input; output must be a bounded JPEG picture.
        cover = self.root / "cover.png"
        subprocess.run(
            [
                "ffmpeg",
                "-v",
                "error",
                "-y",
                "-f",
                "lavfi",
                "-i",
                "color=c=green:s=80x60",
                "-frames:v",
                "1",
                str(cover),
            ],
            check=True,
        )
        out = self.root / "art"
        convert(self.sources, self.metadata, {"repair": "trim"}, out, cover)
        self.assertLess((out / "cover.jpg").stat().st_size, 256 * 1024)
        probe = json.loads(
            subprocess.check_output(
                [
                    "ffprobe",
                    "-v",
                    "error",
                    "-show_streams",
                    "-of",
                    "json",
                    str(out / "a1.opus"),
                ]
            )
        )
        images = [s for s in probe["streams"] if s["codec_type"] == "video"]
        self.assertEqual((images[0]["width"], images[0]["height"]), (512, 512))

    def test_optional_mastering(self):
        try:
            import pyloudnorm
        except ImportError:
            self.skipTest("optional mastering dependencies unavailable")
        result = convert(
            self.sources,
            self.metadata,
            {"repair": "trim", "master": True},
            self.root / "mastered",
        )
        self.assertEqual(result["frames"], 480000)
        self.assertEqual(len(result["mastering"]), 3)
        for report in result["mastering"]:
            self.assertLessEqual(report["true_peak_dbtp"], -1)
