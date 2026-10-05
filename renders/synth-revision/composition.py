# SPDX-License-Identifier: GPL-3.0-or-later
"""Glass Garden: D Dorian/Aeolian, 4/4, 96 bpm, 32 bars (80.0 s). FM bell lead, PWM pad,
reed counter-line, pulse-pluck arpeggio, round bass, kalimba, droplets and wooden drums.

The original soundtrack sits around 96 bpm, centred on D and G minor, with a walking
bass and mallet-like plucks. This piece keeps that tempo and tonal centre, the moving
bass and the plucked colour, in a warm electronic voice (Surge XT patches designed in
``patches.py`` plus the NumPy voices in ``glob2music.backends.dsp``).

Form and harmony
    A   1-8   Theme: a rising A-D-E-F figure over Dm9 - Bbmaj7 - Gm9 - A7sus4/A7, then
              Fmaj7/C and the signature Ebmaj7#11 (a lydian flat-II colour) back to A7.
    A'  9-16  The answer, with the reed counter-line entering; cadence into F major
              (C9 - Fmaj7 - Am7 Dm7 - Gm7 C7 - Fmaj9).
    B   17-24 Development through G minor, Eb and C minor: the theme's rising-fourth cell
              in sequence up to D6 at bar 21; an A7(b9) climax where melody and counter
              move in contrary motion (bar 24).
    A'' 25-32 Varied return winding down onto A7sus4 -> A7, which resolves into bar 1.

The intensity arc rises from 0.40 to 1.00 at bar 24 and falls back to 0.42, so the wrap
does not jump. Every arrangement change happens inside the timeline (bars 5, 9, 21, 30,
31), never at the wrap, so the seam measures like any other phrase boundary.

Moods (one timeline; melody and harmony identical in all three)
    calm      Lead, PWM pad, long bass notes that vary per section, sparse kalimba,
              the counter-line in bars 9-16 and 21-30, a few water-droplet "bloops".
    building  Adds the counter-line throughout, a quaver pulse-pluck arpeggio whose
              figure changes per section, a syncopated bass with approach notes, a
              shaker, and from bar 9 frame drum and log drums, with tom fills.
    combat    Semiquaver arpeggio in 3-3-2 accents, driving quaver octave bass, frame
              drum patterns per section, snaps on 2 and 4, a 3-3-2 log-drum figure,
              toms in B, taiko accents and fills; the lead climbs an octave in B.

Performance. The maintainer approved this piece by ear with its own humanisation, which
differs from the shared ``score.perform``: each part is generated together with its
velocities and timing offsets from fixed-seed ``random.Random`` streams, in a fixed
order, so the approved performance is reproduced exactly. ``arrange`` exposes the
written notes to the shared score layer (checks, score MIDI); ``perform_part`` is the
per-set performer hook (``build_trio(perform_fn=...)``) that returns the performed
events, quantised to 960 ticks per beat. ``lane`` gives the macro and fader-ride curves
(brightness, drive, ride) sampled every quaver.
"""
import itertools
import math
import random
import re

import numpy as np

from glob2music.score import Note, Part, PerformedPart, Score, chord_pcs, parse_harmony, parse_line
from glob2music.score.notation import QUALITIES

BPB = 4
BARS = 32
BPM = 96
BEAT = 60.0 / BPM
PPQ = 960                     # timing resolution of the performance, ticks per beat
LOOP_SECONDS = BARS * BPB * BEAT

HARMONY = """
Dm9 | Bbmaj7 | Gm9 | A7sus4 A7 | Dm9 | Fmaj7/C | Ebmaj7#11 | A7sus4 A7 |
Dm9 | Bbmaj7 | Gm9 | C9 | Fmaj7 | Am7 Dm7 | Gm7 C7 | Fmaj9 |
Gm9 | Ebmaj7 | Cm9 | F7sus4 F7 | Bbmaj7 | Gm7 Em7b5 | A7sus4 | A7b9 |
Dm9 | Bbmaj7 | Gm9 | Ebmaj7#11 | Dm/F Fmaj7 | Bbmaj7 | Gm7 Em7b5 | A7sus4 A7
"""

MELODY = """
r:e a4:e d5:e e5:e f5:q. e5:e | d5:q c5:e d5:e a4:h | r:e g4:e bb4:e d5:e f5:q e5:e d5:e | e5:h c#5:q. r:e |
r:e a4:e d5:e e5:e f5:q g5:e a5:e | c6:q. a5:e g5:q f5:q | g5:q. f5:e eb5:q d5:e bb4:e | d5:q e5:h r:q |
r:e a4:e d5:e e5:e f5:e a5:e g5:e f5:e | e5:e f5:e d5:h r:q | r:e bb4:e d5:e f5:e a5:q. g5:e | e5:q. d5:e bb4:q c5:q |
a4:q. c5:e f5:q e5:q | e5:q c5:q d5:q f5:q | g5:q f5:e d5:e e5:q bb4:e c5:e | g4:e a4:h. r:e |
r:e d5:e g5:e a5:e bb5:q. a5:e | g5:q f5:e g5:e d5:h | r:e eb5:e g5:e bb5:e d6:q. c6:e | bb5:q a5:q f5:q eb5:q |
d5:e f5:e a5:e c6:e d6:q. c6:e | bb5:q a5:e g5:e g5:e e5:e bb5:q | a5:q. g5:e e5:q d5:q | bb5:q g5:q e5:q c#5:q |
r:e a4:e d5:e e5:e f5:q. e5:e | d5:q c5:e d5:e a4:h | r:e g4:e bb4:e d5:e f5:q e5:e d5:e | eb5:q d5:e bb4:e a4:h |
r:e a4:e c5:e d5:e f5:q e5:e c5:e | d5:q. c5:e a4:h | bb4:q a4:e g4:e e4:q g4:q | a4:q. g4:e e4:q. r:e
"""

COUNTER = """
r:w | r:w | r:w | r:w |
f4:w | e4:w | g4:h bb4:h | d4:h c#4:h |
f4:h e4:h | d4:h f4:h | bb4:h a4:h | g4:h e4:h |
f4:q. g4:e a4:h | g4:h f4:h | d4:q f4:q e4:h | f4:h. e4:q |
d4:h f4:h | g4:h bb4:h | g4:h. bb4:q | c4:h eb4:h |
d4:h f4:h | d4:w | d4:h e4:h | c#4:q e4:q g4:q bb4:q |
a4:h f4:h | f4:h d4:h | d4:h f4:h | g4:h bb4:q g4:q |
a4:h g4:h | f4:w | d4:h bb3:h | d4:h c#4:h
"""

#: The dynamic arc, one value per bar.
ARC = [0.40, 0.42, 0.45, 0.48, 0.50, 0.55, 0.58, 0.55, 0.55, 0.58, 0.62, 0.65, 0.66, 0.68, 0.70, 0.62,
       0.68, 0.72, 0.76, 0.80, 0.86, 0.92, 0.98, 1.00, 0.80, 0.74, 0.70, 0.66, 0.60, 0.55, 0.48, 0.42]

_harmony = parse_harmony(HARMONY, BPB)
_melody = parse_line(MELODY, BPB, part='melody')
_counter = parse_line(COUNTER, BPB, part='counter')


def _chord(symbol):
    """``{'root', 'bass', 'tones', 'ivs'}`` of a chord symbol; ``ivs`` are the quality's
    intervals (9ths as 14, #11 as 18), which the voicing rules test."""
    root, tones, bass = chord_pcs(symbol)
    quality = re.fullmatch(r'[A-G](?:#|b)?([a-z0-9#]*)(?:/.*)?', symbol).group(1)
    return dict(root=root, bass=bass, tones=tones, ivs=QUALITIES[quality])


#: ``[(start_beat, beats, chord)]``.
SPANS = [(s, d, _chord(sym)) for s, d, sym in _harmony]


def _bass_pitch(pc, lo=38):
    """A chord's bass pitch class placed in D2..C#3."""
    p = lo + (pc - lo) % 12
    return p if p <= 49 else p - 12


SCORE = Score(title='glass-garden', bpm=BPM, beats_per_bar=BPB, bars=BARS, harmony=_harmony, melody=_melody,
              counter=_counter, bass=[Note(s, d, [_bass_pitch(c['bass'])]) for s, d, c in SPANS], intensity=ARC,
              phrases=[(b, b + 3) for b in range(1, BARS + 1, 4)],
              sections={'A': (1, 8), "A'": (9, 16), 'B': (17, 24), "A''": (25, 32)})


def arc(beat):
    """``ARC`` as a smooth, loop-periodic curve (bar centres joined by smoothsteps)."""
    x = beat / BPB - 0.5
    i = math.floor(x)
    f = x - i
    f = f * f * (3 - 2 * f)
    return ARC[i % BARS] + (ARC[(i + 1) % BARS] - ARC[i % BARS]) * f


def section(bar):
    """Section of a 1-based bar: 'A', 'A2' (A'), 'B' or 'A3' (A'')."""
    return 'A' if bar <= 8 else 'A2' if bar <= 16 else 'B' if bar <= 24 else 'A3'


# ----------------------------------------------------------------------------- humanisation

class Human:
    """One part's humaniser: ``t()`` timing offset in seconds (Gaussian, clipped to
    20 ms), ``v()`` velocity offset. Seeded per part, so every mood plays a shared part
    identically and crossfades stay phase-coherent."""

    def __init__(self, seed, t_sd=0.006, v_sd=5.0):
        self.r = random.Random(seed)
        self.t_sd = t_sd
        self.v_sd = v_sd

    def t(self):
        return max(-0.02, min(0.02, self.r.gauss(0, self.t_sd)))

    def v(self):
        return self.r.gauss(0, self.v_sd)


def phrase_velocities(notes, base, span, human, accent_downbeat=4):
    """Velocity = base + arc + contour and position within each 2-bar phrase + metric
    accent + a little for long notes + noise."""
    out = []
    for n in notes:
        ph0 = (int(n['start'] // 4) // 2) * 8.0
        group = [m['pitch'] for m in notes if ph0 <= m['start'] < ph0 + 8]
        lo, hi = min(group), max(group)
        contour = 0.0 if hi == lo else (n['pitch'] - lo) / (hi - lo)
        shape = math.sin(math.pi * min(1.0, (n['start'] - ph0) / 8.0 * 1.15)) * 0.5 + contour * 0.5
        metric = accent_downbeat if abs(n['start'] % 4) < 1e-6 else (2 if abs(n['start'] % 1) < 1e-6 else 0)
        out.append(base + span * arc(n['start']) + 10 * shape + metric + min(6, n['dur'] * 3) + human.v())
    return out


def _dicts(notes):
    return [dict(start=n.start, dur=n.dur, pitch=n.pitches[0]) for n in notes]


# ----------------------------------------------------------------------------- voicing and parts
# Each part is a list of dicts: start and dur in beats, pitch, vel (float), dt (seconds).

def voice_chords(lo=50, hi=74, nv=4):
    """Four-voice pad voicings: guide tones (3rd/sus4, 7th) required, colour tones (b9,
    9, #11) preferred, the root dropped when there are spare tones, smooth voice leading,
    no unchanged voicing on a chord change. Returns ``[(start, beats, chord, voicing)]``."""
    prev, out = None, []
    for st, du, c in SPANS:
        pcs = list(dict.fromkeys(c['tones']))
        if len(pcs) > nv:
            sets = [s for s in itertools.combinations(pcs, nv) if c['root'] not in s or len(pcs) == 4]
        else:
            sets = [tuple(pcs)]
        best = None
        for chosen in sets or list(itertools.combinations(pcs, nv)):
            for combo in itertools.product(*[[p for p in range(lo, hi + 1) if p % 12 == pc] for pc in chosen]):
                v = sorted(combo)
                if len(set(v)) < len(v):
                    continue
                gaps = [b - a for a, b in zip(v, v[1:])]
                cost = sum(3 for g in gaps if g < 2) + sum(1.5 for g in gaps if g > 7)
                if v[1] - v[0] < 3 and v[0] < 57:
                    cost += 3
                cost += abs(sum(v) / len(v) - 63) * 0.25
                if prev:
                    cost += sum(abs(a - b) for a, b in zip(v, prev)) * 0.6
                for gi in (3, 4, 10, 11, 5):
                    if gi in c['ivs'] and (c['root'] + gi) % 12 not in chosen:
                        cost += 8
                for gi in (13, 14, 18):
                    if gi in c['ivs'] and (c['root'] + gi) % 12 not in chosen:
                        cost += 3
                if prev and v == prev:
                    cost += 4
                if best is None or cost < best[0]:
                    best = (cost, v)
        prev = best[1]
        out.append((st, du, c, best[1]))
    return out


VOICED = voice_chords()


def part_melody(octave_up_in_b=False, seed=11):
    """The lead: stepwise neighbours are slurred (overlapping, so the mono patch glides);
    in combat, bars 17-24 move up an octave."""
    notes = _dicts(_melody)
    h = Human(seed, 0.007, 4.0)
    vels = phrase_velocities(notes, 58, 40, h)
    out = []
    for i, (n, v) in enumerate(zip(notes, vels)):
        nxt = notes[i + 1] if i + 1 < len(notes) else None
        slur = nxt is not None and abs(nxt['start'] - (n['start'] + n['dur'])) < 1e-6 and abs(nxt['pitch'] - n['pitch']) <= 2
        dur = n['dur'] + 0.06 if slur else n['dur'] * 0.92
        p = n['pitch'] + (12 if octave_up_in_b and 17 <= n['start'] // 4 + 1 <= 24 and n['pitch'] < 84 else 0)
        out.append(dict(start=n['start'], dur=dur, pitch=p, vel=v, dt=h.t() + (0.006 if n['dur'] >= 1.5 else 0)))
    return out


def part_counter(bars_on, seed=23):
    notes = [n for n in _dicts(_counter) if int(n['start'] // 4) + 1 in bars_on]
    h = Human(seed, 0.008, 4.0)
    vels = phrase_velocities(notes, 46, 34, h, accent_downbeat=2)
    out = []
    for i, (n, v) in enumerate(zip(notes, vels)):
        nxt = notes[i + 1] if i + 1 < len(notes) else None
        legato = nxt is not None and abs(nxt['start'] - (n['start'] + n['dur'])) < 1e-6
        out.append(dict(start=n['start'], dur=n['dur'] + (0.05 if legato else -0.1), pitch=n['pitch'], vel=v,
                        dt=h.t() + 0.008))
    return out


def part_pad(base, span=26, seed=31):
    """Held chords; common tones are tied, not re-struck; voices enter staggered."""
    h = Human(seed, 0.012, 3.0)
    out, active = [], {}
    for st, du, c, v in VOICED:
        nxt_active = {}
        for k, p in enumerate(v):
            if p in active and abs(active[p]['start'] + active[p]['dur'] - 0.12 - st) < 1e-6 and du > 0:
                n = active[p]
                n['dur'] += du
            else:
                stag = 0.09 * k + 0.08 * h.r.random()
                n = dict(start=st + stag, dur=du + 0.12 - stag, pitch=p, vel=base + span * arc(st) + h.v() - 3 * k,
                         dt=abs(h.t()))
                out.append(n)
            nxt_active[p] = n
        active = nxt_active
    return out


def part_bass(mood, seed=41):
    """Calm: long notes varied per section; building: syncopated figure with fifth and
    approach note; combat: driving quavers with octave jumps and ghosted off-beats."""
    h = Human(seed, 0.005, 4.0)
    out = []
    for idx, (st, du, c) in enumerate(SPANS):
        nxt = SPANS[(idx + 1) % len(SPANS)][2]
        root = _bass_pitch(c['bass'])
        fifth = root + 7 if root + 7 <= 52 else root - 5
        up = root + 12 if root < 45 else root
        approach = _bass_pitch(nxt['bass']) - 1 if idx % 3 else _bass_pitch(nxt['bass']) + 2
        bar = int(st // 4) + 1
        a = arc(st)
        sec = section(bar)
        if mood == 'calm':
            v0 = 58 + 22 * a
            if du == 2.0 or sec == 'A' and bar % 4 != 0:
                pat = [(0, du - 0.05, root, 0)]
            elif sec == 'A2':
                pat = [(0, 1.9, root, 0), (2, 1.9, fifth if bar % 2 else root + 12 if root < 45 else root - 12, -8)]
            elif sec == 'B' or not (bar % 2 == 1 and bar % 4 != 0):
                pat = [(0, 2.9, root, 0), (3, 0.9, approach, -10)]
            else:
                pat = [(0.5, 3.4, root, -4)]                     # a pushed entry
        elif mood == 'building':
            v0 = 62 + 26 * a
            pat = [(0, 1.4, root, 8), (1.5, 0.45, fifth, -4), (2.5, 0.9, up, 0), (3.5, 0.45, approach, -6)]
            if du == 2.0:
                pat = [(0, 1.4, root, 8), (1.5, 0.45, approach if idx % 2 else fifth, -6)]
            if sec == 'B' and du == 4.0:
                pat = [(0, 0.9, root, 8), (1.0, 0.45, root, -6), (1.5, 0.9, fifth, 0), (2.5, 0.45, up, -2),
                       (3.0, 0.45, fifth, -4), (3.5, 0.45, approach, -6)]
        else:
            v0 = 70 + 24 * a
            cells = int(du * 2)
            pat = []
            for k in range(cells):
                p = approach if k == cells - 1 else up if k % 4 == 2 else fifth if k % 4 == 3 else root
                pat.append((k * 0.5, 0.4 if k % 2 else 0.46, p, 4 if k == 3 else 10 if k % 2 == 0 else -10))
        for o, d, p, acc in pat:
            out.append(dict(start=st + o, dur=d, pitch=p, vel=v0 + acc + h.v(), dt=h.t()))
    return out


ARP_SHAPES = {'A': [0, 1, 2, 3, 2, 1, 0, 1], 'A2': [0, 2, 1, 3, 2, 0, 3, 1], 'B': [0, 1, 2, 3, 1, 2, 3, 4],
              'A3': [0, 1, 2, 3, 2, 1, 0, 2]}


def part_arp(mood, seed=53):
    """The pad voicing lifted above C4: quavers in building, semiquavers with 3-3-2
    accents in combat; the figure changes per section."""
    h = Human(seed, 0.004, 5.0)
    out = []
    for st, du, c, v in VOICED:
        sec = section(int(st // 4) + 1)
        a = arc(st)
        tones = sorted({t + 12 if t < 60 else t for t in sorted(v) + [v[0] + 12, v[1] + 12]})
        if mood == 'building':
            step, shape = 0.5, ARP_SHAPES[sec]
        else:
            step, shape = 0.25, ARP_SHAPES[sec] + [s + 1 for s in ARP_SHAPES[sec]]
        for k in range(int(round(du / step))):
            g = int(round((st + k * step) / step))
            if mood == 'combat':
                acc = (14 if g % 16 in (0, 3, 6, 8, 11, 14) else -8) + (4 if g % 16 in (0, 8) else 0)
            else:
                acc = (8 if g % 2 == 0 else -4) + (6 if g % 8 == 3 else 0)
            out.append(dict(start=st + k * step, dur=step * 0.8, pitch=tones[shape[g % len(shape)] % len(tones)],
                            vel=50 + 30 * a + acc + h.v(), dt=h.t()))
    return out


KALIMBA_RHYTHMS = {  # semiquaver positions in the bar, rotated so neighbours differ
    'A': [[2, 10], [6, 12], [3, 11, 14], [2, 8]],
    'A2': [[2, 7, 10], [4, 10, 14], [2, 6, 11], [6, 9, 14]],
    'B': [[2, 5, 10, 13], [3, 6, 10, 14], [2, 6, 9, 12], [1, 6, 10, 14]],
    'A3': [[2, 10, 13], [6, 11], [3, 10], [4, 12]]}


def part_kalimba(seed=67):
    """Sparse chord tones between G4 and E6 moving by small steps, mostly where the
    melody is not starting a note."""
    r = random.Random(seed)
    h = Human(seed + 1, 0.006, 6.0)
    mel_on = {round(n.start * 4) for n in _melody}
    out, last = [], 74
    for bar in range(1, BARS + 1):
        pats = KALIMBA_RHYTHMS[section(bar)]
        for pos in pats[(bar * 3 + bar // 4) % len(pats)]:
            beat = (bar - 1) * 4 + pos / 4.0
            if round(beat * 4) in mel_on and r.random() < 0.6:
                continue
            pcs = next(vc[2]['tones'] for vc in VOICED if vc[0] <= beat < vc[0] + vc[1])
            cands = [p for p in range(67, 89) if p % 12 in pcs]
            cands.sort(key=lambda p: abs(p - last) + r.random() * 4 + (6 if p == last else 0))
            last = cands[0]
            out.append(dict(start=beat, dur=0.5, pitch=last, vel=48 + 30 * arc(beat) + h.v() + (6 if pos % 4 == 0 else 0),
                            dt=h.t()))
    return out


def part_bloops(density, seed=79):
    """Occasional water-droplet blips on the D minor pentatonic, sometimes in pairs."""
    r = random.Random(seed)
    scale = [62, 65, 67, 69, 72, 74, 77, 79, 81, 84]
    out = []
    for bar in range(1, BARS + 1):
        if r.random() < density:
            pos = r.choice([1.5, 2.25, 2.75, 3.5, 0.75])
            out.append(dict(start=(bar - 1) * 4 + pos, dur=0.25, pitch=r.choice(scale), vel=50 + r.random() * 30, dt=0.0))
            if r.random() < 0.35:
                out.append(dict(start=(bar - 1) * 4 + pos + 0.25, dur=0.25, pitch=r.choice(scale),
                                vel=40 + r.random() * 25, dt=0.0))
    return out


#: Percussion note map, played by ``backends.dsp`` voice ``kit``.
FRAME, SNAP, TOM_LO, TOM_MID, TOM_HI, SHAKER, TOK_HI, TOK_LO, TAIKO = 36, 39, 41, 45, 48, 70, 76, 77, 35


def part_perc(mood, seed=97):
    """Building: shaker quavers, frame drum and log drums in bars 9-30, tom fills.
    Combat: swung semiquaver shaker, frame-drum patterns per section, snaps on 2 and 4,
    a 3-3-2 log-drum figure, toms in B, taiko accents and fills."""
    h = Human(seed, 0.004, 6.0)
    r = random.Random(seed)
    out = []

    def hit(beat, note, vel, swing=0.0):
        sw = swing if abs((beat * 4) % 2 - 1) < 1e-6 else 0.0      # delay odd semiquavers
        out.append(dict(start=beat + sw, dur=0.1, pitch=note, vel=vel + h.v(), dt=h.t()))

    for bar in range(1, BARS + 1):
        b0 = (bar - 1) * 4.0
        sec = section(bar)
        a = arc(b0)
        if mood == 'building':
            for k in range(8):
                hit(b0 + k * 0.5, SHAKER, 36 + 30 * a + (12 if k % 2 else 0))
            if 9 <= bar <= 30:
                hit(b0, FRAME, 62 + 20 * a)
                if sec == 'B':
                    hit(b0 + 2.5, FRAME, 48 + 20 * a)
                hit(b0 + 1.5, TOK_LO, 44 + 20 * a)
                hit(b0 + 3.0, TOK_HI, 50 + 20 * a)
                if bar % 2 == 0:
                    hit(b0 + 3.75, TOK_HI, 36 + 15 * a)
            if bar % 8 == 0:
                for k, (o, n) in enumerate([(3.0, TOM_HI), (3.25, TOM_MID), (3.5, TOM_MID), (3.75, TOM_LO)]):
                    hit(b0 + o, n, 55 + 20 * a + 4 * k)
        else:
            for k in range(16):
                hit(b0 + k * 0.25, SHAKER, 34 + 22 * a + (14 if k % 4 == 2 else 6 if k % 2 else 0), swing=0.03)
            kick = [0, 1.5, 2.0] if sec in ('A', 'A3') else [0, 0.75, 2.0, 2.5, 3.5]
            if bar % 4 == 3:
                kick = [0, 1.5, 2.0, 3.25]
            for o in kick:
                hit(b0 + o, FRAME, 80 + 22 * a - (10 if o % 1 else 0))
            hit(b0 + 1, SNAP, 72 + 20 * a)
            hit(b0 + 3, SNAP, 76 + 20 * a)
            if r.random() < 0.4:
                hit(b0 + 3.75, SNAP, 40 + 10 * a)
            for o, n in [(0, TOK_LO), (0.75, TOK_HI), (1.5, TOK_LO), (2.0, TOK_HI), (2.75, TOK_LO), (3.5, TOK_HI)]:
                hit(b0 + o, n, 46 + 20 * a)
            if sec == 'B':
                for o, n in [(0.5, TOM_LO), (1.25, TOM_MID), (2.5, TOM_LO), (3.25, TOM_MID)]:
                    hit(b0 + o, n, 58 + 22 * a)
            if bar in (1, 9, 17, 21, 25):
                hit(b0, TAIKO, 100)
            if bar % 8 == 0:
                for k, n in enumerate([TOM_HI, TOM_HI, TOM_MID, TOM_MID, TOM_LO, TOM_LO, TOM_MID, TOM_LO]):
                    hit(b0 + 2 + k * 0.25, n, 64 + 3 * k + 18 * a)
    return out


def generate(mood):
    """``{part: [dict]}`` for ``mood``, with velocities and timing (the performance)."""
    lead = part_melody(octave_up_in_b=(mood == 'combat'))
    for n in lead:
        n['vel'] *= {'calm': 0.8, 'building': 0.92, 'combat': 1.0}[mood]
    counter_bars = set(range(9, 17)) | set(range(21, 31)) if mood == 'calm' else set(range(5, 33))
    parts = {'lead': lead, 'counter': part_counter(counter_bars),
             'pad': part_pad({'calm': 50, 'building': 46, 'combat': 44}[mood]), 'bass': part_bass(mood)}
    if mood != 'combat':
        parts['kalimba'] = part_kalimba()
        parts['bloops'] = part_bloops({'calm': 0.55, 'building': 0.3}[mood])
    if mood != 'calm':
        parts['arp'] = part_arp(mood)
        parts['perc'] = part_perc(mood)
    return parts


#: Part -> (instrument key, role in the shared score layer).
PLAYERS = {'lead': ('glass_lead', 'lead'), 'counter': ('reed_counter', 'counter'), 'pad': ('glass_pad', 'pad'),
           'bass': ('round_bass', 'bass'), 'kalimba': ('kalimba', 'harp'), 'bloops': ('bloop', 'sparkle'),
           'arp': ('pulse_pluck', 'ostinato'), 'perc': ('kit', 'perc')}
_PERFORMANCES = {}


def _performance(mood):
    if mood not in _PERFORMANCES:
        _PERFORMANCES[mood] = generate(mood)
    return _PERFORMANCES[mood]


def arrange(mood):
    """The written notes of every part in ``mood`` (humanisation removed)."""
    return [Part(name, PLAYERS[name][0], [Note(n['start'], n['dur'], [n['pitch']]) for n in notes], PLAYERS[name][1])
            for name, notes in _performance(mood).items()]


def _events(notes):
    """Performed notes -> events: same-pitch overlaps shortened, times quantised to
    ``PPQ`` ticks with the humanised offset, note-offs before note-ons on the same tick,
    velocities rounded and clamped. Returns ``[(on_s, off_s, pitch, velocity)]`` sorted."""
    notes = sorted((dict(n) for n in notes), key=lambda n: (n['pitch'], n['start']))
    for a, b in zip(notes, notes[1:]):
        if a['pitch'] == b['pitch'] and a['start'] + a['dur'] + a['dt'] / BEAT > b['start'] + b['dt'] / BEAT - 0.02:
            a['dur'] = max(0.05, b['start'] - a['start'] - 0.03)
    per_s = PPQ / BEAT
    msgs = []
    for n in notes:
        on = max(0, int(round(n['start'] * PPQ + n['dt'] * per_s)))
        off = max(on + 30, int(round((n['start'] + n['dur']) * PPQ + n['dt'] * per_s)))
        msgs.append((on, 1, n['pitch'], int(max(1, min(127, round(n['vel']))))))
        msgs.append((off, 0, n['pitch'], 0))
    msgs.sort(key=lambda m: (m[0], m[1]))
    out, sounding = [], {}
    for tick, is_on, pitch, vel in msgs:
        sec = tick * BEAT / PPQ
        if is_on:
            sounding[pitch] = (sec, vel)
        elif pitch in sounding:
            on, v = sounding.pop(pitch)
            out.append((on, sec, pitch, v))
    return sorted(out)


def perform_part(score, mood, part, instrument, seed):
    """Per-set performer hook for ``build_trio``: the approved performance of ``part``
    (``seed`` is unused; the generators carry their own fixed seeds). No CC11: the fader
    ride is applied in the set's mix (``mixdown.py``) from ``lane(mood, 'expr')``."""
    return PerformedPart(part.name, part.instrument, part.role, _events(_performance(mood)[part.name]))


def lane(mood, name):
    """Automation lane ``'bright'`` (Surge macro 1), ``'grit'`` (macro 2) or ``'expr'``
    (fader ride): ``(seconds, value 0..1)`` every quaver, quantised to 7-bit MIDI values.
    Interpolate it periodically over ``LOOP_SECONDS`` (``lane_at``)."""
    pts = []
    for k in range(BARS * 8):
        beat = k * 0.5
        a = arc(beat)
        phrase = math.sin(math.pi * ((beat % 16) / 16.0)) ** 2
        if name == 'expr':
            v = 0.72 + 0.28 * a
        elif name == 'bright':
            v = {'calm': 0.10, 'building': 0.25, 'combat': 0.45}[mood] + 0.35 * a + 0.12 * phrase
        else:
            v = {'calm': 0.0, 'building': 0.15, 'combat': 0.45}[mood] + 0.4 * a * (1 if mood != 'calm' else 0.2)
        pts.append((beat * BEAT, int(round(max(0.0, min(1.0, v)) * 127)) / 127.0))
    return pts


def lane_at(points, seconds):
    """Value of a ``lane`` at ``seconds`` (array or scalar), wrapping at the loop."""
    t, v = np.array([p[0] for p in points]), np.array([p[1] for p in points])
    return np.interp(np.asarray(seconds) % LOOP_SECONDS, t, v, period=LOOP_SECONDS)


_LANES = {}


def MACROS(mood, part, seconds):
    """Surge macro values at ``seconds`` from the start of the loop."""
    if mood not in _LANES:
        _LANES[mood] = (lane(mood, 'bright'), lane(mood, 'grit'))
    bright, grit = _LANES[mood]
    return {'m1': float(lane_at(bright, seconds)), 'm2': float(lane_at(grit, seconds))}

# Targeted repair: soften the combat percussion and arpeggio at the loop boundary.
MIX_ADJUST={'combat':{'perc':-9,'arp':-3}}
