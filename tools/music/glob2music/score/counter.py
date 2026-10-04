# SPDX-License-Identifier: GPL-3.0-or-later
"""Counter-lines: a constraint-based line writer and hand-written overrides.

Pipeline role: a composition's answering voice is either written by hand
(``notation.parse_line``) or drafted here and then corrected by hand. ``counter_line``
picks one chord tone per harmony segment by dynamic programming:

* chord tones only; guide tones (third, seventh) preferred, the bass pitch class
  avoided (doubling the bass thickens the texture and invites parallel octaves);
* it stays on its side of the melody (below, or above for a descant) and avoids
  unisons and octaves with it at segment starts;
* parallel fifths and octaves against the bass *and* the melody cost far more than any
  leap, so the solver only accepts one when nothing else fits;
* common tones are slightly penalised and steps favoured, so the line moves.

A third between two chosen notes is then filled with a passing note on the segment's
last beat, taken from the key's major scale (minus the diatonic neighbours of any
chromatic chord tone) and never a semitone from the melody. ``override`` replaces bars
of the draft with hand-written answers where the melody holds or rests.

The solver is deterministic: the same harmony, melody and bass give the same line.
"""
from .notation import Note, chord_pcs, parse_line

MAJOR = (0, 2, 4, 5, 7, 9, 11)


def major_scale(key_pc):
    """Pitch classes of the major scale on ``key_pc`` (Dorian on D = major on C, etc.)."""
    return {(x + key_pc) % 12 for x in MAJOR}


def sounding(line, beat):
    """First pitch of the note of ``line`` sounding at ``beat``, or ``None``."""
    for n in line:
        if n.start - 1e-6 <= beat < n.start + n.dur - 1e-6:
            return n.pitches[0]
    return None


def counter_line(harmony, melody, bass, lo, hi, key_pc, seed_pitch=None, fill=True, above=False):
    """Draft a counter-line in ``[lo, hi]`` (MIDI) over ``harmony``.

    ``key_pc`` is the tonic of the major scale used for passing notes (for a modal
    piece, the major scale with the same notes: E Dorian -> 2, D major). ``seed_pitch``
    pulls the first note towards a register. ``above=True`` writes a descant above the
    melody instead of a line below it. Returns a ``Note`` list, one or two notes per
    harmony segment.
    """
    segs = list(harmony)
    cands = []
    for t, d, sym in segs:
        _, pcs, bpc = chord_pcs(sym)
        third = pcs[1] if len(pcs) > 1 else pcs[0]
        seventh = pcs[3] if len(pcs) > 3 else None
        mel = sounding(melody, t)
        opts = []
        for p in range(lo, hi + 1):
            if p % 12 not in pcs:
                continue
            c = 0.0
            if p % 12 == third or (seventh is not None and p % 12 == seventh):
                c -= 2.0
            if p % 12 == bpc:
                c += 2.5
            if mel is not None:
                if (not above and p >= mel - 2) or (above and p <= mel + 2):
                    c += 8.0                       # wrong side of the melody
                if (mel - p) % 12 == 0:
                    c += 3.0                       # unison / octave with the melody
            opts.append((p, c))
        cands.append(opts)

    # Dynamic programming over segments; state = chosen pitch. Dict insertion order and
    # the strict '<' make tie-breaking deterministic.
    best = [{p: (c + (abs(p - seed_pitch) * 0.3 if seed_pitch else 0), None) for p, c in cands[0]}]
    for i in range(1, len(segs)):
        t0, t1 = segs[i - 1][0], segs[i][0]
        b0, b1 = sounding(bass, t0), sounding(bass, t1)
        m0, m1 = sounding(melody, t0), sounding(melody, t1)
        layer = {}
        for p, c in cands[i]:
            bestc, arg = float('inf'), None
            for q, (cq, _) in best[-1].items():
                leap = abs(p - q)
                cost = cq + c + (0.7 if leap == 0 else 0.3 if leap <= 2 else 1.2 if leap <= 4 else 2.5 + leap * 0.3)
                for x0, x1 in ((b0, b1), (m0, m1)):
                    if x0 is None or x1 is None:
                        continue
                    i0, i1 = abs(q - x0) % 12, abs(p - x1) % 12
                    if i0 == i1 and i1 in (0, 7) and q != p and x0 != x1:
                        cost += 50                 # parallel fifth or octave
                if cost < bestc:
                    bestc, arg = cost, q
            layer[p] = (bestc, arg)
        best.append(layer)
    p = min(best[-1], key=lambda k: best[-1][k][0])
    path = [p]
    for i in range(len(segs) - 1, 0, -1):
        p = best[i][p][1]
        path.append(p)
    path.reverse()

    scale0 = major_scale(key_pc)
    out = []
    for i, ((t, d, sym), p) in enumerate(zip(segs, path)):
        nxt = path[(i + 1) % len(path)]
        if fill and d >= 2 and abs(nxt - p) in (3, 4):
            step = 1 if nxt > p else -1
            _, pcs, _ = chord_pcs(sym)
            _, npcs, _ = chord_pcs(segs[(i + 1) % len(segs)][2])
            chromatic = {c for c in set(pcs) | set(npcs) if c not in scale0}
            scale = (scale0 - {(c + 1) % 12 for c in chromatic} - {(c - 1) % 12 for c in chromatic}) | set(pcs)
            mel_then = sounding(melody, t + d - 1)
            q = p + step
            while q % 12 not in scale or q == nxt:
                q += step
                if abs(q - p) > 3:
                    break
            clash = mel_then is not None and (mel_then - q) % 12 in (1, 11)
            if 0 < abs(q - p) < abs(nxt - p) and not clash:
                out.append(Note(t, d - 1, [p], set()))
                out.append(Note(t + d - 1, 1, [q], set()))
                continue
        out.append(Note(t, d, [p], set()))
    return out


def override(line, text, bar, beats_per_bar):
    """Replace the bars of ``line`` starting at ``bar`` with hand-written ``text``
    (as many bars as ``text`` has). Returns a new, time-ordered list."""
    new = parse_line(text, beats_per_bar=beats_per_bar, start_bar=bar)
    nbars = text.count('|') + 1
    s, e = (bar - 1) * beats_per_bar, (bar - 1 + nbars) * beats_per_bar
    kept = [n for n in line if not (s - 1e-6 <= n.start < e - 1e-6)]
    return sorted(kept + new, key=lambda n: n.start)
