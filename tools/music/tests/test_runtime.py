# SPDX-License-Identifier: GPL-3.0-or-later
"""Cross-language portable-file contract: actual Python encoder and C++ installer/player."""

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[3]


class RuntimeTest(unittest.TestCase):
    @unittest.skipUnless(
        shutil.which("g++") and shutil.which("pkg-config"),
        "C++ compiler and opusfile development package required",
    )
    def test_native_roundtrip(self):
        flags = subprocess.run(
            ["pkg-config", "--cflags", "--libs", "opusfile"],
            capture_output=True,
            text=True,
        )
        if flags.returncode:
            self.skipTest("opusfile development package required")
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            exe = tmp / "probe"
            subprocess.run(
                [
                    "g++",
                    "-std=c++20",
                    "-O2",
                    "-I" + str(ROOT / "src/audio"),
                    str(ROOT / "tools/music/tests/runtime_probe.cpp"),
                    str(ROOT / "src/audio/MusicStream.cpp"),
                    str(ROOT / "src/audio/MusicLibrary.cpp"),
                    *flags.stdout.split(),
                    "-lz",
                    "-o",
                    str(exe),
                ],
                check=True,
            )
            subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "tools/music/tests/make_community_fixture.py"),
                    str(tmp / "fixture"),
                ],
                check=True,
            )
            subprocess.run(
                [str(exe), str(tmp / "fixture"), str(tmp / "installed")], check=True
            )
            audio = (tmp / "fixture/a1.opus").read_bytes()
            cases = {
                "traversal": ["../a1.opus"],
                "absolute": ["/a1.opus"],
                "backslash": ["evil\\a1.opus"],
                "incomplete": ["set/a1.opus"],
                "duplicate": ["a1.opus", "a1.opus"],
                "mixed-moods": ["a1.opus", "a2.opus", "a3.opus"],
            }
            for name, members in cases.items():
                with self.subTest(attack=name):
                    archive = tmp / (name + ".zip")
                    with zipfile.ZipFile(archive, "w") as z:
                        for member in members:
                            z.writestr(member, audio)
                    subprocess.run(
                        [str(exe), str(archive), str(tmp / name), "--reject"],
                        check=True,
                    )
            link = tmp / "symlink.zip"
            with zipfile.ZipFile(link, "w") as z:
                member = zipfile.ZipInfo("a1.opus")
                member.create_system = 3
                member.external_attr = 0o120777 << 16
                z.writestr(member, "/etc/passwd")
            subprocess.run(
                [str(exe), str(link), str(tmp / "link"), "--reject"], check=True
            )
