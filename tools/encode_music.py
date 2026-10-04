#!/usr/bin/env python3
"""Encode runtime stereo music as 48 kbps VBR Ogg Opus and verify decoded frames."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import shutil
import struct

RECIPE = ['-af', 'aresample=48000,asetpts=N/SR/TB', '-c:a', 'libopus', '-b:a', '48k', '-vbr', 'on', '-application', 'audio',
          '-compression_level', '10', '-ar', '48000', '-ac', '2']
RATE = 48000


def _page_crc(page):
    page[22:26] = b'\0' * 4
    crc = 0
    for byte in page:
        crc ^= byte << 24
        for _ in range(8):
            crc = ((crc << 1) ^ (0x04C11DB7 if crc & 0x80000000 else 0)) & 0xffffffff
    struct.pack_into('<I', page, 22, crc)


def _packet_frames(packet):
    """Opus packet duration at 48 kHz (RFC 6716 section 3.1)."""
    toc = packet[0]
    config = toc >> 3
    if config >= 16:
        duration = 120 << (config & 3)
    elif config >= 12:
        duration = 480 << (config & 1)
    else:
        duration = 2880 if config & 3 == 3 else 480 << (config & 3)
    code = toc & 3
    count = 1 if code == 0 else 2 if code < 3 else packet[1] & 63
    if count == 0 or duration * count > 5760:
        raise ValueError('Invalid Opus packet duration')
    return duration * count


def _crop_loop(path, warm_frames, frames):
    """Crop circular codec history using RFC 7845 pre-skip and end granules.

    This consumes only our FFmpeg encoder's pages, which end at packet boundaries.
    Audio samples and timestamps inside retained packets remain unchanged.
    """
    data = path.read_bytes()
    result = bytearray()
    pos = decoded = 0
    end = None
    while pos < len(data):
        header = data[pos:pos + 27]
        if len(header) != 27 or header[:4] != b'OggS' or header[5] & 1:
            raise ValueError('Invalid or continued encoder page')
        lacing = data[pos + 27:pos + 27 + header[26]]
        body_start = pos + 27 + len(lacing)
        body = data[body_start:body_start + sum(lacing)]
        if len(body) != sum(lacing) or not lacing or lacing[-1] == 255:
            raise ValueError('Incomplete encoder page')
        page = bytearray(header + lacing + body)
        if body.startswith(b'OpusHead'):
            if len(body) < 19:
                raise ValueError('Invalid Opus header')
            skip = struct.unpack_from('<H', body, 10)[0] + warm_frames
            if skip > 65535:
                raise ValueError('Opus pre-skip exceeds its 16-bit field')
            struct.pack_into('<H', page, 27 + len(lacing) + 10, skip)
            end = skip + frames
            _page_crc(page)
        elif not body.startswith(b'OpusTags'):
            if end is None:
                raise ValueError('Missing Opus header')
            offset = packet_start = 0
            for segment_index, length in enumerate(lacing):
                offset += length
                if length == 255:
                    continue
                decoded += _packet_frames(body[packet_start:offset])
                packet_start = offset
                if decoded >= end:
                    page = bytearray(header + lacing[:segment_index + 1] + body[:offset])
                    page[5] |= 4
                    page[26] = segment_index + 1
                    struct.pack_into('<Q', page, 6, end)
                    _page_crc(page)
                    result.extend(page)
                    path.write_bytes(result)
                    return
        result.extend(page)
        pos = body_start + len(body)
    raise ValueError('Encoder output ended before the loop end')


def encode(source, target, expected_frames=None, loop=False):
    source, target = Path(source), Path(target)
    if target.suffix != '.opus' or source.resolve() == target.resolve():
        raise ValueError('Output must be a distinct .opus file')
    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.opus-', dir=target.parent) as directory:
        staged = Path(directory) / target.name
        warm_frames = 0
        input_args = ['-i', str(source)]
        if loop:
            # Feed the tail first so the codec has circular history at frame zero.
            raw = Path(directory) / 'source.f32'
            subprocess.run(['ffmpeg', '-v', 'error', '-nostdin', '-y', '-i', str(source),
                            '-af', 'aresample=48000,asetpts=N/SR/TB', '-ar', '48000', '-ac', '2',
                            '-f', 'f32le', str(raw)], check=True)
            source_frames = raw.stat().st_size // 8
            if not source_frames:
                raise ValueError('Cannot encode an empty loop')
            warm_frames = min(9600, source_frames)
            warm = Path(directory) / 'warm.f32'
            with raw.open('rb') as src, warm.open('wb') as dst:
                src.seek(-warm_frames * 8, 2)
                dst.write(src.read())
                src.seek(0)
                shutil.copyfileobj(src, dst)
                src.seek(0)
                dst.write(src.read(warm_frames * 8))
            input_args = ['-f', 'f32le', '-ar', '48000', '-ac', '2', '-i', str(warm)]
            if expected_frames is None:
                expected_frames = source_frames
        subprocess.run(['ffmpeg', '-v', 'error', '-nostdin', '-y', *input_args,
                        *RECIPE, str(staged)], check=True)
        if warm_frames:
            _crop_loop(staged, warm_frames, source_frames)
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
        record = dict(source=str(source), source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(), file=str(target), frames=frames, seconds=frames / RATE,
                      bytes=staged.stat().st_size, sample_rate=RATE, recipe=RECIPE,
                      loop_preroll_frames=warm_frames,
                      encoder=subprocess.check_output(['ffmpeg', '-version'], text=True).splitlines()[0])
        staged.replace(target)
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('target', type=Path)
    parser.add_argument('--expected-frames', type=int)
    parser.add_argument('--loop', action='store_true', help='encode circular history and trim it using pre-skip/end granules')
    parser.add_argument('--metadata', type=Path)
    args = parser.parse_args()
    record = encode(args.source, args.target, args.expected_frames, loop=args.loop)
    if args.metadata: args.metadata.write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps(record, indent=2))


if __name__ == '__main__': main()
