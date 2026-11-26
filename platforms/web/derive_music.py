#!/usr/bin/env python3
"""Lower-bitrate copies of the browser's music, checked in under platforms/web/assets/music/<N>k/.

A size-limited browser edition (buildsys/edition.py music_bitrate) ships these in
place of the original soundtrack and the menu music, so its build needs no FFmpeg.
Each copy is re-encoded from the shipped file with tools/encode_music.py at N kbps:
the in-game moods loop (--loop) and keep exactly the original's frame count, so the
mixer still crossfades them at one playback position. sources.json records each
copy's source and both digests; the build ignores a copy whose source changed, and
test/build_system/test_web_assets.py fails until it is regenerated:

    python3 platforms/web/derive_music.py --bitrate 24
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
import encode_music  # noqa: E402

# Runtime music a single-set edition ships, and whether the file loops in game.
MUSIC = {
    'content/data/zik/original/a1.opus': True,
    'content/data/zik/original/a2.opus': True,
    'content/data/zik/original/a3.opus': True,
    'content/data/zik/intro.opus': False,
    'content/data/zik/menu.opus': False,
}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--bitrate', type=int, required=True, help='stereo kbps, below the shipped 48')
    args = parser.parse_args()
    if not 6 <= args.bitrate < 48:
        parser.error('--bitrate must be from 6 to 47 kbps')
    directory = ROOT / 'platforms/web/assets/music' / f'{args.bitrate}k'
    recorded = {}
    for source, loop in MUSIC.items():
        name = source.removeprefix('content/data/zik/').replace('/', '-')
        target = directory / name
        record = encode_music.encode(ROOT / source, target, loop=loop, bitrate=f'{args.bitrate}k')
        recorded[name] = dict(source=source, sha256=digest(ROOT / source), output_sha256=digest(target),
                              bitrate=args.bitrate, loop=loop, frames=record.get('frames'),
                              recipe=encode_music.recipe(f'{args.bitrate}k'))
        print(f'{target.relative_to(ROOT)}: {target.stat().st_size} bytes')
    (directory / 'sources.json').write_text(json.dumps(recorded, indent=2, sort_keys=True) + '\n')


if __name__ == '__main__':
    main()
