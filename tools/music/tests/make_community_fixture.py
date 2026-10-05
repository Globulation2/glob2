# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate synthetic, metadata-bearing music for browser/native integration tests."""

import argparse
import array
import json
import math
from pathlib import Path
import sys
import tempfile
import wave

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from glob2music.community import convert

parser = argparse.ArgumentParser()
parser.add_argument("output", type=Path)
parser.add_argument("--seconds", type=int, default=10)
args = parser.parse_args()
metadata = dict(
    id="4f59b80a-a421-4237-8a96-c05e99a9a554",
    origin="https://music.example",
    title="Moss lantern",
    artist="Test composer",
    description="Three synchronized arrangements for a forest colony.",
    license="CC0-1.0",
    credits="Synthetic test fixture",
    sources=[],
    tags=["forest"],
    aiGenerated=False,
)
with tempfile.TemporaryDirectory() as tmp:
    sources = {}
    for i, mood in enumerate(("calm", "building", "combat")):
        path = Path(tmp) / (mood + ".wav")
        with wave.open(str(path), "wb") as out:
            out.setparams((1, 2, 24000, 0, "NONE", "none"))
            one = array.array(
                "h",
                [
                    int(3500 * math.sin(2 * math.pi * (220 + i * 110) * n / 24000))
                    for n in range(24000)
                ],
            )
            if sys.byteorder != "little":
                one.byteswap()
            for _ in range(args.seconds):
                out.writeframesraw(one.tobytes())
        sources[mood] = str(path)
    convert(sources, metadata, {"repair": "none"}, args.output)
(args.output / "metadata.json").write_text(json.dumps(metadata))
