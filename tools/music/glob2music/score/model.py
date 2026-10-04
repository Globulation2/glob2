# SPDX-License-Identifier: GPL-3.0-or-later
"""The score model: one composed piece, its parts per mood, and its tempo map.

Pipeline role: a set's ``composition.py`` builds one ``Score`` (form, harmony and the
hand-written voices) and an ``arrange(mood) -> [Part]`` function that orchestrates
that *same* timeline three ways. ``perform.perform_part`` turns each ``Part`` into
timed note events, a backend renders them (``backends/sfizz.py`` for sample
libraries; a synthesiser backend can consume the same ``PerformedPart``), and
``mix.py`` folds and mixes the stems into the raw ``Trio`` that ``master.finish``
masters.

Because every mood is derived from one ``Score`` and one ``TempoMap``, the three files
share the beat grid to the sample: the game's mood crossfade (same playback position,
0.37 s) can never flam. Rubato is allowed inside the piece but must leave the last bar
in strict time so the loop seam is steady.

``Instrument`` describes a playable patch independently of how it is rendered: its
articulation family (which decides note lengths and expression), the playable key
range (checked statically) and its default seat in the mix.
"""
from dataclasses import dataclass, field
import math

from .notation import Note


#: Articulation families. They decide how ``perform`` sets note lengths and whether a
#: part gets a CC11 expression curve.
#:   sustain - bowed/blown notes: legato overlap, messa di voce, CC11 phrasing
#:   short   - staccato, spiccato and pizzicato patches: short fixed lengths
#:   ring    - harp, mallets, kalimba: the note rings; note-off only starts a release
#:   perc    - unpitched one-shots
KINDS = ('sustain', 'short', 'ring', 'perc')


@dataclass(frozen=True)
class Instrument:
    """A playable patch as the score layer sees it.

    ``lag`` (seconds, usually negative) moves note-ons earlier to compensate slow
    sample attacks; ``low``/``high`` is the mapped key range (notes outside it are
    silent, so ``checks.ranges`` treats them as errors); ``pan`` (-1..1), ``send``
    (reverb send, linear) and ``gain_db`` are the patch's default mix seat. A part may
    override the pan (e.g. one GM-style percussion kit used for several instruments).
    ``highpass_hz`` fixes the stem's high-pass corner instead of the role default
    (``mix.stem_highpass_hz``).
    """

    name: str
    kind: str
    lag: float = 0.0
    pan: float = 0.0
    send: float = 0.3
    gain_db: float = 0.0
    low: int = 0
    high: int = 127
    highpass_hz: float = None

    def __post_init__(self):
        if self.kind not in KINDS:
            raise ValueError(f'{self.name}: kind must be one of {KINDS}')


@dataclass
class Part:
    """One player in one mood: an instrument key, its notes and its role.

    ``role`` selects the velocity range, timing looseness (``perform.ROLES``) and the
    level relative to the lead (``mix.ROLE_DB``). ``name`` identifies the player across
    moods: the same name gets the same humanisation seed and the same level reference
    in every mood, so a crossfade changes the arrangement, not the performance.
    """

    name: str
    instrument: str
    notes: list
    role: str
    pan: float = None


class TempoMap:
    """Beat <-> seconds with per-beat rubato.

    ``rubato`` is a list of ``(bar, beat_in_bar, factor)``: the beat starting at that
    position plays at ``factor`` times the tempo (0.9 = 10 % slower). Factors on the
    same beat multiply. Tempo is constant within a beat.
    """

    def __init__(self, bpm, total_beats, rubato=(), beats_per_bar=4):
        self.spb = []
        for b in range(int(total_beats)):
            f = 1.0
            for bar, beat, fac in rubato:
                start = (bar - 1) * beats_per_bar + beat
                if start <= b < start + 1:
                    f *= fac
            self.spb.append(60.0 / bpm / f)
        self.cum = [0.0]
        for s in self.spb:
            self.cum.append(self.cum[-1] + s)
        self.total = self.cum[-1]

    def sec(self, beat):
        """Seconds at ``beat`` (extrapolated at the edge tempos outside the piece)."""
        if beat <= 0:
            return beat * self.spb[0]
        if beat >= len(self.spb):
            return self.total + (beat - len(self.spb)) * self.spb[-1]
        i = int(beat)
        return self.cum[i] + (beat - i) * self.spb[i]

    def beat(self, sec):
        """Inverse of ``sec`` (binary search over the cumulative beat times)."""
        if sec >= self.total:
            return len(self.spb) + (sec - self.total) / self.spb[-1]
        lo, hi = 0, len(self.cum) - 1
        while lo < hi - 1:
            mid = (lo + hi) // 2
            if self.cum[mid] <= sec:
                lo = mid
            else:
                hi = mid
        return lo + (sec - self.cum[lo]) / self.spb[lo]


@dataclass
class Score:
    """A composed piece: form, harmony, the hand-written voices and the dynamic plan.

    Attributes:
        title: the set id; also the namespace of humanisation seeds.
        bpm, beats_per_bar, bars: crotchet tempo, crotchet beats per bar, bar count.
        harmony: ``notation.parse_harmony`` output.
        melody, counter, bass: hand-written voices (``Note`` lists). The bass is the
            written bass line that generators elaborate (walks, gallops, pedals).
        intensity: one value per bar in 0..1, the piece's dynamic arc. It drives
            velocities and CC11 in every mood; it should end near where it starts so
            the loop does not jump in level.
        phrases: ``[(first_bar, last_bar), ...]``; each gets a rise-and-relax arch.
        sections: ``{label: (first_bar, last_bar)}``, for reports and arrangements.
        rubato: ``TempoMap`` rubato list (never in the last bar).
        time_signature: notated metre for MIDI export, e.g. ``(6, 8)``.
        accent: optional ``f(beat) -> velocity offset`` replacing the default metric
            accents (``perform.metric_accent``); compound metres need one.
        counter_above: bar ranges where the counter-line is *meant* to sit above the
            melody (a descant); ``True`` for the whole piece. Only affects checks.
    """

    title: str
    bpm: float
    beats_per_bar: int
    bars: int
    harmony: list
    melody: list
    counter: list
    bass: list
    intensity: list
    phrases: list
    sections: dict = field(default_factory=dict)
    rubato: list = field(default_factory=list)
    time_signature: tuple = None
    accent: object = None
    counter_above: object = ()

    def __post_init__(self):
        if self.time_signature is None:
            self.time_signature = (self.beats_per_bar, 4)
        if len(self.intensity) != self.bars:
            raise ValueError(f'{self.title}: intensity has {len(self.intensity)} values for {self.bars} bars')
        if any(bar == self.bars for bar, _, _ in self.rubato):
            raise ValueError(f'{self.title}: no rubato in the last bar, the loop seam must stay in time')
        self.tempo = TempoMap(self.bpm, self.total_beats, self.rubato, self.beats_per_bar)

    @property
    def total_beats(self):
        return self.bars * self.beats_per_bar

    @property
    def seconds(self):
        return self.tempo.total

    def loop_frames(self, sample_rate):
        """Loop length in frames: identical for all moods by construction."""
        return int(round(self.tempo.total * sample_rate))

    def span(self, first_bar, last_bar):
        """``(start_beat, end_beat)`` of bars ``first_bar..last_bar`` inclusive."""
        return (first_bar - 1) * self.beats_per_bar, last_bar * self.beats_per_bar

    def bars_of(self, notes, first_bar, last_bar):
        """The notes starting within bars ``first_bar..last_bar`` (inclusive)."""
        s, e = self.span(first_bar, last_bar)
        return [n for n in notes if s - 1e-6 <= n.start < e - 1e-6]

    def bar_of(self, beat):
        """1-based bar number containing ``beat``."""
        return int(math.floor(beat / self.beats_per_bar + 1e-9)) + 1

    def intensity_at(self, beat):
        return self.intensity[min(int(beat // self.beats_per_bar), len(self.intensity) - 1)]

    def counter_is_above(self, beat):
        if self.counter_above is True:
            return True
        return any(a <= self.bar_of(beat) <= b for a, b in self.counter_above)


def octave(notes, k):
    """Copies of ``notes`` moved by ``k`` octaves."""
    return [n.moved(12 * k) for n in notes]


def fold_into_range(notes, low, high):
    """Copies with every pitch moved by octaves into ``[low, high]`` (for doublings
    that would otherwise leave an instrument's range)."""
    out = []
    for n in notes:
        ps = []
        for p in n.pitches:
            while p < low:
                p += 12
            while p > high:
                p -= 12
            ps.append(p)
        out.append(Note(n.start, n.dur, ps, set(n.artic)))
    return out
