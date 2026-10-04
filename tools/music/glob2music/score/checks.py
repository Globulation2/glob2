# SPDX-License-Identifier: GPL-3.0-or-later
"""Static checks on a score, and the dropout scan of rendered stems.

Pipeline role: a symbolic recipe runs ``check_score`` before rendering and
``find_dropouts`` after it. The first catches writing mistakes cheaply; the second
catches rendering faults that no listener should have to find.

``check_score`` returns ``Finding`` objects at three levels:

* ``error`` - a note outside its instrument's mapped key range. Sample players stay
  silent there, so the build stops.
* ``warning`` - voice-leading and writing faults that make generated music sound
  generated: parallel fifths/octaves between melody, counter-line and bass (on
  consecutive onsets), the counter-line crossing above the melody where it is not a
  descant (``Score.counter_above``), and bars of melody or counter-line repeated note
  for note (exact repetition was the first experiments' main fault; a returning theme
  should be varied).
* ``info`` - strong-beat non-chord tones in the written voices (usually deliberate
  appoggiaturas, listed so they are deliberate) and onsets per beat per section and
  mood (a quick view of how much each mood adds).

``find_dropouts`` looks for sustained notes (>= 0.9 s) whose rendered stem is silent
0.45-0.85 s after the onset. Under heavy parallel load sfizz_render can outrun its
background sample streamer and play only the preloaded head of a sample (~0.3 s); this
scan found 1-4 such notes in about 13 stems of the first Moss Lanterns and Thistle
Waltz renders.
"""
from dataclasses import dataclass

import numpy as np

from .counter import sounding
from .notation import chord_at, chord_pcs, midi_to_name, NOTE_NAMES


@dataclass
class Finding:
    level: str          # 'error' | 'warning' | 'info'
    kind: str           # 'range' | 'parallel' | 'crossing' | 'reuse' | 'nct' | 'density'
    message: str

    def __str__(self):
        return f'{self.level.upper():7s} {self.kind:8s} {self.message}'


def _where(score, beat):
    return f'bar {score.bar_of(beat)} beat {beat % score.beats_per_bar + 1:g}'


def ranges(score, parts_by_mood, instruments):
    """Notes outside their instrument's ``low..high`` key range (errors)."""
    out = []
    for mood, parts in parts_by_mood.items():
        for part in parts:
            inst = instruments[part.instrument]
            if inst.kind == 'perc':
                continue
            bad = [(n.start, p) for n in part.notes for p in n.pitches if not inst.low <= p <= inst.high]
            for beat, p in bad:
                out.append(Finding('error', 'range', f'{mood}:{part.name} ({part.instrument}) {midi_to_name(p)} '
                                   f'outside {midi_to_name(inst.low)}-{midi_to_name(inst.high)} at {_where(score, beat)}'))
    return out


def non_chord_tones(score):
    """Strong-beat notes of the written voices that are not in the chord (info)."""
    out = []
    bpb = score.beats_per_bar
    for label, line in (('melody', score.melody), ('counter', score.counter), ('bass', score.bass)):
        for n in line:
            strong = abs(n.start % 1) < 1e-6 and (n.start % bpb in (0, 2) or n.dur >= 1.5)
            if not strong:
                continue
            sym = chord_at(score.harmony, n.start)
            _, pcs, bass = chord_pcs(sym)
            for p in n.pitches:
                if p % 12 not in pcs and p % 12 != bass:
                    out.append(Finding('info', 'nct', f'{label} {NOTE_NAMES[p % 12]} over {sym} at '
                                       f'{_where(score, n.start)} ({n.dur:g} beats)'))
    return out


def parallels(score):
    """Parallel fifths/octaves between melody, counter and bass on consecutive onsets,
    and counter-line crossings above the melody outside descant bars (warnings)."""
    out = []
    pairs = (('melody', score.melody, 'counter', score.counter), ('melody', score.melody, 'bass', score.bass),
             ('counter', score.counter, 'bass', score.bass))
    for la, a_line, lb, b_line in pairs:
        prev = None
        for t in sorted({n.start for n in a_line} | {n.start for n in b_line}):
            a, b = sounding(a_line, t), sounding(b_line, t)
            if a is None or b is None:
                prev = None
                continue
            iv = abs(a - b) % 12
            if prev and prev[2] == iv and iv in (0, 7) and prev[0] != a and prev[1] != b:
                out.append(Finding('warning', 'parallel', f'{"octaves" if iv == 0 else "fifths"} {la}/{lb} '
                                   f'into {_where(score, t)}'))
            if lb == 'counter' and b > a and abs(t % 1) < 1e-6 and not score.counter_is_above(t):
                out.append(Finding('warning', 'crossing', f'counter above melody at {_where(score, t)}'))
            prev = (a, b, iv)
    return out


def repeated_bars(score):
    """Bars of melody or counter-line identical (pitch and rhythm) to an earlier bar."""
    out = []
    bpb = score.beats_per_bar
    for label, line in (('melody', score.melody), ('counter', score.counter)):
        seen = {}
        for bar in range(1, score.bars + 1):
            s0 = (bar - 1) * bpb
            key = tuple((round(n.start - s0, 3), round(n.dur, 3), tuple(n.pitches))
                        for n in line if s0 - 1e-6 <= n.start < s0 + bpb - 1e-6)
            if len(key) >= 2 and key in seen:
                out.append(Finding('warning', 'reuse', f'{label} bar {bar} repeats bar {seen[key]}'))
            seen.setdefault(key, bar)
    return out


def density(score, parts_by_mood):
    """Onsets per beat per section and mood (info)."""
    out = []
    for mood, parts in parts_by_mood.items():
        cells = []
        for sec, (a, b) in score.sections.items():
            s, e = score.span(a, b)
            count = sum(1 for p in parts for n in p.notes if s <= n.start < e)
            cells.append(f'{sec} {count / (e - s):.1f}')
        out.append(Finding('info', 'density', f'{mood}: onsets/beat ' + ', '.join(cells)))
    return out


def check_score(score, parts_by_mood, instruments):
    """All static checks; returns a list of ``Finding`` (errors first)."""
    found = (ranges(score, parts_by_mood, instruments) + parallels(score) + repeated_bars(score)
             + non_chord_tones(score) + density(score, parts_by_mood))
    order = {'error': 0, 'warning': 1, 'info': 2}
    return sorted(found, key=lambda f: order[f.level])


def find_dropouts(stem, notes, sample_rate, preroll_s=0.0, min_note_s=0.9, floor_db=-78.0):
    """Sustained notes that are silent in a rendered stem.

    ``stem`` is the rendered ``(frames, channels)`` array (with ``preroll_s`` of lead-in),
    ``notes`` the ``(on_s, off_s, pitch)`` triples from ``midi.read_notes``. A note of at
    least ``min_note_s`` whose mean level 0.45-0.85 s after its onset is below
    ``floor_db`` dBFS counts. Returns ``(silent, checked)`` lists of note triples.
    """
    a = np.abs(np.asarray(stem, dtype=np.float64)).reshape(len(stem), -1).mean(1)
    silent, checked = [], []
    for on, off, p in notes:
        s = on + preroll_s
        if off - on < min_note_s or s < 0:
            continue
        seg = a[int((s + 0.45) * sample_rate):int((s + 0.85) * sample_rate)]
        if not len(seg):
            continue
        checked.append((on, off, p))
        if 20 * np.log10(seg.mean() + 1e-12) < floor_db:
            silent.append((on, off, p))
    return silent, checked
