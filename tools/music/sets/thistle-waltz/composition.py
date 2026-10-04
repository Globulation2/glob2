# SPDX-License-Identifier: GPL-3.0-or-later
"""Thistle Waltz: D major (B minor middle), 3/4, 126 bpm, 48 bars (68.8 s). Recorders, ocarina,
kalimba, folk harp and hand drums.

The deliberate contrast to Moss Lanterns: brighter key, a lilting waltz, and woody
folk timbres that suit the game's handmade, organic look. It keeps the same layout of
a small, high lead over a busy plucked bed.

Form and harmony
    A   1-16  An 8 + 8 alto-recorder tune: a rising D-F#-A arpeggio, a half cadence on
              A7 at bar 8, a full close at 16. A borrowed Gm6 (bar 14) and D7/C colour
              the second half.
    B   17-32 Ocarina in B minor: a sequence through F#7 and E7/G#, then a turn to the
              flat side (C, bar 30) and a dominant (A7sus4 -> A7) that pulls back to D.
    A'  33-40 The tune an octave higher, opening with the arpeggio inverted
              (D6-A5-F#5). The climax is B7 -> Em7 A7 -> D at bars 38-40.
    Coda 41-48 Tenor recorder, quiet, through C and Bb (bVII, bVI) to A7/C#, whose
              falling arpeggio leads into bar 1.

The counter-line is drafted by ``counter_line`` (guide tones, no parallels, below the
tune), then hand-corrected in ten bars where it should answer the melody.

Moods (one timeline)
    calm      Recorder or ocarina lead, the clarinet counter-line in places, rolled folk
              harp, kalimba in the A sections, sustained cellos, a viola pad in the
              middle and soft vibraphone rolls in the coda.
    building  A waltz rhythm section: harp and kalimba in quavers, pizzicato
              "pah-pah"s, cello and bass pizzicato "oom"s, staccato bassoon, marimba in
              B, and frame drum, shaker, woodblock and bongos.
    combat    Violins double the tune. Cellos and basses play spiccato quavers over a
              sustained contrabass, violas play a semiquaver ostinato, horn takes the
              counter-line, and timpani, bass drum, darbuka, frame drum and slit drum
              drive it.

Approved by the maintainer by ear.
"""
from glob2music.score import (Accompaniment, Note, Part, Score, counter_line, octave, override, parse_harmony,
                              parse_line)

BPB = 3

HARMONY = """
D | D/F# | G | D/A | Bm | G | Em7 | A7 | D | F#m/C# | Bm | D7/C | G | Gm6/Bb | D/A:2 A7:1 | D |
Bm | Bm/A | G | F#7 | Bm/F# | E7/G# | A | A7 | G | A | F#m | Bm | Em | C | G | A7sus4:2 A7:1 |
D | D/F# | G | Gm6 | D/A | B7 | Em7:2 A7:1 | D |
Bm | G | C | G/B | Bb | C | Em7/D | A7/C#
"""

MELODY = """
d5:q f#5 a5 | g5:q. f#5:e e5:q | d5:q g5 b5 | a5:h f#5:q | b5:q. a5:e f#5:q | g5:q e5 d5 | e5:q g5:e f#5 e5:q | c#5:h r:q |
d5:q f#5 a5 | c#6:q. b5:e a5:q | b5:q d6 b5 | a5:q. c6:e b5 a5 | g5:h b5:q | bb5:q. g5:e e5:q | f#5:h e5:q | d5:h. |
f#5:q b5:e a5 b5:q | d6:q b5:e a5 f#5:q | g5:q. f#5:e e5:q | a#5:h c#6:q | d6:q. c#6:e b5:q | g#5:q b5 d6 | c#6:q. b5:e a5:q | g5:h c#5:q |
d5:q g5 b5 | e5:q a5 c#6 | c#6:q. a5:e f#5:q | d6:h b5:q | g5:q. f#5:e e5:q | e5:q g5 c6 | b5:h g5:q | d6:h c#6:q |
d6:q a5 f#5 | f#5:q. g5:e a5:q | d6:q. b5:e g5:q | bb5:q. a5:e g5:q | f#5:q a5 d6 | d#6:q. c#6:e a5:q | e6:q. d6:e c#6:q | d6:h. |
f#5:h d5:q | d5:h b4:q | e5:h c5:q | d5:h. | f5:q. d5:e bb4:q | e5:q. c5:e g4:q | g4:q b4 e5 | g5:q e5 c#5
"""

BASS = """
d3:h. | f#2 | g2 | a2 | b2 | g2 | e2 | a2 | d3 | c#3 | b2 | c3 | g2 | bb2 | a2 | d3 |
b2 | a2 | g2 | f#2 | f#2 | g#2 | a2 | a2 | g2 | a2 | f#2 | b2 | e2 | c3 | g2 | a2 |
d3 | f#2 | g2 | g2 | a2 | b2 | e3:h a2:q | d3:h. |
b2 | g2 | c3 | b2 | bb2 | c3 | d3 | c#3
"""

_harmony = parse_harmony(HARMONY, BPB)
_melody = parse_line(MELODY, BPB, part='melody')
_bass = parse_line(BASS, BPB, part='bass')

# Drafted below the tune (D4-G5), then answered by hand where the tune holds or rests.
_counter = counter_line(_harmony, _melody, _bass, 62, 79, key_pc=2, seed_pitch=66)
for _bar, _text in ((4, 'f#4:h e4:e g4:e'),            # under the held A5, a turn
                    (8, 'e4:q a4:e g4 e4:q'),          # answers the half cadence
                    (16, 'f#4:q a4:e g4 f#4 e4'),      # walks down into B
                    (15, 'a4:h g4:q'), (17, 'd4:h.'), (23, 'e5:h c#5:q'), (24, 'a4:h g4:q'),
                    (32, 'e4:q g4:e a4 b4 c#5'),       # pickup into A'
                    (40, 'd4:q f#4:e a4 d5:q'),        # answers the climax
                    (48, 'g4:h e4:e c#4:e')):          # leads to bar 1
    _counter = override(_counter, _text, _bar, BPB)

SCORE = Score(
    title='thistle-waltz', bpm=126.0, beats_per_bar=BPB, bars=48,
    harmony=_harmony, melody=_melody, counter=_counter, bass=_bass,
    intensity=[0.40, 0.42, 0.45, 0.44, 0.47, 0.50, 0.52, 0.48, 0.50, 0.54, 0.56, 0.58, 0.60, 0.62, 0.58, 0.55,
               0.58, 0.60, 0.62, 0.66, 0.68, 0.72, 0.74, 0.70, 0.66, 0.70, 0.74, 0.78, 0.76, 0.80, 0.82, 0.84,
               0.82, 0.84, 0.86, 0.88, 0.90, 0.94, 0.96, 0.92,
               0.70, 0.62, 0.58, 0.54, 0.52, 0.50, 0.46, 0.42],
    phrases=[(a, a + 3) for a in range(1, 49, 4)],
    sections={'A': (1, 16), 'B': (17, 32), 'A2': (33, 40), 'Coda': (41, 48)},
    rubato=[(8, 2.0, 0.94), (16, 1.0, 0.95), (16, 2.0, 0.90), (32, 2.0, 0.95), (40, 1.0, 0.94), (40, 2.0, 0.90),
            (44, 2.0, 0.95)],
)

S = SCORE
G = Accompaniment(S)
M, C, B = S.melody, S.counter, S.bass
bars = S.bars_of


def timpani(pattern):
    """Timpani on each bar's first written bass note, tuned between F2 and E3."""
    out = []
    for bar in range(1, S.bars + 1):
        p = 41 + (bars(B, bar, bar)[0].pitches[0] % 12 - 5) % 12
        p = p - 12 if p > 52 else p
        out += [Note((bar - 1) * BPB + x, 0.5, [p], {'>'} if x == 0 else set()) for x in pattern(bar)]
    return out


def arrange(mood):
    P = [
        Part('recorder', 'alto_recorder', bars(M, 1, 16) + bars(M, 33, 40), 'lead'),
        Part('ocarina', 'ocarina', bars(M, 17, 32), 'lead'),
        Part('tenor_rec', 'tenor_recorder', bars(M, 41, 48), 'lead'),
    ]
    if mood == 'calm':
        P += [
            Part('clarinet_ctr', 'clarinet_sus', bars(C, 9, 16) + bars(C, 25, 32) + bars(C, 37, 48), 'counter'),
            Part('harp', 'folk_harp', G.rolled(1, 48, 50, 74, 4), 'harp'),
            Part('kalimba', 'kalimba', G.arpeggio(9, 16, 62, 86, [0, 2, 3, 1, 2, None])
                 + G.arpeggio(33, 40, 62, 86, [0, 2, 3, 1, 2, None]), 'sparkle'),
            Part('cellos', 'cellos_sus_q', B, 'bass'),
            Part('violas_pad', 'violas_sus_q', G.pad(17, 40, 55, 72, 2), 'pad'),
            Part('vibes', 'vibraphone_soft', G.rolled(41, 48, 62, 79, 3, spread=0.12), 'sparkle'),
        ]
    else:
        P += [
            Part('clarinet_ctr', 'clarinet_sus', bars(C, 1, 16) + bars(C, 41, 48), 'counter'),
            Part('recorder_ctr', 'tenor_recorder', bars(C, 17, 32), 'counter'),
            Part('violins_ctr', 'violins_sus', bars(C, 33, 40), 'counter'),
            Part('harp', 'folk_harp', G.arpeggio(1, 48, 50, 79, [0, 1, 2, 3, 2, 1]), 'harp'),
            Part('kalimba', 'kalimba', G.arpeggio(1, 16, 62, 86, [3, 1, 2, 0, 2, 1])
                 + G.arpeggio(17, 32, 62, 86, [0, 2, 1, 3, 2, 1]) + G.arpeggio(33, 48, 62, 86, [3, 1, 2, 0, 2, 1]), 'sparkle'),
            Part('violas_pad', 'violas_sus', G.pad(1, 48, 55, 72, 2), 'pad'),
            Part('violins_pizz', 'violins_pizz', G.chords_at(1, 48, 62, 79, (1, 2), dur=0.6), 'pizz'),
            Part('bassoon', 'bassoon_stac', G.bass_steps(17, 40, [0, None, None], 1.0), 'bass'),
        ]

    if mood == 'building':
        def frame(bar):
            return [0, 1, 1.5, 2, 2.5] if bar in (16, 32, 40, 48) else ([0, 2] if bar % 2 else [0, 1.5, 2])
        P += [
            Part('cellos_pizz', 'cellos_pizz', G.bass_steps(1, 48, [0, None, 7], 1.0), 'bass'),
            Part('basses_pizz', 'basses_pizz', octave(G.bass_steps(1, 48, [0, None, None], 1.0), -1), 'bass'),
            Part('marimba', 'marimba', G.arpeggio(17, 32, 55, 79, [0, None, 2, 1, 3, None]), 'harp'),
            # frame drum: low hit on the downbeat, small-head hand strokes elsewhere
            Part('frame_drum', 'frame_drum', G.hits(1, 48, frame, lambda b, x: 61 if x == 0 else 63), 'perc'),
            Part('shaker', 'shaker', G.hits(9, 48, [0.5, 1.5, 2.5], lambda b, x: 62 if x == 0.5 else 63), 'perc_soft'),
            Part('woodblock', 'woodblock', G.hits(17, 32, lambda b: [2.5] if b % 2 == 0 else [], 60), 'perc_soft'),
            Part('bongos', 'bongos', G.hits(33, 40, [1, 1.5, 2], lambda b, x: 60 if x != 2 else 63), 'perc'),
        ]

    if mood == 'combat':
        def darbuka(bar):
            if bar in (8, 24, 44):
                return [0, 0.5, 1, 1.5, 2, 2.25, 2.5, 2.75]
            return [0, 1, 1.5, 2, 2.5] if bar % 2 else [0, 0.5, 1, 2, 2.5]
        P += [
            Part('melody_dbl', 'violins_sus', octave(bars(M, 1, 16) + bars(M, 33, 40), -1) + bars(M, 17, 32), 'lead_dbl'),
            Part('cellos_spic', 'cellos_spic', G.bass_steps(1, 48, [0, 0, 12, 0, 7, 0], 0.5), 'bass'),
            Part('basses_spic', 'basses_spic', octave(G.bass_steps(1, 48, [0, None, 0, None, 0, None], 0.5), -1), 'bass'),
            Part('basses_sus', 'basses_sus', octave(B, -1), 'bass_sus'),
            Part('violas_spic', 'violas_spic', G.arpeggio(1, 48, 52, 71, [0, 2, 1, 2] * 3, sub=0.25, n=3), 'ostinato'),
            Part('horn', 'horn_sus', bars(C, 17, 32) + octave(bars(C, 33, 40), -1), 'counter'),
            Part('timpani', 'timpani', timpani(lambda bar: [0, 1, 1.5, 2, 2.5] if bar in (16, 32, 40)
                                               else ([0] if bar % 2 else [0, 2])), 'low_drum'),
            Part('bassdrum', 'bass_drum', G.hits(1, 48, lambda b: [0] if b % 4 else [0, 2.5], 60), 'low_drum'),
            # darbuka: doum on the downbeat, tek on beats, ka on off-beats
            Part('darbuka', 'darbuka', G.hits(1, 48, darbuka, lambda b, x: 60 if x == 0 else (62 if x % 1 == 0 else 64)), 'perc'),
            Part('frame_drum', 'frame_drum', G.hits(1, 48, [1, 2], 64), 'perc'),
            Part('slit_drum', 'slit_drum', G.hits(1, 48, lambda b: [2.5] if b % 4 == 3 else [], 61), 'perc'),
            Part('shaker', 'shaker', G.hits(1, 48, [0.5, 1.5, 2.5], 63), 'perc_soft'),
        ]
    return P
