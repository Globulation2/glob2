# SPDX-License-Identifier: GPL-3.0-or-later
"""Audio types and frame-exact file IO: the ``Trio`` every recipe returns.

Pipeline role: a recipe (``sets/<id>/recipe.py``) builds three float arrays and
returns them as a ``Trio``; ``master.finish`` turns that into a mastered ``Trio``;
``write_trio`` alone writes the Ogg Vorbis files the game loads. Keeping one writer
means every set gets the same encoder, quality and frame-count guarantee.

Array convention, used throughout the package: ``float32`` (or float64 while
processing), shape ``(frames, 2)``, 44100 Hz, nominal full scale +-1.0. One array is
exactly one loop: frame ``n - 1`` is followed by frame ``0`` when the game wraps.

Decoding uses libsndfile (``soundfile``), which reads Vorbis through libvorbis and
reports the same PCM frame count as the game's ``ov_pcm_total``. Encoding uses
ffmpeg's libvorbis encoder, which writes an exact end granule position, so the
decoded length equals the array length to the frame (``write_ogg`` verifies this).
libsndfile's own Vorbis encoder is the fallback when ffmpeg is missing; it is also
frame-exact but must be fed in small blocks (large single writes crash 1.2.2).
"""
from dataclasses import dataclass, field
from pathlib import Path
import shutil
import struct
import subprocess

import numpy as np
import soundfile as sf

from .spec import CHANNELS, FILENAMES, MOODS, SAMPLE_RATE, DEFAULT_SPEC


class AudioFormatError(ValueError):
    """An array or file does not meet the trio format."""


def as_stereo(y, name='audio'):
    """Return ``y`` as a C-contiguous float32 ``(frames, 2)`` array.

    Mono input (1-D or one column) is duplicated to both channels. Anything else
    that is not two channels raises ``AudioFormatError``.
    """
    y = np.asarray(y)
    if y.ndim == 1:
        y = y[:, None]
    if y.ndim != 2:
        raise AudioFormatError(f'{name}: expected (frames, channels), got shape {y.shape}')
    if y.shape[1] == 1:
        y = np.repeat(y, 2, axis=1)
    if y.shape[1] != CHANNELS:
        raise AudioFormatError(f'{name}: expected {CHANNELS} channels, got {y.shape[1]}')
    if not np.all(np.isfinite(y)):
        raise AudioFormatError(f'{name}: contains NaN or infinity')
    return np.ascontiguousarray(y, dtype=np.float32)


@dataclass
class Trio:
    """Three position-aligned, equally long loops: calm, building and combat.

    This is the contract between recipes and the shared mastering/encoding stage.
    The constructor normalises each array with ``as_stereo`` and refuses unequal
    lengths, so a ``Trio`` is always structurally valid; whether it *sounds* valid is
    for ``glob2music.qa``.

    ``meta`` carries free-form provenance (seeds, source offsets, mastering report);
    ``master.finish`` adds a ``'master'`` entry and the build writes it next to the
    Oggs as ``build.json``.
    """

    calm: np.ndarray
    building: np.ndarray
    combat: np.ndarray
    sample_rate: int = SAMPLE_RATE
    meta: dict = field(default_factory=dict)

    def __post_init__(self):
        if self.sample_rate != SAMPLE_RATE:
            raise AudioFormatError(f'trio sample rate must be {SAMPLE_RATE}, got {self.sample_rate}')
        for mood in MOODS:
            setattr(self, mood, as_stereo(getattr(self, mood), mood))
        lengths = {mood: len(getattr(self, mood)) for mood in MOODS}
        if len(set(lengths.values())) != 1:
            raise AudioFormatError(f'trio moods differ in length: {lengths}')
        if lengths['calm'] == 0:
            raise AudioFormatError('trio is empty')

    @property
    def frames(self):
        """Loop length in frames (identical for all three moods)."""
        return len(self.calm)

    @property
    def seconds(self):
        return self.frames / self.sample_rate

    def __getitem__(self, mood):
        if mood not in MOODS:
            raise KeyError(mood)
        return getattr(self, mood)

    def items(self):
        """``(mood, array)`` pairs in file order."""
        return [(mood, getattr(self, mood)) for mood in MOODS]

    def map(self, fn, **meta):
        """A new ``Trio`` with ``fn(array, mood)`` applied to each mood; ``meta`` is
        merged into a copy of this trio's metadata."""
        out = {mood: fn(getattr(self, mood), mood) for mood in MOODS}
        return Trio(sample_rate=self.sample_rate, meta={**self.meta, **meta}, **out)


# ----------------------------------------------------------------------------- reading

def read_audio(path):
    """Decode any libsndfile-readable file to ``(float32 (frames, 2), sample_rate)``.

    No resampling happens here: callers that need 44.1 kHz must check the rate.
    """
    y, sr = sf.read(str(path), dtype='float32', always_2d=True)
    if y.shape[1] == 1:
        y = np.repeat(y, 2, axis=1)
    return np.ascontiguousarray(y[:, :2]), sr


def trio_paths(directory):
    """``{mood: Path}`` of ``a1.ogg``/``a2.ogg``/``a3.ogg`` inside ``directory``."""
    directory = Path(directory)
    return {mood: directory / FILENAMES[mood] for mood in MOODS}


def read_trio(directory):
    """Load a finished trio directory into a ``Trio`` (raises if lengths differ)."""
    arrays = {}
    for mood, path in trio_paths(directory).items():
        y, sr = read_audio(path)
        if sr != SAMPLE_RATE:
            raise AudioFormatError(f'{path}: sample rate {sr}, expected {SAMPLE_RATE}')
        arrays[mood] = y
    return Trio(meta={'source_dir': str(directory)}, **arrays)


def ogg_streams(path):
    """Describe the logical streams of an Ogg file by walking its pages.

    Returns a list of ``{'serial', 'codec', 'pages', 'last_granule'}`` dicts, in the
    order the streams start. The game requires exactly one Vorbis stream
    (``ov_streams() == 1``); chained or multiplexed files are refused. This is a
    pure-Python parse of the page headers (RFC 3533), so QA needs no ffprobe.
    Raises ``AudioFormatError`` if the file is not an Ogg stream at all.
    """
    data = Path(path).read_bytes()
    streams = {}            # serial -> index of its current entry in order
    order = []
    pos = 0
    while pos < len(data):
        if data[pos:pos + 4] != b'OggS':
            raise AudioFormatError(f'{path}: not an Ogg page at byte {pos}')
        if pos + 27 > len(data):
            raise AudioFormatError(f'{path}: truncated page header at byte {pos}')
        header_type = data[pos + 5]
        granule, serial, _seq, _crc, nseg = struct.unpack_from('<qIIIB', data, pos + 6)
        lacing = data[pos + 27:pos + 27 + nseg]
        body_start = pos + 27 + nseg
        body_len = sum(lacing)
        # A beginning-of-stream page always opens a new logical stream, even when a
        # chained file reuses the serial number of the previous link.
        if header_type & 0x02 or serial not in streams:
            body = data[body_start:body_start + 8]
            codec = 'vorbis' if body[:7] == b'\x01vorbis' else 'opus' if body[:8] == b'OpusHead' else 'other'
            key = len(order)
            streams[serial] = key
            order.append({'serial': serial, 'codec': codec, 'pages': 0, 'last_granule': -1,
                          'bos': bool(header_type & 0x02)})
        entry = order[streams[serial]]
        entry['pages'] += 1
        if granule >= 0:
            entry['last_granule'] = granule
        pos = body_start + body_len
    if not order:
        raise AudioFormatError(f'{path}: empty file')
    return order


# ----------------------------------------------------------------------------- writing

def _encode_ffmpeg(y, path, quality):
    cmd = ['ffmpeg', '-v', 'error', '-y', '-f', 'f32le', '-ar', str(SAMPLE_RATE), '-ac', str(CHANNELS),
           '-i', '-', '-c:a', 'libvorbis', '-q:a', f'{quality:g}', '-map_metadata', '-1', str(path)]
    subprocess.run(cmd, input=y.astype('<f4').tobytes(), check=True)


def _encode_libsndfile(y, path, quality):
    # libsndfile maps compression_level 0..1 onto Vorbis quality 1.0..0.0 (i.e. q10..q0).
    level = min(1.0, max(0.0, 1.0 - quality / 10.0))
    with sf.SoundFile(str(path), 'w', SAMPLE_RATE, CHANNELS, format='OGG', subtype='VORBIS',
                      compression_level=level) as out:
        for start in range(0, len(y), 8192):
            out.write(y[start:start + 8192])


def write_ogg(y, path, quality=DEFAULT_SPEC.vorbis_quality, encoder='auto'):
    """Encode one loop to Ogg Vorbis and verify the decoded frame count.

    ``y`` is clipped to +-1 (mastering keeps it well inside). ``encoder`` is
    ``'ffmpeg'``, ``'libsndfile'`` or ``'auto'`` (ffmpeg when on PATH). Writes to a
    temporary name first so a failed encode never leaves a half-written file under
    the real name. Returns the path.
    """
    y = as_stereo(y, str(path))
    y = np.clip(y, -1.0, 1.0)
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + '.part.ogg')
    if encoder == 'auto':
        encoder = 'ffmpeg' if shutil.which('ffmpeg') else 'libsndfile'
    if encoder == 'ffmpeg':
        _encode_ffmpeg(y, tmp, quality)
    elif encoder == 'libsndfile':
        _encode_libsndfile(y, tmp, quality)
    else:
        raise ValueError(f'unknown encoder {encoder!r}')
    frames = sf.info(str(tmp)).frames
    if frames != len(y):
        tmp.unlink(missing_ok=True)
        raise AudioFormatError(f'{path}: encoder wrote {frames} frames, expected {len(y)}')
    tmp.replace(path)
    return path


def write_trio(trio, directory, quality=DEFAULT_SPEC.vorbis_quality, encoder='auto'):
    """Write ``a1.ogg``/``a2.ogg``/``a3.ogg`` for ``trio`` into ``directory``.

    The only function in the pipeline that produces game files. Each file is
    verified to decode to exactly ``trio.frames`` frames. Returns ``{mood: Path}``.
    """
    if not isinstance(trio, Trio):
        raise TypeError('write_trio expects a Trio')
    paths = trio_paths(directory)
    for mood, path in paths.items():
        write_ogg(trio[mood], path, quality=quality, encoder=encoder)
    return paths


# ----------------------------------------------------------------------------- helpers

def db_to_gain(db):
    return 10.0 ** (np.asarray(db, dtype=np.float64) / 20.0)
