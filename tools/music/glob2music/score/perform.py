# SPDX-License-Identifier: GPL-3.0-or-later
"""Humanisation and articulation: from written notes to performed note events.

Pipeline role: ``perform_part`` turns one ``Part`` of a ``Score`` into a
``PerformedPart``: note events in seconds with velocities, plus a CC11 (expression)
curve for sustained instruments. Rendering backends consume ``PerformedPart`` only, so
a sample backend (sfizz) and a synthesiser backend play the same performance.

What a performance adds to the written notes, and why (the rejected first attempts had
every note equally loud and locked to the grid):

* **Velocity** = role range x (piece intensity arc + phrase arch) + metric accent +
  melodic contour (higher notes in a line slightly louder) + marked accents, minus a
  little at phrase ends, plus a small random term. The mood shifts the whole range
  (``MOOD_VELOCITY``): calm plays softer, combat harder.
* **Timing**: Gaussian jitter per role (4-14 ms), plus the instrument's ``lag`` (slow
  bowed and blown attacks are played early so they *sound* on the beat). Calm leads sit
  6 ms behind the beat, which reads as relaxed.
* **Length** by articulation family: legato lines overlap the next note by 35 ms
  (a repeated pitch is re-articulated instead), staccato is 38 %, pads and basses
  hold to the next chord, short patches are clipped, ringing instruments ring.
* **Expression** (CC11, sustained instruments): the intensity arc, a messa di voce on
  long notes and a slow half-bar "breathing" on pads.

Randomness: each player's ``random.Random`` is seeded from ``md5(namespace + part
name)``, so the same player is humanised identically in all three moods (crossfades
change the arrangement, not the timing), and a build is reproducible. The namespace
is the set id (``seed_namespace`` adds the manifest seed when it is not 0).

One rule protects every backend: a note-off must never arrive after the next note-on
of the same key, or it would release the new note. ``perform_part`` shortens the
earlier note to end 20 ms before the repeat.
"""
from dataclasses import dataclass, field
import hashlib
import math
import random

#: Per role: (lowest velocity, highest velocity, timing jitter SD in seconds).
ROLES = {
    'lead': (52, 104, 0.009), 'counter': (42, 92, 0.011), 'pad': (36, 84, 0.014),
    'harp': (40, 96, 0.006), 'bass': (48, 98, 0.007), 'pizz': (40, 92, 0.009),
    'perc': (46, 112, 0.004), 'perc_soft': (28, 78, 0.006), 'perc_snare': (30, 88, 0.004),
    'sparkle': (36, 82, 0.005), 'low_drum': (50, 112, 0.004), 'bass_sus': (48, 96, 0.010),
    'lead_dbl': (40, 86, 0.010), 'ostinato': (46, 100, 0.005),
}
#: Velocity offset per mood.
MOOD_VELOCITY = {'calm': -12, 'building': 0, 'combat': 9}
#: CC11 offset per mood.
MOOD_EXPRESSION = {'calm': -10, 'building': 0, 'combat': 8}
#: CC11 resolution, seconds.
CC_STEP = 0.025
#: Roles whose loudness follows the melodic contour and that play legato.
LINE_ROLES = ('lead', 'counter', 'lead_dbl')


@dataclass
class PerformedPart:
    """A part ready to render.

    ``events`` are ``(on_s, off_s, midi_pitch, velocity)`` sorted by onset, in seconds
    from the start of the loop (onsets may be slightly negative: those notes are played
    early and belong to the end of the previous pass). ``cc11`` is ``[(t_s, value)]``,
    empty for instruments without expression.
    """

    name: str
    instrument: str
    role: str
    events: list
    cc11: list = field(default_factory=list)
    pan: float = None


def seed_namespace(set_id, seed=0):
    """The humanisation namespace for a build: the set id, plus the seed if non-zero."""
    return set_id if not seed else f'{set_id}#{seed}'


def player_seed(namespace, part_name):
    """Deterministic integer seed for one player."""
    return int(hashlib.md5(f'{namespace}{part_name}'.encode()).hexdigest()[:8], 16)


def phrase_arc(beat, phrases, bpb):
    """0..1 arch over each phrase: rises to ~62 % of it, then relaxes to ~0.55."""
    for a, b in phrases:
        s, e = (a - 1) * bpb, b * bpb
        if s <= beat < e:
            x = (beat - s) / (e - s)
            if x < 0.62:
                return math.sin(math.pi * min(1.0, x / 1.24))
            return 0.55 + 0.45 * math.cos(math.pi * (x - 0.62) / 0.76)
    return 0.5


def metric_accent(beat, bpb):
    """Velocity offset by metric position in simple metres: downbeat +6, the middle
    of a 4/4 bar +3, other beats 0, quavers -3, semiquavers -6."""
    f = beat % bpb
    if abs(f) < 1e-6:
        return 6
    if bpb == 4 and abs(f - 2) < 1e-6:
        return 3
    if abs(f - round(f)) < 1e-6:
        return 0
    if abs(f * 2 - round(f * 2)) < 1e-6:
        return -3
    return -6


def perform_part(score, mood, part, instrument, seed):
    """Humanise ``part`` (a ``model.Part``) of ``score`` for ``mood``.

    ``instrument`` is the ``model.Instrument`` the part plays (its ``kind`` and
    ``lag``); ``seed`` comes from ``player_seed``. Returns a ``PerformedPart``.
    """
    rng = random.Random(seed)
    vmin, vmax, sd = ROLES[part.role]
    tm = score.tempo
    bpb = score.beats_per_bar
    accent = score.accent or (lambda beat: metric_accent(beat, bpb))
    notes = sorted(part.notes, key=lambda n: (n.start, n.pitches))
    starts = [n.start for n in notes]
    events = []
    for i, n in enumerate(notes):
        intensity = score.intensity_at(n.start)
        arc = phrase_arc(n.start, score.phrases, bpb)
        contour = 0.0
        if part.role in LINE_ROLES:
            near = [m.pitches[0] for m in notes if abs(m.start - n.start) < 8]
            contour = (n.pitches[0] - min(near)) / max(1, max(near) - min(near)) - 0.5
        v = vmin + (vmax - vmin) * (0.85 * intensity + 0.25 * arc - 0.08) + 14 * contour
        v += MOOD_VELOCITY[mood] + accent(n.start)
        if '>' in n.artic or '^' in n.artic:
            v += 10
        if '!' in n.artic:
            v -= 2
        for a, b in score.phrases:                       # phrase-final notes relax
            if abs(n.end - b * bpb) < 1.01 and n.dur >= 1:
                v -= 5
        v += rng.gauss(0, 3.0)
        vel = int(max(12, min(124, round(v))))

        on = tm.sec(n.start) + rng.gauss(0, sd) + instrument.lag
        if part.role == 'lead' and mood == 'calm':
            on += 0.006
        nominal = tm.sec(n.end) - tm.sec(n.start)
        if instrument.kind == 'sustain':
            if '!' in n.artic:
                off = on + max(0.09, 0.38 * nominal)
            else:
                legato = part.role in LINE_ROLES and any(abs(s - n.end) < 1e-6 for s in starts[i + 1:i + 6])
                repeat = any(abs(m.start - n.end) < 1e-6 and m.pitches == n.pitches for m in notes[i + 1:i + 6])
                if legato and repeat:
                    off = tm.sec(n.end) - 0.03            # re-articulate a repeated pitch
                elif legato:
                    off = tm.sec(n.end) + 0.035           # overlap: legato
                elif part.role in ('pad', 'bass'):
                    off = tm.sec(n.end) + 0.02
                elif '-' in n.artic:
                    off = on + 0.97 * nominal
                else:
                    off = on + 0.90 * nominal
        elif instrument.kind == 'short':
            off = on + min(nominal * 0.8, 0.35)
        elif instrument.kind == 'ring':
            off = on + max(nominal, 0.6)
        else:
            off = on + 0.15
        for p in n.pitches:
            chord = len(n.pitches) > 1
            dv = rng.randint(-5, 3) if chord else 0
            events.append((on + (rng.gauss(0, 0.006) if chord else 0), off, p, max(10, min(124, vel + dv))))

    # A note-off after the next note-on of the same key would release the new note.
    events.sort()
    last = {}
    for i, (on, off, p, vel) in enumerate(events):
        j = last.get(p)
        if j is not None:
            pon, poff, pp, pv = events[j]
            if poff > on - 0.02:
                events[j] = (pon, max(pon + 0.05, on - 0.02), pp, pv)
        last[p] = i

    cc = []
    if instrument.kind == 'sustain':
        sustains = [(tm.sec(n.start), tm.sec(n.end), n.dur) for n in notes]
        t = 0.0
        while t < tm.total:
            beat = tm.beat(t)
            base = 48 + 72 * score.intensity_at(beat) + MOOD_EXPRESSION[mood]
            swell = 0.0
            for s, e, d in sustains:                     # messa di voce on long notes
                if s <= t < e and d >= 1.5:
                    x = (t - s) / max(1e-3, e - s)
                    swell = 12 * math.sin(math.pi * min(1.0, x * 1.25)) - (6 * x if part.role != 'pad' else 0)
                    break
            if part.role == 'pad':                       # slow breathing, one cycle per two bars
                swell += 5 * math.sin(2 * math.pi * beat / (bpb * 2))
            cc.append((t, int(max(30, min(127, base + swell)))))
            t += CC_STEP
    return PerformedPart(part.name, part.instrument, part.role, events, cc, part.pan)
