# SPDX-License-Identifier: GPL-3.0-or-later
"""Check ``contrast``: are the three moods audibly different, in the right direction?

What it measures:

* ``<pair>.distance``: the mean absolute difference (dB) between the two moods'
  64-band log-mel spectrograms, frame by frame, after removing each file's overall
  level (and flooring 80 dB below its peak). Identical moods score 0; a mood that is
  another plus or minus a layer scores 2-5 dB; different orchestrations 5-12 dB.
* ``escalation``: whether combat steps up from building. Two routes count: more
  rhythmic activity (onset-rate ratio combat/building, scored as
  ``log2(ratio) / log2(1.1)``) or more percussion (HPSS percussive energy share,
  scored as ``delta / 0.10``). The score is the better of the two; 1.0 means a 10%
  denser rhythm *or* 10 points more percussive energy.
* ``percussive_ladder``: combat's percussive share is at least calm's.
* ``calm_restraint``: calm's percussive share and onset rate stay modest.

Why it matters for the adaptive mixer: the moods exist to tell the player, without a
word, that the colony is quiet, growing or under attack. The mixer swaps them at the
same position with a short fade, so a mood change the ear cannot hear is a signal
lost; a calm mood that is already busy leaves nowhere to go and tires the ear over a
long, mostly peaceful session.

Thresholds (calibrated with ``python3 -m glob2music calibrate``):

* distance fails below 0.75 dB and warns below 2.0 dB. Approved pairs measure at
  least 2.40 dB (orchestral-dawn building/combat); identical moods measure ~0.
* escalation fails below 0.60 and warns below 1.0. Every approved set scores >= 1.33
  (woodland, by percussion: 0.10 -> 0.24); the lowest by rhythm is thistle-waltz at
  1.86 (onsets x1.19). curious-critters scores 3.05 (onsets x1.34, percussion 0.07
  -> 0.17).
  An earlier adaptation of Curious Critters, judged to have too little difference
  between its moods, measured 0.54: its combat is no denser than building (x0.97) and adds only 5 points
  of percussion. The rule is fitted to that one negative verdict; treat its margin
  as provisional until more sets are judged.

  Deliberately *not* a route: more low end. That earlier adaptation's combat raised the below-150 Hz share
  from 0.60 to 0.84 and was still heard as too similar. Nor does the score grow
  without bound in any way that matters: it is a floor, so busier or brighter
  percussion earns nothing once the step is made (the maintainer wants downtempo
  music). HPSS also under-reads warm, long-decay low drums as harmonic, which is why
  a rhythm route exists alongside the percussion route.
* percussive_ladder warns when combat is less percussive than calm by over 0.02.
* calm_restraint warns above 0.15 percussive share or 5.0 onsets/s. Approved calm
  moods reach 0.12 and 4.6/s (apple-cider); Toy Soldiers, rejected as busy, has a
  calm of 0.24 and 5.5/s.
"""
import math

import numpy as np

from ..spec import MOODS
from .result import CheckResult, PASS, WARN, band

NAME = 'contrast'
PAIRS = (('calm', 'building'), ('building', 'combat'), ('calm', 'combat'))


def mel_distance(ma, mb, floor_db=80.0):
    """Mean |difference| (dB) of two level-normalised log-mel spectrograms."""
    n = min(ma.shape[1], mb.shape[1])
    a = np.maximum(ma[:, :n], ma[:, :n].max() - floor_db)
    b = np.maximum(mb[:, :n], mb[:, :n].max() - floor_db)
    return float(np.abs((a - a.mean()) - (b - b.mean())).mean())


def escalation_score(onset_low, onset_high, perc_low, perc_high):
    """Better of the rhythm route and the percussion route (see module docstring)."""
    ratio = onset_high / max(onset_low, 1e-6)
    rhythm = math.log2(max(ratio, 1e-6)) / math.log2(1.1)
    percussion = (perc_high - perc_low) / 0.10
    return max(rhythm, percussion), ratio, perc_high - perc_low


def check(trio, spec):
    t = spec.qa
    cr = CheckResult(NAME, description='moods distinct; combat escalates; calm stays calm')
    if not all(m in trio.moods for m in MOODS):
        return cr
    for x, y in PAIRS:
        d = mel_distance(trio[x].logmel, trio[y].logmel)
        cr.add(f'{x}_{y}.distance', band(d, t.mood_distance_warn_db, t.mood_distance_fail_db, higher_is_worse=False),
               d, f'>= {t.mood_distance_warn_db:g} dB (fail < {t.mood_distance_fail_db:g})',
               'level-matched log-mel difference', unit='dB')
    perc = {m: trio[m].percussive_share for m in MOODS}
    ons = {m: trio[m].onset_rate for m in MOODS}
    score, ratio, dperc = escalation_score(ons['building'], ons['combat'], perc['building'], perc['combat'])
    cr.add('escalation', band(score, t.escalation_warn, t.escalation_fail, higher_is_worse=False), score,
           f'>= {t.escalation_warn:g} (fail < {t.escalation_fail:g})',
           f'combat/building onsets x{ratio:.2f}, percussive share {perc["building"]:.2f} -> {perc["combat"]:.2f}')
    drop = perc['calm'] - perc['combat']
    cr.add('percussive_ladder', WARN if drop > t.percussive_ladder_slack else PASS, perc['combat'] - perc['calm'],
           f'combat - calm >= -{t.percussive_ladder_slack:g}',
           'percussive share ' + ' / '.join(f'{perc[m]:.2f}' for m in MOODS))
    busy = perc['calm'] > t.calm_percussive_max or ons['calm'] > t.calm_onset_rate_max
    cr.add('calm_restraint', WARN if busy else PASS, perc['calm'],
           f'percussive <= {t.calm_percussive_max:g}, onsets <= {t.calm_onset_rate_max:g}/s',
           f'calm percussive share {perc["calm"]:.2f}, {ons["calm"]:.1f} onsets/s')
    return cr
