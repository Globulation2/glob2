# SPDX-License-Identifier: GPL-3.0-or-later
"""Audio types and frame-exact file IO: the ``Trio`` every recipe returns.

Pipeline role: a recipe (``sets/<id>/recipe.py``) builds three float arrays and
returns them as a ``Trio``; ``master.finish`` turns that into a mastered ``Trio``;
``write_trio`` alone writes the Ogg Opus files the game loads. Keeping one writer
means every set gets the same encoder, quality and frame-count guarantee.

Array convention, used throughout the package: ``float32`` (or float64 while
processing), shape ``(frames, 2)``, 48000 Hz, nominal full scale +-1.0. One array is
exactly one loop: frame ``n - 1`` is followed by frame ``0`` when the game wraps.

Encoding uses the shared FFmpeg libopus recipe at 48 kbps VBR stereo. Every
file is completely decoded before publication, including pre-skip/end trimming.
"""
from dataclasses import dataclass, field
from pathlib import Path
import struct

import numpy as np
import soundfile as sf

from .spec import CHANNELS, FILENAMES, MOODS, SAMPLE_RATE


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

    No resampling happens here: callers that need 48 kHz must check the rate.
    """
    y, sr = sf.read(str(path), dtype='float32', always_2d=True)
    if y.shape[1] == 1:
        y = np.repeat(y, 2, axis=1)
    return np.ascontiguousarray(y[:, :2]), sr


def trio_paths(directory):
    """``{mood: Path}`` of ``a1.opus``/``a2.opus``/``a3.opus`` inside ``directory``."""
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
    order the streams start. Opus decoded length is last_granule minus pre_skip. The game requires exactly one Opus stream
    (``op_link_count() == 1``); chained or multiplexed files are refused. This is a
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
        if pos + 27 + nseg > len(data):
            raise AudioFormatError(f'{path}: truncated lacing table')
        lacing = data[pos + 27:pos + 27 + nseg]
        body_start = pos + 27 + nseg
        body_len = sum(lacing)
        if body_start + body_len > len(data):
            raise AudioFormatError(f'{path}: truncated page body')
        # A beginning-of-stream page always opens a new logical stream, even when a
        # chained file reuses the serial number of the previous link.
        if header_type & 0x02 or serial not in streams:
            body = data[body_start:body_start + 8]
            codec = 'vorbis' if body[:7] == b'\x01vorbis' else 'opus' if body[:8] == b'OpusHead' else 'other'
            key = len(order)
            streams[serial] = key
            order.append({'serial': serial, 'codec': codec, 'pages': 0, 'last_granule': -1,
                          'pre_skip': struct.unpack_from('<H', data, body_start + 10)[0] if codec == 'opus' and body_len >= 19 else 0,
                          'bos': bool(header_type & 0x02)})
        entry = order[streams[serial]]
        entry['pages'] += 1
        entry['eos'] = bool(header_type & 0x04)
        if granule >= 0:
            entry['last_granule'] = granule
        pos = body_start + body_len
    if not order:
        raise AudioFormatError(f'{path}: empty file')
    return order


# ----------------------------------------------------------------------------- writing

def write_opus(y, path, loop=False):
    """Encode PCM with the repository's fixed recipe; verify complete decoding."""
    import importlib.util
    import tempfile
    module_path = Path(__file__).resolve().parents[2] / 'encode_music.py'
    spec = importlib.util.spec_from_file_location('glob2_encode_music', module_path)
    encoder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(encoder)
    y = np.clip(as_stereo(y, str(path)), -1.0, 1.0)
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.pcm-', dir=path.parent) as tmp:
        wav = Path(tmp) / 'rendered.wav'
        sf.write(wav, y, SAMPLE_RATE, subtype='FLOAT')
        encoder.encode(wav, path, expected_frames=len(y), loop=loop)
    return path


def write_trio(trio, directory):
    """Stage and fully validate all three loops before publishing any file."""
    import tempfile
    if not isinstance(trio, Trio):
        raise TypeError('write_trio expects a Trio')
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.trio-', dir=directory) as tmp:
        for mood, path in trio_paths(tmp).items():
            write_opus(trio[mood], path, loop=True)
        for mood, path in trio_paths(tmp).items():
            path.replace(directory / FILENAMES[mood])
    return trio_paths(directory)


# ----------------------------------------------------------------------------- helpers

def db_to_gain(db):
    return 10.0 ** (np.asarray(db, dtype=np.float64) / 20.0)
