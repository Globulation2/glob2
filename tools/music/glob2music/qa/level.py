# SPDX-License-Identifier: GPL-3.0-or-later
"""Checks ``audibility`` and ``dropout``: level problems inside a file (warn only).

``audibility`` -- what it measures: the power of the quietest 3 s window (0.5 s hop,
circular) relative to the file's mean power, in dB.

Why it matters: calm plays under constant unit and building sound effects. A calm
passage 10 dB or more under the file's average disappears under the effects, and the
player hears the music "stop" and "restart" every loop. Building and combat are
judged by the same rule: a deep hole in combat reads as the fight being over.

Threshold: warn below -10 dB, the brief's rule. On the corpus this warns on three
approved calm moods (woodland -19.6 dB, its quiet intro; thistle-waltz -11.7;
moss-lanterns -10.7); the maintainer accepted these, and the later acoustic sets
(bramble-jig, fennel-mist) were deliberately composed to stay above -9 dB. A warning
is a prompt to listen, not a defect.

``dropout`` -- what it measures: sudden collapses of level inside sustained material.
On 10 ms RMS frames, a dropout starts where the preceding 50 ms sit within
``dropout_context_db`` of the file's median level (so the music was substantial) and
every frame of the next 100 ms is at least ``dropout_drop_db`` lower than that.
Natural releases decay through reverb over hundreds of milliseconds and do not
qualify; a render that loses a sample stream (as early sfizz renders streaming samples from
disk did) or a hard edit gap does. The wrap is included, as the analysis is circular.

Threshold: warn at one or more events. No corpus set has one; this is a guard
against render faults, which are hard to hear in a full mix but obvious in game
when the rest of the arrangement thins out.
"""
import numpy as np

from .result import CheckResult, PASS, WARN, band

NAME_AUDIBILITY = 'audibility'
NAME_DROPOUT = 'dropout'


def quietest_window_db(mono, sample_rate, window_s=3.0, hop_s=0.5):
    """(dB of the quietest window re mean power, its start in seconds), circular."""
    w = int(window_s * sample_rate)
    h = int(hop_s * sample_rate)
    x = np.asarray(mono, dtype=np.float64) ** 2
    if len(x) <= w:
        return float('nan'), 0.0
    z = np.concatenate([x, x[:w]])
    c = np.concatenate([[0.0], np.cumsum(z)])
    starts = np.arange(0, len(x), h)
    p = (c[starts + w] - c[starts]) / w
    i = int(np.argmin(p))
    return float(10 * np.log10((p[i] + 1e-20) / (x.mean() + 1e-20))), starts[i] / sample_rate


def find_dropouts(mono, sample_rate, drop_db, context_db):
    """Start times (s) of dropout events (see module docstring)."""
    f = int(0.01 * sample_rate)
    x = np.asarray(mono, dtype=np.float64)
    n = len(x) // f
    if n < 30:
        return []
    lv = 10 * np.log10(np.mean(x[:n * f].reshape(n, f) ** 2, axis=1) + 1e-12)
    med = np.median(lv)
    lv2 = np.concatenate([lv, lv[:15]])                  # circular look-ahead
    events = []
    k = 0
    while k < n:
        before = lv2[k - 5:k] if k >= 5 else np.concatenate([lv[k - 5:], lv[:k]])
        ref = 10 * np.log10(np.mean(10 ** (before / 10)))
        if ref >= med - context_db and np.max(lv2[k:k + 10]) <= ref - drop_db:
            events.append(k * 0.01)
            k += 50                                       # one event per 0.5 s
        else:
            k += 1
    return events


def check_audibility(trio, spec):
    t = spec.qa
    cr = CheckResult(NAME_AUDIBILITY, description='quietest 3 s window vs mean (warn only)')
    for mood in trio.present():
        a = trio[mood]
        db, at = quietest_window_db(a.mono, a.sample_rate)
        status = band(db, t.quietest_window_warn_db, -1e9, higher_is_worse=False)
        cr.add(f'{mood}.quietest_3s', status, db, f'>= {t.quietest_window_warn_db:g} dB',
               f'at {at:.1f} s', unit='dB')
    return cr


def check_dropout(trio, spec):
    t = spec.qa
    cr = CheckResult(NAME_DROPOUT, description='sudden silences inside sustained material (warn only)')
    for mood in trio.present():
        a = trio[mood]
        ev = find_dropouts(a.mono, a.sample_rate, t.dropout_drop_db, t.dropout_context_db)
        cr.add(f'{mood}.events', WARN if len(ev) >= t.dropout_warn_count else PASS, len(ev),
               f'< {t.dropout_warn_count}',
               ('at ' + ', '.join(f'{s:.2f} s' for s in ev[:6])) if ev else 'none')
    return cr
