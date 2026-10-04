#!/usr/bin/env python3
"""Encode runtime stereo music as 48 kbps VBR Ogg Opus and verify decoded frames."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

RECIPE = ['-af', 'aresample=48000,asetpts=N/SR/TB', '-c:a', 'libopus', '-b:a', '48k', '-vbr', 'on', '-application', 'audio',
          '-compression_level', '10', '-ar', '48000', '-ac', '2']
RATE = 48000


def encode(source, target, expected_frames=None):
    source, target = Path(source), Path(target)
    if target.suffix != '.opus' or source.resolve() == target.resolve():
        raise ValueError('Output must be a distinct .opus file')
    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.opus-', dir=target.parent) as directory:
        staged = Path(directory) / target.name
        subprocess.run(['ffmpeg', '-v', 'error', '-nostdin', '-y', '-i', str(source),
                        *RECIPE, str(staged)], check=True)
        # Decode to disk rather than keeping a full soundtrack in memory.
        pcm = Path(directory) / 'decoded.pcm'
        subprocess.run(['ffmpeg', '-v', 'error', '-nostdin', '-xerror', '-i', str(staged),
                        '-f', 's16le', '-c:a', 'pcm_s16le', str(pcm)], check=True)
        size = pcm.stat().st_size
        frames = size // 4
        if not frames or size % 4 or (expected_frames is not None and frames != expected_frames):
            raise ValueError(f'{target}: invalid decoded length {frames}, expected {expected_frames}')
        probe = json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-show_streams',
                                                    '-of', 'json', str(staged)], text=True))
        streams = probe['streams']
        if len(streams) != 1 or streams[0]['codec_name'] != 'opus' or streams[0]['channels'] != 2:
            raise ValueError('Expected a single stereo Opus stream')
        record = dict(source=str(source), file=str(target), frames=frames, seconds=frames / RATE,
                      bytes=staged.stat().st_size, sample_rate=RATE, recipe=RECIPE,
                      encoder=subprocess.check_output(['ffmpeg', '-version'], text=True).splitlines()[0])
        staged.replace(target)
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('target', type=Path)
    parser.add_argument('--expected-frames', type=int)
    parser.add_argument('--metadata', type=Path)
    args = parser.parse_args()
    record = encode(args.source, args.target, args.expected_frames)
    if args.metadata: args.metadata.write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps(record, indent=2))


if __name__ == '__main__': main()
