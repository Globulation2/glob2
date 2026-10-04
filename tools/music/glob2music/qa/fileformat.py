# SPDX-License-Identifier: GPL-3.0-or-later
"""Check ``format``: will the game load this trio at all?

What it measures: for each of a1/a2/a3.ogg, that the file is an Ogg container with
exactly one logical stream, that the stream is Vorbis, 44100 Hz and stereo; across the
trio, that the three decoded PCM frame counts are identical; and that the loop length
lies within ``min_seconds``..``max_seconds``.

Why it matters: ``SoundMixer::openMusicSet`` refuses a set unless ``ov_info`` reports
44100 Hz stereo, ``ov_streams`` is 1 and ``ov_pcm_total`` is equal for all three files,
because the mixer crossfades between moods at the *same playback position* and wraps
every file at the same frame. A one-frame difference would make the moods drift apart
by a frame per loop; the game prefers refusing the set. The decoded length here comes
from libsndfile, which (like libvorbisfile) reads the final granule position, and was
cross-checked against ffmpeg and ffprobe on the whole calibration corpus.

Thresholds: these are hard rules of the engine, not tuned values, so every violation
is a ``fail``. The length band (50-120 s) is the soundtrack brief's: below 50 s a loop
repeats too often in a long session (the rejected generated sets were ~20 s ideas
repeated inside longer files, which ``repetition`` catches); above 120 s the files
cost bundle size for no audible gain. Every corpus set lies in 53-111 s.
"""
from ..audio import AudioFormatError, ogg_streams
from ..spec import FILENAMES, SAMPLE_RATE
from .result import CheckResult, FAIL, PASS, SKIP

NAME = 'format'


def check(trio, spec):
    """Run the format check on a ``TrioAudio``; returns a ``CheckResult``."""
    t = spec.qa
    cr = CheckResult(NAME, description='rate, channels, single Vorbis stream, equal frame counts, length')
    for mood, err in sorted(getattr(trio, 'errors', {}).items()):
        cr.add(f'{mood}.readable', FAIL, err, 'a decodable file', err)
    for mood in trio.present():
        a = trio[mood]
        if a.path is not None:
            try:
                streams = ogg_streams(a.path)
                ok = len(streams) == 1 and streams[0]['codec'] == 'vorbis'
                desc = ', '.join(f"{s['codec']}#{s['serial']:08x}" for s in streams)
                cr.add(f'{mood}.container', PASS if ok else FAIL, len(streams), 'one Vorbis stream',
                       f'{FILENAMES[mood]}: {desc}')
            except AudioFormatError as e:
                cr.add(f'{mood}.container', FAIL, 'not Ogg', 'one Vorbis stream', str(e))
        cr.add(f'{mood}.rate', PASS if a.sample_rate == SAMPLE_RATE else FAIL, a.sample_rate,
               f'== {SAMPLE_RATE}', unit='Hz')
        channels = a.samples.shape[1]
        cr.add(f'{mood}.channels', PASS if channels == spec.channels else FAIL, channels, f'== {spec.channels}')
    present = trio.present()
    if len(present) >= 2:
        frames = {m: trio[m].frames for m in present}
        equal = len(set(frames.values())) == 1 and trio.complete
        cr.add('frames', PASS if equal else FAIL, max(frames.values()) - min(frames.values()), 'all equal',
               ', '.join(f'{m} {n}' for m, n in frames.items()), unit='frames diff')
    if present:
        secs = max(trio[m].seconds for m in present)
        ok = t.min_seconds <= secs <= t.max_seconds
        cr.add('length', PASS if ok else FAIL, secs, f'{t.min_seconds:g}-{t.max_seconds:g} s', unit='s')
    else:
        cr.add('length', SKIP, None, '', 'no decodable file')
    return cr
