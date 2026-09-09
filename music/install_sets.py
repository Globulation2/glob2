"""Encode the curated WAV trios with libsndfile and verify their decoded lengths.

Usage: python3 music/install_sets.py
Requires libsndfile with Ogg/Vorbis support (e.g. Homebrew libsndfile on macOS).
"""
from pathlib import Path
import ctypes as C
import ctypes.util
import json
import wave
import sys
import tempfile
import os

ROOT=Path(__file__).resolve().parent
SETS={
 'seedling': ('Seedling', 'Organic electronic', 'seedling', ('a1-calm','a2-building','a3-combat')),
 'bramble-dance': ('Bramble Dance', 'Marimba, bamboo flute and hand drums', 'contrasts/bramble-dance', ('calm','building','combat')),
 'velvet-orbit': ('Velvet Orbit', 'Spacious electric piano and broken beats', 'contrasts/velvet-orbit', ('calm','building','combat')),
 'tidepool': ('Tidepool', 'Caribbean-inspired organic electronic', 'tidepool', ('calm','building','combat')),
}

class Info(C.Structure):
    _fields_ = [('frames', C.c_int64), ('samplerate', C.c_int), ('channels', C.c_int),
                ('format', C.c_int), ('sections', C.c_int), ('seekable', C.c_int)]


def load_encoder():
    library = ctypes.util.find_library('sndfile')
    if not library and Path('/opt/homebrew/lib/libsndfile.dylib').exists():
        library = '/opt/homebrew/lib/libsndfile.dylib'
    if not library:
        raise RuntimeError('Install libsndfile with Ogg/Vorbis support first.')
    lib = C.CDLL(library)
    lib.sf_open.argtypes = [C.c_char_p, C.c_int, C.POINTER(Info)]
    lib.sf_open.restype = C.c_void_p
    for method in ('sf_writef_short', 'sf_readf_short'):
        function = getattr(lib, method)
        function.argtypes = [C.c_void_p, C.POINTER(C.c_short), C.c_int64]
        function.restype = C.c_int64
    lib.sf_close.argtypes = [C.c_void_p]
    lib.sf_close.restype = C.c_int
    lib.sf_strerror.argtypes = [C.c_void_p]
    lib.sf_strerror.restype = C.c_char_p
    return lib


def read_pcm(path):
    with wave.open(str(path)) as source:
        if (source.getnchannels(), source.getsampwidth(), source.getframerate()) != (2, 2, 44100):
            raise ValueError(f'{path}: expected stereo 44.1 kHz PCM16')
        frames = source.getnframes()
        raw = source.readframes(frames)
    if frames <= 0 or len(raw) != frames * 4:
        raise ValueError(f'{path}: empty or truncated WAV')
    return frames, (C.c_short * (len(raw) // 2)).from_buffer_copy(raw)


def close_file(lib, handle):
    if lib.sf_close(handle) != 0:
        raise RuntimeError('Could not close audio file')


def encode_track(lib, frames, samples, target):
    info = Info(0, 44100, 2, 0x200060, 0, 0)  # Ogg/Vorbis
    handle = lib.sf_open(os.fsencode(target), 0x20, C.byref(info))
    if not handle:
        raise RuntimeError(lib.sf_strerror(None).decode())
    try:
        if lib.sf_writef_short(handle, samples, frames) != frames:
            raise RuntimeError(f'{target}: incomplete audio write')
    finally:
        close_file(lib, handle)

    decoded = Info()
    handle = lib.sf_open(os.fsencode(target), 0x10, C.byref(decoded))
    if not handle:
        raise RuntimeError(lib.sf_strerror(None).decode())
    try:
        if (decoded.frames, decoded.samplerate, decoded.channels, decoded.format) != (frames, 44100, 2, 0x200060):
            raise ValueError(f'{target}: invalid encoded audio format or length')
        block = (C.c_short * (8192 * 2))()
        total = peak = 0
        while True:
            count = lib.sf_readf_short(handle, block, 8192)
            if count < 0:
                raise RuntimeError(f'{target}: audio decode failed')
            if count == 0:
                break
            total += count
            peak = max(peak, max(abs(sample) for sample in block[:count * 2]))
        if total != frames or not 0 < peak < 32767:
            raise ValueError(f'{target}: incomplete, silent or clipped audio')
    finally:
        close_file(lib, handle)
    return {'frames': frames, 'seconds': frames / 44100, 'decoded_peak': peak / 32768}


def install_trio(lib, sources, destination):
    if len(sources) != 3:
        raise ValueError('A music set requires exactly three tracks')
    pcm = [read_pcm(path) for path in sources]
    if len({frames for frames, _ in pcm}) != 1:
        raise ValueError('Music set tracks must have matching lengths')
    destination.mkdir(parents=True, exist_ok=True)
    # Validate every encoded track before replacing any installed audio.
    with tempfile.TemporaryDirectory(prefix='.encode-', dir=destination) as temporary:
        staged = [Path(temporary) / f'a{index}.ogg' for index in range(1, 4)]
        records = [encode_track(lib, frames, samples, target)
                   for (frames, samples), target in zip(pcm, staged)]
        for target in staged:
            target.replace(destination / target.name)
    return records


def main():
    selected = set(sys.argv[1:]) or set(SETS)
    if selected - set(SETS):
        raise SystemExit('Unknown music set: ' + ', '.join(sorted(selected - set(SETS))))
    lib = load_encoder()
    manifest_path = ROOT / 'installed-sets.json'
    manifest = json.loads(manifest_path.read_text()) if manifest_path.exists() else {}
    manifest = {key: value for key, value in manifest.items() if key in SETS}
    for key, (title, style, source, names) in SETS.items():
        if key not in selected:
            continue
        destination = ROOT.parent / 'data/zik' / key
        sources = [ROOT / source / f'{name}.wav' for name in names]
        records = install_trio(lib, sources, destination)
        for index, (path, record) in enumerate(zip(sources, records), 1):
            record['file'] = str((destination / f'a{index}.ogg').relative_to(ROOT.parent))
            record['source'] = str(path.relative_to(ROOT.parent))
        manifest[key] = {'title': title, 'style': style, 'tracks': records}
        print(key, 'encoded and verified all three tracks', flush=True)
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    main()
