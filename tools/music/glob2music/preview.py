# SPDX-License-Identifier: GPL-3.0-or-later
"""Listening previews: calm -> building -> combat -> calm at aligned positions.

Pipeline role: every build writes ``preview.opus`` next to the trio so a human can
judge a set in about two and a half minutes without starting the game. The preview
imitates the game's mixer: one shared playback position runs through all three
files (wrapping at the loop length, so the seam is heard too) and each mood change is
a smoothstep crossfade *at the same position*, as the gameplay producer does.
The default duration and interleaved gain rounding follow ``MusicTypes.h`` and
``MusicStream.cpp``. This is an audition model, not bit-exact engine PCM output.
"""
import numpy as np

from .audio import Trio, read_trio, write_opus
from .spec import MOODS, SAMPLE_RATE

DEFAULT_ORDER = ('calm', 'building', 'combat', 'calm')
# Music::GameFadeFrames, preserving the original 44.1 kHz fade at 48 kHz.
GAME_FADE_FRAMES = (16384 * SAMPLE_RATE + 22050) // 44100
GAME_FADE_SECONDS = GAME_FADE_FRAMES / SAMPLE_RATE


def render(trio, order=DEFAULT_ORDER, segment_s=40.0, crossfade_s=GAME_FADE_SECONDS, fade_out_s=1.5, start_s=0.0):
    """Return the preview as a float64 (frames, 2) array.

    Each entry of ``order`` plays for ``segment_s``; changes crossfade over
    ``crossfade_s`` starting at the boundary. ``start_s`` offsets the shared playback
    position (to audition a particular passage or the seam).
    """
    if not isinstance(trio, Trio):
        raise TypeError('render expects a Trio')
    for mood in order:
        if mood not in MOODS:
            raise ValueError(f'unknown mood {mood!r}')
    if segment_s <= 0 or crossfade_s < 0 or fade_out_s < 0:
        raise ValueError('segment must be positive and fades non-negative')
    n = trio.frames
    seg = int(round(segment_s * SAMPLE_RATE))
    if seg < 1:
        raise ValueError('segment must contain at least one frame')
    total = seg * len(order)
    pos = (np.arange(total) + int(round(start_s * SAMPLE_RATE))) % n   # shared position, wrapping
    xf = min(int(round(crossfade_s * SAMPLE_RATE)), seg)
    # Interleaved stereo gains, including the tiny difference between channels.
    sample = np.arange(xf * 2, dtype=np.float64)
    length = max(1, xf * 2 - 1)
    incoming = np.floor(65535 * ((-2 / length**3) * sample**3 +
                               (3 / length**2) * sample**2)).reshape(xf, 2) / 65536
    outgoing = 65535 / 65536 - incoming
    out = np.zeros((total, 2))
    for k, mood in enumerate(order):
        a, b = k * seg, (k + 1) * seg
        out[a:b] = trio[mood][pos[a:b]]
    for k in range(1, len(order)):
        a = k * seg
        out[a:a + xf] = (trio[order[k - 1]][pos[a:a + xf]] * outgoing +
                        trio[order[k]][pos[a:a + xf]] * incoming)
    fade = min(int(fade_out_s * SAMPLE_RATE), total)
    if fade:
        out[-fade:] *= np.linspace(1.0, 0.0, fade)[:, None]
    return out


def make(trio_or_dir, out_path, **kwargs):
    """Write a preview Ogg for a ``Trio`` or a directory holding a1/a2/a3.opus.

    Keyword arguments go to ``render``. Returns the output path.
    """
    trio = trio_or_dir if isinstance(trio_or_dir, Trio) else read_trio(trio_or_dir)
    return write_opus(render(trio, **kwargs), out_path)
