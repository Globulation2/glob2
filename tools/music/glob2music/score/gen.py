# SPDX-License-Identifier: GPL-3.0-or-later
"""Accompaniment generators: figures derived from a score's harmony and bass line.

Pipeline role: a composition writes its melody, counter-line and bass by hand, and
asks an ``Accompaniment`` for everything that elaborates the harmony: rolled and
broken chords, sustained pads, pizzicato "pah"s, walking and galloping basses,
spiccato ostinati, percussion patterns and timpani. Because every figure is computed
from the one ``Score``, all three moods stay on the same harmony and grid, and a
change to the chord chart re-voices every mood at once.

Voicing: chord figures are voice-led (``notation.voice_lead``) from one chord to the
next inside a register window, so inner voices move by step and repeat common tones.
Each call keeps its own voice-leading thread, so call once per continuous passage.

All positions are in beats; ``first_bar``/``last_bar`` are inclusive, 1-based.
"""
from .notation import Note, chord_at, chord_pcs, voice_lead


class Accompaniment:
    """Generators bound to one ``Score`` (its harmony, bass line and metre)."""

    def __init__(self, score):
        self.score = score
        self.harmony = score.harmony
        self.bass = score.bass
        self.bpb = score.beats_per_bar

    # ------------------------------------------------------------------ helpers
    def segments(self, first_bar, last_bar):
        """Harmony segments ``(start, beats, symbol)`` starting in the bar range."""
        s, e = self.score.span(first_bar, last_bar)
        return [(t, d, sym) for t, d, sym in self.harmony if s - 1e-6 <= t < e - 1e-6]

    def bass_notes(self, first_bar, last_bar):
        return self.score.bars_of(self.bass, first_bar, last_bar)

    # ------------------------------------------------------------------ chords
    def rolled(self, first_bar, last_bar, lo, hi, n=4, spread=0.07, every=None, hold='chord'):
        """Harp-style rolled chords, lowest note first, ``spread`` beats apart.

        One roll per chord change, or every ``every`` beats within a chord (a chord
        shorter than ``every/2`` then gets none). ``hold='chord'`` lets each note last
        the whole chord; ``'stagger'`` shortens later notes by their delay (minimum
        half a beat), so the top of the roll does not outlast the chord.
        """
        out, prev = [], None
        for t, d, sym in self.segments(first_bar, last_bar):
            _, pcs, _ = chord_pcs(sym)
            v = voice_lead(prev, pcs, lo, hi, n)
            prev = v
            starts = [t] if not every else [t + k * every for k in range(int(round(d / every)))]
            for ts in starts:
                for i, p in enumerate(v):
                    dur = d if hold == 'chord' else max(0.5, d - i * spread)
                    out.append(Note(ts + i * spread, dur, [p], {'roll'}))
        return out

    def arpeggio(self, first_bar, last_bar, lo, hi, pattern, sub=0.5, n=4, ring=1.5, accent_every=None):
        """Broken chords: every ``sub`` beats play ``voicing[pattern[k]]``.

        Pattern entries index the ascending ``n``-note voicing; indices past the top
        continue an octave higher (``n`` -> lowest note + 12); ``None`` is a rest. Notes
        last ``ring * sub`` beats (overlapping, like a harp or kalimba). With
        ``accent_every`` every k-th step is accented (spiccato ostinati).
        """
        out, prev = [], None
        for t, d, sym in self.segments(first_bar, last_bar):
            _, pcs, _ = chord_pcs(sym)
            v = sorted(voice_lead(prev, pcs, lo, hi, n))
            prev = v
            k, x = 0, t
            while x < t + d - 1e-6:
                idx = pattern[k % len(pattern)]
                if idx is not None:
                    artic = {'>'} if accent_every and k % accent_every == 0 else set()
                    out.append(Note(x, sub * ring, [v[idx % len(v)] + 12 * (idx // len(v))], artic))
                x += sub
                k += 1
        return out

    def pad(self, first_bar, last_bar, lo, hi, n=2, drop_fifth=False):
        """Sustained voice-led chords; an unchanged voicing is held, not re-struck.

        With ``drop_fifth`` a chord with more tones than voices loses its fifth first
        (keeping third and seventh), otherwise ``voice_lead`` chooses which tones to keep.
        """
        out, prev = [], None
        for t, d, sym in self.segments(first_bar, last_bar):
            _, pcs, _ = chord_pcs(sym)
            if drop_fifth and len(pcs) > n:
                fifth = (pcs[0] + 7) % 12
                pcs = [p for p in pcs if p != fifth] if fifth in pcs and len(pcs) - 1 >= n else pcs[:n]
            v = voice_lead(prev, pcs, lo, hi, n)
            if prev == v and out:
                for x in out[-len(v):]:
                    x.dur += d
            else:
                out += [Note(t, d, [p], set()) for p in v]
            prev = v
        return out

    def chords_at(self, first_bar, last_bar, lo, hi, offsets, n=2, dur=0.4, skip_root=True):
        """Short staccato chords at fixed offsets (beats) within each bar: pizzicato
        off-beats, waltz "pah-pah"s, brass stabs. ``skip_root`` leaves the root to the
        bass when the chord has more than two tones."""
        out, prev = [], None
        for t, d, sym in self.segments(first_bar, last_bar):
            _, pcs, _ = chord_pcs(sym)
            v = voice_lead(prev, pcs[1:] if skip_root and len(pcs) > 2 else pcs, lo, hi, n)
            prev = v
            bar0 = (t // self.bpb) * self.bpb
            for o in offsets:
                x = bar0 + o
                if t - 1e-6 <= x < t + d - 1e-6:
                    out.append(Note(x, dur, list(v), {'!'}))
        return out

    # ------------------------------------------------------------------ bass
    def bass_pattern(self, first_bar, last_bar, pattern):
        """Restate each written bass note with a figure.

        ``pattern`` is a list of ``(offset_beats, interval, beats, accented)`` applied from
        the start of every bass note (entries past the note's end are dropped), e.g. a
        gallop ``[(0, 0, 1, True), (1, 0, .5, False), (1.5, 7, 1, True), (2.5, 12, .5, False)]``.
        """
        out = []
        for n in self.bass_notes(first_bar, last_bar):
            for off, iv, dur, acc in pattern:
                if off < n.dur - 1e-6:
                    out.append(Note(n.start + off, dur, [n.pitches[0] + iv], {'>'} if acc else set()))
        return out

    def bass_steps(self, first_bar, last_bar, shape, sub, accent_every=None):
        """Even subdivisions of each written bass note: step ``k`` plays the bass note
        plus ``shape[k % len(shape)]`` semitones (``None`` = rest). The first step of
        each note is accented, or every ``accent_every``-th step if given."""
        out = []
        for n in self.bass_notes(first_bar, last_bar):
            k, x = 0, n.start
            while x < n.start + n.dur - 1e-6:
                off = shape[k % len(shape)]
                if off is not None:
                    acc = (k % accent_every == 0) if accent_every else k == 0
                    out.append(Note(x, sub, [n.pitches[0] + off], {'>'} if acc else set()))
                x += sub
                k += 1
        return out

    def walking(self, first_bar, last_bar, scale_pcs):
        """Crotchet walking bass from the written half-note line.

        Each written note is struck, then (for notes of two beats or more) an approach
        note leads to the next written note: a chord tone near a fourth above when the
        next note is a step away, otherwise a chromatic or scale approach from below or
        above. ``scale_pcs`` are the pitch classes allowed for approach notes (beyond
        the chord). The line wraps: the last note approaches the first.
        """
        line = self.bass
        out = []
        for j, n in enumerate(line):
            if n not in self.bass_notes(first_bar, last_bar):
                continue
            nxt = line[(j + 1) % len(line)].pitches[0]
            cur = n.pitches[0]
            sym = chord_at(self.harmony, n.start)
            out.append(Note(n.start, 1.0, [cur], set()))
            if n.dur >= 2:
                _, pcs, _ = chord_pcs(sym)
                if abs(nxt - cur) <= 2:
                    ap = min((p for p in range(cur - 9, cur + 10) if p % 12 in pcs and p != cur),
                             key=lambda p: abs(p - (cur + 5)))
                else:
                    ap = nxt - 1 if nxt > cur else nxt + 2
                    if ap % 12 not in set(pcs) | set(scale_pcs):
                        ap = nxt - 2
                out.append(Note(n.start + 1, 1.0, [ap], set()))
            elif n.dur > 1:
                out[-1].dur = n.dur
        return out

    def run_up(self, to_bar, steps=6, sub=0.5):
        """A chromatic run of ``steps`` notes ending just before ``to_bar``'s bass note
        (wrapping to bar 1 after the last bar), accenting the arrival."""
        target_bar = to_bar if to_bar <= self.score.bars else 1
        tgt = self.bass_notes(target_bar, target_bar)[0].pitches[0]
        s = (to_bar - 1) * self.bpb - steps * sub
        return [Note(s + i * sub, sub, [tgt - steps + i], {'>'} if i == steps - 1 else set()) for i in range(steps)]

    # ------------------------------------------------------------------ percussion
    def hits(self, first_bar, last_bar, pattern, key, artic=None):
        """Percussion: per bar, hits at ``pattern`` offsets (a list, or ``f(bar)``),
        on MIDI key ``key`` (an int, or ``f(bar, offset)``)."""
        out = []
        for bar in range(first_bar, last_bar + 1):
            offsets = pattern(bar) if callable(pattern) else pattern
            for x in offsets:
                k = key(bar, x) if callable(key) else key
                out.append(Note((bar - 1) * self.bpb + x, 0.25, [k], set(artic or ())))
        return out

    def timpani(self, first_bar, last_bar, pattern, lo=36, hi=48):
        """Timpani hits at ``pattern`` offsets, tuned to the bass of the chord sounding
        at each hit and placed in ``[lo, hi]``; downbeats are accented."""
        out = []
        for bar in range(first_bar, last_bar + 1):
            for x in (pattern(bar) if callable(pattern) else pattern):
                tb = (bar - 1) * self.bpb + x
                _, _, bpc = chord_pcs(chord_at(self.harmony, tb))
                p = lo + (bpc - lo) % 12
                if p > hi:
                    p -= 12
                out.append(Note(tb, 0.5, [p], {'>'} if x == 0 else set()))
        return out
