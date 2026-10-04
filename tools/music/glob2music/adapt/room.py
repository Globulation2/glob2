# SPDX-License-Identifier: GPL-3.0-or-later
"""A synthetic room: reverb from generated noise, so no third-party impulse response.

Pipeline role: adapted moods sometimes need space the source did not have -- a calm
mood stripped to its melody sounds dry and exposed, and synthesised percussion needs
to sit in a room to blend with recorded music. ``add_room`` convolves the loop with
an impulse response built here: seeded Gaussian noise under an exponential decay
(``decay_s`` is the time to fall 60 dB), low-passed so the tail is dark and warm,
after a short pre-delay, normalised to unit energy per channel. The two channels use
independent noise, which gives a natural stereo spread.

A room is a *preset* identified by its integer ``seed``: the noise realisation gives
each room its own colour (a long, loud bed especially), and the rooms in approved sets
were judged by ear with specific seeds, so recipes pin them like any other setting.

The convolution is circular (``master.circular_convolve``): the reverb of the loop's
last notes rings into its first frames, exactly as when the game wraps, so the seam
needs no special treatment.

Used by Curious Critters: its calm "bed" is the melody layer drowned in a 4.5 s room
(preset 7) and mixed back under itself, and its synthesised percussion sits in
1.2-1.6 s rooms (preset 1).
"""
import numpy as np
import scipy.signal as ss

from .. import master
from ..spec import SAMPLE_RATE

SR = SAMPLE_RATE


def impulse_response(seed, decay_s=1.8, predelay_s=0.012, lowpass_hz=3500.0, channels=2):
    """A (taps, channels) noise reverb IR, 1.5 x ``decay_s`` long, unit energy per channel."""
    rng = np.random.default_rng(seed)
    n = int(decay_s * 1.5 * SR)
    t = np.arange(n) / SR
    env = np.exp(-6.9 * t / decay_s)          # -60 dB at t = decay_s
    ir = rng.standard_normal((n, channels)) * env[:, None]
    sos = ss.butter(2, lowpass_hz, 'low', fs=SR, output='sos')
    ir = ss.sosfilt(sos, ir, axis=0)
    ir = np.concatenate([np.zeros((int(predelay_s * SR), channels)), ir])
    return ir / np.sqrt(np.sum(ir ** 2, axis=0, keepdims=True))


def add_room(y, wet, seed=1, decay_s=1.8, predelay_s=0.012, lowpass_hz=3500.0):
    """Blend a loop with its synthetic-room reverb: ``(1 - wet) * dry + wet * reverb``.

    ``seed`` selects the room preset (see the module docstring). ``wet=1`` returns the
    reverb alone, the way to make a "bed" that is mixed back under the dry signal at a
    chosen level.
    """
    y = np.asarray(y, dtype=np.float64)
    ir = impulse_response(seed, decay_s, predelay_s, lowpass_hz, channels=y.shape[1])
    return y * (1.0 - wet) + master.circular_convolve(y, ir) * wet
