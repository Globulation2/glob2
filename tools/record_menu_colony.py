#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Capture the real UI-free menu colony and encode its web background loop."""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def run(*args):
    subprocess.run([str(arg) for arg in args], cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--harness', type=Path)
    parser.add_argument('--ffmpeg', default='ffmpeg', help='FFmpeg executable')
    parser.add_argument('--encode-only', action='store_true', help='reuse an existing complete capture')
    parser.add_argument('--seconds', type=int, default=60)
    parser.add_argument('--frames', type=Path, default=ROOT / 'artifacts/menu-colony/frames')
    parser.add_argument('--keep-frames', action='store_true', help='retain all lossless capture frames')
    args = parser.parse_args()
    if args.seconds < 5:
        parser.error('record at least five seconds')
    frames = args.frames.resolve()
    frames.mkdir(parents=True, exist_ok=True)
    # 25 fps matches the menu simulation's 40 ms clock. Two extra seconds
    # dissolve the ending into the opening; the middle then follows naturally.
    if not args.encode_only:
        if not args.harness:
            parser.error('--harness is required unless --encode-only is supplied')
        run(args.harness.resolve(), 'record-colony', frames, (args.seconds + 2) * 25, 1600, 900)
    art = ROOT / 'platform/apps/web/src/art'
    run(args.ffmpeg, '-y', '-framerate', 25, '-i', frames / '%04d.bmp',
        '-filter_complex',
        f'[0:v]split=3[mid][tail][head];'
        f'[mid]trim=start=2:end={args.seconds},setpts=PTS-STARTPTS[m];'
        f'[tail]trim=start={args.seconds}:end={args.seconds + 2},setpts=PTS-STARTPTS[t];'
        '[head]trim=start=0:end=2,setpts=PTS-STARTPTS[h];'
        '[t][h]blend=all_expr=A*(1-T/2)+B*(T/2)[join];'
        '[m][join]concat=n=2:v=1:a=0,format=yuv420p[out]',
        '-map', '[out]', '-an', '-c:v', 'libx264', '-preset', 'slow', '-crf', 27,
        '-movflags', '+faststart', art / 'colony-loop.mp4')
    for width in (960, 1600):
        run(args.ffmpeg, '-y', '-i', frames / '0050.bmp', '-vf', f'scale={width}:-1',
            '-frames:v', 1, '-quality', 85, art / f'colony-{width}.webp')
    if not args.keep_frames:
        keep = {50, args.seconds * 25 // 2, (args.seconds + 2) * 25 - 1}
        for index in range((args.seconds + 2) * 25):
            if index not in keep:
                (frames / f'{index:04d}.bmp').unlink()


if __name__ == '__main__':
    main()
