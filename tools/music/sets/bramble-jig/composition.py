# SPDX-License-Identifier: GPL-3.0-or-later
"""Bramble Jig: A major / A Mixolydian, 6/8 at dotted crotchet = 92, 56 bars (73.1 s). Flute,
clarinet, oboe and glockenspiel over harp and strings.

A playful jig: lilting crotchet-quaver figures in compound time, written here as three
crotchet beats per bar at 138 bpm (``q.`` is one dotted-crotchet pulse). Accents fall
on both pulses of the bar (``accent``).

Form and harmony
    A   1-16  The jig tune on flute, 8 + 8. The Mixolydian G of bar 10 (G/B) is its
              wink; it closes on A at bar 16.
    B   17-32 Clarinet in F# minor over a bass that falls by step (bars 17-23). C#7 at
              bar 28 and E7sus4 -> E7 at 32.
    C   33-40 A sudden bright turn to C major (bIII): a sequenced, bouncing figure in
              flute and glockenspiel, back through D7/F# and E7.
    A'  41-52 The tune climbs higher; a borrowed Dm6/F at bar 44 is the emotional peak.
    Link 53-56 Oboe, settling over F#m - D/F# - Bm7 onto E7, which falls into bar 1.

No bar of melody or counter-line repeats note for note. The counter-line is drafted
below the tune by ``counter_line`` and answered by hand in six bars.

Moods (one timeline)
    calm      The leads, the clarinet and viola counter-lines, a jig-rhythm harp
              (crotchet-quaver), a quiet violin pad, sustained cellos and basses.
    building  Adds a pizzicato "oom-pah" (cellos and basses on the two pulses, violins
              and violas on the off-quavers), staccato bassoon, marimba in B and C, and
              bongos, frame drum, shaker and woodblock.
    combat    A real re-arrangement in the same key and tune. Cellos, basses and tuba
              play a galloping low riff (it changes in C) with chromatic run-ups into
              each section, over a sustained contrabass and low trombone. Violins add a
              spiccato quaver ostinato, horns double the tune an octave down, and horn
              stabs mark the downbeats. Timpani, bass drum, toms, rope snare and shaker
              form the drum line.

Approved by the maintainer by ear.
"""
from glob2music.score import (Accompaniment, Part, Score, counter_line, fold_into_range, octave, override,
                              parse_harmony, parse_line)

BPB = 3

HARMONY = """
A | A/C# | D/F# | A | F#m/A | D | E/B | E7/D |
A/C# | G/B | D | A/C# | Bm7 | E7 | A:1.5 E7:1.5 | A |
F#m | C#m/E | D | A/C# | Bm | F#m/A | G | E7/G# |
F#m/A | D | Bm | C#7 | F#m | G | Bm7 | E7sus4:1.5 E7:1.5 |
C | F | C/E | G | Am | F | D7/F# | E7 |
A | A/G | D/F# | Dm6/F | A/E | F#m | Bm7 | E7 |
A | G | D | A:1.5 E:1.5 | F#m | D/F# | Bm7 | E7sus4:1.5 E7:1.5
"""

MELODY = """
e5:q a5:e c#6:q a5:e | e5:e f#5 e5 c#5:q. | d5:q f#5:e a5:q f#5:e | e5:q. c#5:q a4:e |
f#5:q a5:e c#6:e b5 a5 | b5:q a5:e f#5:q d5:e | e5:e f#5 g#5 b5:q g#5:e | e5:q. b4:q. |
c#5:q e5:e a5:q c#6:e | d6:q b5:e g5:q b5:e | a5:q f#5:e d5:e e5 f#5 | e5:q c#5:e a4:q e5:e |
b4:q d5:e f#5:q a5:e | g#5:q. e5:e d5 b4 | c#5:q a4:e b4:q d5:e | c#5:q. r:q e5:e |
f#5:q a5:e c#6:q a5:e | g#5:q. e5:q c#5:e | d5:e f#5 a5 d6:q c#6:e | c#6:q a5:e e5:q. |
d5:q f#5:e b5:q a5:e | a5:q f#5:e c#5:q. | b4:q d5:e g5:q b5:e | g#5:q e5:e d5:q. |
c#5:q f#5:e a5:e g#5 f#5 | f#5:q. a5:q d6:e | d6:q c#6:e b5:q f#5:e | b5:q g#5:e e#5:q c#5:e |
a5:q. f#5:e e5 a5 | d5:q g5:e b5:q g5:e | a5:q. f#5:q d5:e | a5:q. g#5:q. |
g5:e e5 g5 c6:q g5:e | a5:e f5 a5 c6:q a5:e | g5:q e5:e c5:q e5:e | d5:e g5 b5 a5:q b5:e |
e6:q c6:e a5:q. | c6:q a5:e f5:q a5:e | f#5:e a5 c6 d6:q. | d6:q. b5:q g#5:e |
e5:q a5:e c#6:q e6:e | c#6:q b5:e a5:q e5:e | a5:q d6:e f#6:q d6:e | f6:q. d6:q b5:e |
e6:q. c#6:q a5:e | c#6:q a5:e f#5:q a5:e | d6:e c#6 b5 a5:q f#5:e | g#5:q b5:e d6:q. |
c#6:q. a5:q. | d5:q g5:e b5:e a5 g5 | f#5:q a5:e d6:q a5:e | c#6:q. b5:q. |
a5:q c#6:e f#5:q. | d5:q f#5:e a5:q. | d5:q f#5:e b4:q d5:e | a5:q. g#5:q d5:e
"""

BASS = """
a2:h. | c#3 | f#2 | a2 | a2 | d3 | b2 | d3 |
c#3 | b2 | d3 | c#3 | b2 | e3 | a2:q. e3:q. | a2:h. |
f#3 | e3 | d3 | c#3 | b2 | a2 | g2 | g#2 |
a2 | d3 | b2 | c#3 | f#2 | g2 | b2 | e2 |
c3 | f2 | e2 | g2 | a2 | f2 | f#2 | e2 |
a2 | g2 | f#2 | f2 | e2 | f#2 | b2 | e2 |
a2 | g2 | d3 | a2:q. e2:q. | f#2:h. | f#2 | b2 | e2
"""


def accent(beat):
    """6/8 accents: the bar's first pulse +7, its second pulse (beat 1.5) +4, other
    quavers -2, anything finer -6."""
    f = beat % 3
    if abs(f) < 1e-6:
        return 7
    if abs(f - 1.5) < 1e-6:
        return 4
    if abs(f * 2 - round(f * 2)) < 1e-6:
        return -2
    return -6


_harmony = parse_harmony(HARMONY, BPB)
_melody = parse_line(MELODY, BPB, part='melody')
_bass = parse_line(BASS, BPB, part='bass')
_counter = counter_line(_harmony, _melody, _bass, 57, 76, key_pc=9, seed_pitch=64)
for _bar, _text in ((12, 'a4:q. c#5:q.'), (16, 'e4:e f#4 g#4 a4:q.'), (21, 'f#4:h.'),
                    (32, 'g#4:q. b4:q d5:e'), (40, 'g#4:q a4:e b4:q d5:e'), (48, 'e4:e f#4 g#4 b4:q.')):
    _counter = override(_counter, _text, _bar, BPB)

SCORE = Score(
    title='bramble-jig', bpm=138.0, beats_per_bar=BPB, bars=56, time_signature=(6, 8), accent=accent,
    harmony=_harmony, melody=_melody, counter=_counter, bass=_bass,
    # kept high at the start so calm is audible from bar 1; peaks at bars 43-45
    intensity=[0.55, 0.56, 0.58, 0.56, 0.58, 0.60, 0.62, 0.58, 0.60, 0.63, 0.64, 0.62, 0.63, 0.66, 0.64, 0.60,
               0.62, 0.64, 0.66, 0.66, 0.68, 0.70, 0.72, 0.70, 0.70, 0.72, 0.74, 0.76, 0.74, 0.76, 0.78, 0.80,
               0.74, 0.76, 0.78, 0.80, 0.80, 0.82, 0.84, 0.86,
               0.84, 0.86, 0.90, 0.95, 0.94, 0.90, 0.88, 0.86, 0.82, 0.78, 0.74, 0.70,
               0.64, 0.60, 0.58, 0.56],
    phrases=[(a, a + 3) for a in range(1, 57, 4)],
    sections={'A': (1, 16), 'B': (17, 32), 'C': (33, 40), 'A2': (41, 52), 'Link': (53, 56)},
    rubato=[(16, 1.5, 0.95), (32, 1.5, 0.96), (40, 1.5, 0.94), (44, 1.5, 0.96), (52, 1.5, 0.96)],
)

# Combat leans on the low register: sustained basses and low drums up, galloping basses a little.
MIX_ADJUST = {'combat': {'bass_sus': 4.0, 'low_drum': 3.0, 'bass': 1.0}}

S = SCORE
G = Accompaniment(S)
M, C, B = S.melody, S.counter, S.bass
bars = S.bars_of

#: The combat riff, per written bass note: crotchet root, quaver root, crotchet fifth,
#: quaver octave; in section C a dotted-crotchet root, then root-fifth-octave quavers.
GALLOP = [(0, 0, 1.0, True), (1.0, 0, 0.5, False), (1.5, 7, 1.0, True), (2.5, 12, 0.5, False)]
GALLOP_C = [(0, 0, 1.5, True), (1.5, 0, 0.5, True), (2.0, 7, 0.5, False), (2.5, 12, 0.5, False)]


def arrange(mood):
    glock = bars(M, 33, 40) + ([n for n in bars(M, 41, 48) if n.dur <= 0.5] if mood != 'calm' else [])
    P = [
        Part('flute', 'flute_sus', bars(M, 1, 16) + bars(M, 33, 52), 'lead'),
        Part('clarinet', 'clarinet_sus', bars(M, 17, 32), 'lead'),
        Part('oboe', 'oboe_sus', bars(M, 53, 56), 'lead'),
        Part('glock', 'glockenspiel_vcsl', glock, 'sparkle'),
        Part('clarinet_ctr', 'clarinet_sus', bars(C, 1, 16) + bars(C, 41, 56), 'counter'),
        Part('violas_ctr', 'violas_sus' if mood != 'calm' else 'violas_sus_q', bars(C, 17, 40), 'counter'),
    ]
    if mood == 'calm':
        P += [
            Part('harp', 'harp', G.arpeggio(1, 56, 52, 76, [0, None, 2, 1, None, 3]), 'harp'),   # crotchet-quaver
            Part('violins_pad', 'violins_sus_q', G.pad(1, 56, 62, 76, 2), 'pad'),
            Part('cellos', 'cellos_sus_q', B, 'bass'),
            Part('basses', 'basses_sus_q', octave(bars(B, 17, 32) + bars(B, 41, 52), -1), 'bass'),
        ]
    else:
        P += [
            Part('harp', 'harp', G.arpeggio(1, 56, 52, 79, [0, 2, 1, 3, 2, 1]), 'harp'),
            Part('violins_pad', 'violins_sus_q', G.pad(1, 56, 62, 76, 2), 'pad'),
            Part('violins_pizz', 'violins_pizz', G.chords_at(1, 56, 64, 79, (1.0, 2.5)), 'pizz'),
            Part('violas_pizz', 'violas_pizz', G.chords_at(1, 56, 55, 69, (1.0, 2.5)), 'pizz'),
        ]
    if mood == 'building':
        P += [
            Part('cellos_pizz', 'cellos_pizz', G.bass_pattern(1, 56, [(0, 0, 1, True), (1.5, 7, 1, False)]), 'bass'),
            Part('basses_pizz', 'basses_pizz', G.bass_pattern(1, 56, [(0, -12, 1, True), (1.5, -12, 1, False)]), 'bass'),
            Part('bassoon', 'bassoon_stac', G.bass_pattern(17, 52, [(0, 0, 0.5, True), (1.5, 0, 0.5, False),
                                                                     (2.5, 7, 0.5, False)]), 'bass'),
            Part('marimba', 'marimba', G.arpeggio(17, 40, 57, 81, [3, 2, 1, 0, 1, 2]), 'harp'),
            Part('bongos', 'bongos', G.hits(1, 56, lambda b: [0, 1.0, 1.5, 2.5] if b % 4 else [0, 1.0, 1.5, 2.0, 2.5],
                                            lambda b, x: 63 if x in (0, 1.5) else 60), 'perc'),
            Part('frame_drum', 'frame_drum', G.hits(9, 56, [0, 1.5], 61), 'perc'),
            Part('shaker', 'shaker', G.hits(17, 56, [0.5, 1.0, 2.0, 2.5], lambda b, x: 62 if x in (1.0, 2.5) else 63),
                 'perc_soft'),
            Part('woodblock', 'woodblock', G.hits(33, 40, [1.0, 2.5], 60), 'perc_soft'),
        ]
    if mood == 'combat':
        runs = sum((G.run_up(b) for b in (9, 17, 33, 41, 53, 57)), [])
        run_bars = {S.bar_of(n.start) for n in runs}
        gallop = [n for n in G.bass_pattern(1, 32, GALLOP) + G.bass_pattern(33, 40, GALLOP_C) + G.bass_pattern(41, 56, GALLOP)
                  if S.bar_of(n.start) not in run_bars]
        tuba = fold_into_range(octave(G.bass_pattern(1, 56, [(0, 0, 0.5, True), (1.5, 0, 0.5, True)]), -1), 29, 127)
        horn_tune = fold_into_range(octave(bars(M, 1, 16) + bars(M, 41, 52), -1), 0, 77)
        P += [
            Part('cellos_spic', 'cellos_spic', gallop + runs, 'bass'),
            Part('basses_spic', 'basses_spic', octave(gallop + runs, -1), 'bass'),
            Part('tuba', 'tuba_stac', tuba, 'bass'),
            Part('trombone_low', 'trombone_sus', B, 'bass_sus'),
            Part('basses_sus', 'basses_sus', octave(B, -1), 'bass_sus'),
            Part('violins_spic', 'violins_spic', G.arpeggio(1, 56, 62, 81, [2, 1, 0, 3, 1, 2], ring=1.0), 'ostinato'),
            Part('horn_dbl', 'horn_sus', horn_tune, 'lead_dbl'),
            Part('horn', 'horn_sus', bars(C, 17, 40), 'counter'),
            Part('horn_stac', 'horn_stac', G.chords_at(1, 56, 55, 70, (0.0,), dur=0.5, skip_root=False), 'pizz'),
            Part('timpani', 'timpani', G.timpani(1, 56, lambda b: [0, 1.5, 2.5] if b % 2 == 0 else [0, 1.5]), 'low_drum'),
            Part('bassdrum', 'bass_drum_2', G.hits(1, 56, [0, 1.5], 62), 'low_drum'),
            Part('tom', 'tom', G.hits(1, 56, lambda b: [1.0, 2.5] if b % 4 else [0.5, 1.0, 2.0, 2.5],
                                      lambda b, x: 62 if x < 2 else 64), 'perc'),
            Part('snare', 'snare_rope', G.hits(1, 56, lambda b: [1.5] if b % 8 else [1.0, 1.5, 2.0, 2.25, 2.5, 2.75], 62),
                 'perc_snare'),
            Part('shaker', 'shaker', G.hits(1, 56, [0.5, 1.0, 2.0, 2.5], 63), 'perc_soft'),
        ]
    return P
