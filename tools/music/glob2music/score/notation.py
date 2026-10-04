# SPDX-License-Identifier: GPL-3.0-or-later
"""Text notation for voices and harmony, and the small amount of theory scores need.

Pipeline role: composers write melodies, counter-lines and bass lines as short strings
in a set's ``composition.py``; ``parse_line`` turns them into ``Note`` lists and
``parse_harmony`` turns a chord chart into timed chord symbols. Everything downstream
(accompaniment generators, humanisation, checks, MIDI export, any rendering backend)
works on those ``Note`` objects, so the notation is the one place a human edits music.

Voice notation, bars separated by ``|``::

    "r:e d5:e g5 a5 bb5:q a5:e g5 | g5:q bb5:e g5 a5:q. c6:e"

A token is ``PITCH[:DUR][ARTIC]``:

* ``PITCH``: ``r`` (rest) or a note name with octave, ``c4`` = MIDI 60 = middle C.
  Accidentals are ``#``, ``b``, ``##``, ``bb`` (``bb5`` is B-flat 5, ``e#5`` sounds F5).
  Chords join pitches with ``+``: ``g3+d4+bb4``.
* ``DUR``: ``w h q e s`` (4, 2, 1, 1/2, 1/4 beats; a beat is a crotchet), optionally
  dotted (``q.``) or triplet (``et``), or a plain number of beats (``1.5``). Omitted, the
  previous duration repeats.
* ``ARTIC``: any of ``!`` staccato, ``-`` tenuto, ``>`` accent, ``^`` marcato,
  ``~`` tie into the next note of the same pitch.

Every bar is checked against the metre, so a miscounted bar fails loudly at import
time instead of silently shifting the rest of the piece. Compound metres are written in
crotchet beats: 6/8 is three beats per bar (``q.`` = one dotted-crotchet pulse).

Chord symbols: a root (``A``-``G`` with ``#``/``b``), a quality from ``QUALITIES``
(``m``, ``7``, ``m7``, ``maj7``, ``m6``, ``dim7``, ``sus4``, ``7b9``, ``m7b5``, ...)
and an optional slash bass (``Gm/F``). A chart is bars separated by ``|``; chords in
a bar share it evenly unless given explicit lengths (``Eb:3 F:1``).
"""
from dataclasses import dataclass, field
import itertools
import re

NOTE_PC = {'c': 0, 'd': 2, 'e': 4, 'f': 5, 'g': 7, 'a': 9, 'b': 11}
NOTE_NAMES = ('C', 'C#', 'D', 'Eb', 'E', 'F', 'F#', 'G', 'Ab', 'A', 'Bb', 'B')
DURATIONS = {'w': 4.0, 'h': 2.0, 'q': 1.0, 'e': 0.5, 's': 0.25}
_ACC = {'#': 1, 'b': -1, '##': 2, 'bb': -2, None: 0}
_TOKEN = re.compile(r'([a-g#b+\d\-r]+?)(?::([whqes]\.?t?|\d+(?:\.\d+)?))?([!\->~^*]*)')


class NotationError(ValueError):
    """A voice line or chord chart cannot be parsed, or a bar has the wrong length."""


def name_to_midi(name):
    """``'bb4'`` -> 70. Octave numbers follow scientific pitch (``c4`` = 60)."""
    m = re.fullmatch(r'([a-g])(#|b|##|bb)?(-?\d)', name)
    if not m:
        raise NotationError(f'bad pitch {name!r}')
    return 12 * (int(m.group(3)) + 1) + NOTE_PC[m.group(1)] + _ACC[m.group(2)]


def midi_to_name(pitch):
    """60 -> ``'C4'`` (for messages and reports)."""
    return f'{NOTE_NAMES[pitch % 12]}{pitch // 12 - 1}'


@dataclass
class Note:
    """One written note or chord.

    ``start`` and ``dur`` are in beats from the start of the piece (beat 0 = bar 1).
    ``pitches`` holds MIDI note numbers (several for a chord). ``artic`` is a set of the
    articulation marks above; generators also use ``'roll'`` for rolled chords.
    Performance (velocity, timing, length) is not stored here: see ``perform.py``.
    """

    start: float
    dur: float
    pitches: list
    artic: set = field(default_factory=set)

    @property
    def end(self):
        return self.start + self.dur

    def moved(self, semitones=0, artic=None):
        """A copy transposed by ``semitones`` and optionally with extra marks."""
        return Note(self.start, self.dur, [p + semitones for p in self.pitches],
                    set(self.artic) | set(artic or ()))


def parse_line(text, beats_per_bar=4, start_bar=1, part=''):
    """Parse a voice line into ``Note`` objects (rests dropped, ties merged).

    ``start_bar`` places the first bar (used for hand-written overrides inside a
    longer line); ``part`` only labels error messages. Newlines are allowed anywhere.
    """
    text = text.replace('\n', ' ')
    notes = []
    t = (start_bar - 1) * beats_per_bar
    cur = 1.0
    for bi, bar in enumerate(text.split('|')):
        toks = bar.split()
        if not toks:
            continue
        bar_start = t
        for tok in toks:
            m = _TOKEN.fullmatch(tok)
            if not m:
                raise NotationError(f'{part}: bad token {tok!r}')
            pitch, dur, artic = m.groups()
            if dur:
                if dur[0] in DURATIONS:
                    cur = DURATIONS[dur[0]] * (1.5 if '.' in dur else 1.0) * (2 / 3 if 't' in dur else 1.0)
                else:
                    cur = float(dur)
            if pitch != 'r':
                notes.append(Note(t, cur, [name_to_midi(x) for x in pitch.split('+')], set(artic)))
            t += cur
        if abs((t - bar_start) - beats_per_bar) > 1e-6:
            raise NotationError(f'{part}: bar {start_bar + bi} has {t - bar_start:g} beats, '
                                f'expected {beats_per_bar}: {bar.strip()!r}')
    merged = []
    for n in notes:
        prev = merged[-1] if merged else None
        if prev and '~' in prev.artic and prev.pitches == n.pitches and abs(prev.end - n.start) < 1e-6:
            prev.dur += n.dur
            prev.artic = (prev.artic - {'~'}) | n.artic
        else:
            merged.append(n)
    return merged


# ----------------------------------------------------------------------------- harmony

#: Chord qualities as semitone offsets from the root, in the order generators use
#: them (root, third, fifth, seventh, extensions): ``pcs[1]`` is the third, ``pcs[3]``
#: the seventh when present.
QUALITIES = {
    '': [0, 4, 7], 'm': [0, 3, 7], '7': [0, 4, 7, 10], 'm7': [0, 3, 7, 10],
    'maj7': [0, 4, 7, 11], 'm6': [0, 3, 7, 9], '6': [0, 4, 7, 9], 'dim7': [0, 3, 6, 9],
    'dim': [0, 3, 6], 'sus4': [0, 5, 7], '7sus4': [0, 5, 7, 10], '7b9': [0, 4, 7, 10, 13],
    'add9': [0, 4, 7, 14], 'madd9': [0, 3, 7, 14], 'm9': [0, 3, 7, 10, 14], 'maj9': [0, 4, 7, 11, 14],
    'aug': [0, 4, 8], 'm7b5': [0, 3, 6, 10], '9': [0, 4, 7, 10, 14], 'maj7#11': [0, 4, 7, 11, 18],
}


def chord_pcs(symbol):
    """``'Gm7/F'`` -> ``(root_pc, [pitch classes], bass_pc)``."""
    m = re.fullmatch(r'([A-G])(#|b)?([a-z0-9#]*)(?:/([A-G])(#|b)?)?', symbol)
    if not m:
        raise NotationError(f'bad chord {symbol!r}')
    root = (NOTE_PC[m.group(1).lower()] + _ACC[m.group(2)]) % 12
    if m.group(3) not in QUALITIES:
        raise NotationError(f'unknown chord quality {m.group(3)!r} in {symbol!r}')
    pcs = [(root + i) % 12 for i in QUALITIES[m.group(3)]]
    bass = (NOTE_PC[m.group(4).lower()] + _ACC[m.group(5)]) % 12 if m.group(4) else root
    return root, pcs, bass


def parse_harmony(text, beats_per_bar=4):
    """Parse a chord chart into ``[(start_beat, beats, symbol), ...]``."""
    out = []
    for bi, bar in enumerate(text.replace('\n', ' ').split('|')):
        toks = bar.split()
        if not toks:
            continue
        syms, durs = [], []
        for tok in toks:
            sym, _, d = tok.partition(':')
            chord_pcs(sym)                                 # validate early
            syms.append(sym)
            durs.append(float(d) if d else None)
        free = beats_per_bar - sum(d for d in durs if d)
        nfree = sum(1 for d in durs if d is None)
        durs = [d if d else free / nfree for d in durs]
        if abs(sum(durs) - beats_per_bar) > 1e-6:
            raise NotationError(f'chord bar {bi + 1} has {sum(durs):g} beats: {bar.strip()!r}')
        t = bi * beats_per_bar
        for s, d in zip(syms, durs):
            out.append((t, d, s))
            t += d
    return out


def chord_at(harmony, beat):
    """The chord symbol sounding at ``beat`` (the last chord after the end)."""
    for s, d, sym in harmony:
        if s <= beat + 1e-6 < s + d:
            return sym
    return harmony[-1][2]


def voice_lead(prev, pcs, lo, hi, n):
    """Choose ``n`` chord tones in ``[lo, hi]`` that move least from ``prev``.

    Candidates must cover ``min(n, distinct pcs)`` pitch classes, span at most 19
    semitones and avoid close intervals below G3 (low-register mud). Without a previous
    voicing the one nearest the middle of the range wins. Returns an ascending list.
    """
    cands = [p for p in range(lo, hi + 1) if p % 12 in pcs]
    need = min(n, len(set(pcs)))
    best = None
    for combo in itertools.combinations(cands, n):
        if combo[-1] - combo[0] > 19 or len({c % 12 for c in combo}) < need:
            continue
        mud = sum(1 for a, b in zip(combo, combo[1:]) if a < 55 and b - a < 5)
        if prev:
            cost = sum(abs(a - b) for a, b in zip(sorted(prev), combo))
        else:
            cost = abs(sum(combo) / n - (lo + hi) / 2) * n * 0.5
        cost += mud * 6
        if best is None or cost < best[0]:
            best = (cost, combo)
    if best is None:
        raise NotationError(f'no {n}-note voicing of {pcs} in {lo}-{hi}')
    return list(best[1])
