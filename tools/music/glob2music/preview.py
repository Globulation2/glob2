# SPDX-License-Identifier: GPL-3.0-or-later
"""Listening previews: calm -> building -> combat -> calm at aligned positions.

Pipeline role: every build writes ``preview.ogg`` next to the trio so a human can
judge a set in about two and a half minutes without starting the game. The preview
imitates the game's mixer: one shared playback position runs through all three
files (wrapping at the loop length, so the seam is heard too) and each mood change is
an equal-power crossfade *at the same position*, as ``SoundMixer`` does. The game's
own crossfade is only 0.37 s; the preview defaults to 3 s so the listener can hear
both moods agree, and ``crossfade_s=0.37`` reproduces the game exactly.
"""
import numpy as np

from .audio import Trio, read_trio, write_ogg
from .loop import equal_power
from .spec import MOODS, SAMPLE_RATE

DEFAULT_ORDER = ('calm', 'building', 'combat', 'calm')


def render(trio, order=DEFAULT_ORDER, segment_s=40.0, crossfade_s=3.0, fade_out_s=1.5, start_s=0.0):
    """Return the preview as a float64 (frames, 2) array.

    Each entry of ``order`` plays for ``segment_s``; changes crossfade over
    ``crossfade_s`` centred on the boundary. ``start_s`` offsets the shared playback
    position (to audition a particular passage or the seam).
    """
    if not isinstance(trio, Trio):
        raise TypeError('render expects a Trio')
    for mood in order:
        if mood not in MOODS:
            raise ValueError(f'unknown mood {mood!r}')
    n = trio.frames
    seg = int(round(segment_s * SAMPLE_RATE))
    total = seg * len(order)
    pos = (np.arange(total) + int(round(start_s * SAMPLE_RATE))) % n   # shared position, wrapping
    xf = min(int(round(crossfade_s * SAMPLE_RATE)), seg)
    weights = np.zeros((len(order), total))
    for k in range(len(order)):
        weights[k, k * seg:(k + 1) * seg] = 1.0
    fade_out, fade_in = equal_power(xf)
    for k in range(1, len(order)):
        a = k * seg - xf // 2
        weights[k - 1, a:a + xf] = fade_out
        weights[k, a:a + xf] = fade_in
    out = np.zeros((total, 2))
    for k, mood in enumerate(order):
        active = weights[k] > 0
        out[active] += trio[mood][pos[active]].astype(np.float64) * weights[k, active, None]
    fade = min(int(fade_out_s * SAMPLE_RATE), total)
    if fade:
        out[-fade:] *= np.linspace(1.0, 0.0, fade)[:, None]
    return out


def make(trio_or_dir, out_path, quality=5, **kwargs):
    """Write a preview Ogg for a ``Trio`` or a directory holding a1/a2/a3.ogg.

    Keyword arguments go to ``render``. Returns the output path.
    """
    trio = trio_or_dir if isinstance(trio_or_dir, Trio) else read_trio(trio_or_dir)
    return write_ogg(render(trio, **kwargs), out_path, quality=quality)
