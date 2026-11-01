# SPDX-License-Identifier: GPL-3.0-or-later
"""Fold rendered score tails and early notes into a seamless shared loop.

The score backends render a pre-roll and release tails. ``fold_tail`` wraps these
onto the score's exact loop length so every mood keeps the same timeline.
"""
import numpy as np


def _as2d(y):
    y = np.asarray(y, dtype=np.float64)
    return (y[:, None], True) if y.ndim == 1 else (y, False)


def fold_tail(y, loop_frames, preroll_frames=0):
    """Fold a render with pre-roll and tail into one loop of ``loop_frames``.

    ``y`` holds ``preroll_frames`` of lead-in, then the loop, then any tail. The tail
    (possibly longer than a loop) is added onto the loop start; anything sounding in
    the pre-roll (notes played early by humanised timing) is added onto the loop end,
    because in the game it precedes the wrap.
    """
    y, flat = _as2d(y)
    need = preroll_frames + loop_frames
    if len(y) < need:
        y = np.vstack([y, np.zeros((need - len(y), y.shape[1]))])
    out = y[preroll_frames:need].copy()
    tail = y[need:]
    for k in range(0, len(tail), loop_frames):
        seg = tail[k:k + loop_frames]
        out[:len(seg)] += seg
    if preroll_frames:
        pre = y[:preroll_frames]
        if preroll_frames > loop_frames:
            raise ValueError('pre-roll longer than the loop')
        out[loop_frames - preroll_frames:] += pre
    return out[:, 0] if flat else out
