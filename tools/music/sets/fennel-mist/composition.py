# SPDX-License-Identifier: GPL-3.0-or-later
"""Fennel Mist: E Dorian, 4/4, 72 bpm, 24 bars (80.3 s). Bassoon and horn with a flute descant,
harp and strings.

Slow, misty and pastoral. The low lead (bassoon, then horn) leaves the top of the
texture to a descant, so the line sits *above* the tune here (``counter_above``).

Form and harmony
    A   1-8   Bassoon. The Dorian C# (A/E over an E pedal, bar 2) is the piece's
              signature colour; the Aeolian C of bar 5 is its shadow. The section ends
              on Bsus4 -> Bm.
    B   9-16  Horn, in the relative G major, over a falling bass G-F#-E-C-B, then Gmaj7,
              and F#m7b5 -> B7 pulling back to E minor.
    A'  17-24 Bassoon again, re-harmonised (A/C#, C, D, Em/G, Cmaj7), with a new
              continuation under a rising flute descant (F#6 at bar 20). B7 at bar 24
              falls into bar 1.

The descant is drafted above the tune by ``counter_line`` (clarinet, bars 1-8) and
written by hand for bars 9-24 (flute), where the draft hovered on G5/A5.

Moods (one timeline)
    calm      Bassoon/horn lead and descant, quaver harp arpeggios from bar 1, quiet
              violin and viola pads, sustained cellos (basses from B), a glockenspiel
              halo on the long notes of bars 13-15.
    building  Harp in semiquavers, pizzicato off-beats, a pizzicato root-fifth-octave
              walk, staccato clarinet arpeggios in B, and frame drum, shaker and bongos.
    combat    Not just louder. The horn takes the whole tune with the bassoon doubling
              it. Cellos play a semiquaver spiccato engine (root-octave-fifth, rising
              minor figure at the end of each bar), under tremolo violas and a violin
              quaver ostinato. Basses drive in quavers over a sustained contrabass and
              tuba floor, trombones stab on 1, the "and" of 3, and 4, and toms,
              timpani, bass drum, snare and tambourine form the drum line.

Approved by the maintainer by ear.
"""
from glob2music.backends.sfizz import PERC_KIT as K
from glob2music.score import (Accompaniment, Part, Score, counter_line, fold_into_range, octave, override,
                              parse_harmony, parse_line)

BPB = 4

HARMONY = """
Em | A/E | Em:2 Em/G:2 | D/F# G | C | G/B D | Am7 | Bsus4 Bm |
G | D/F# | Em7 | Cmaj7 | G/B | Am7/C D | Gmaj7 | F#m7b5 B7 |
Em | A/C# | C | D | Em/G | Cmaj7 | Am7 F#m7b5 | Bsus4 B7
"""

MELODY = """
e4:q. f#4:e g4:q b4:q | c#5:h. b4:q | a4:q. g4:e f#4:q e4:q | d4:h g4:q a4:q |
b4:q. a4:e g4:q e4:q | d4:h f#4:q a4:q | c5:h b4:e a4 g4 e4 | e4:h f#4:q d4:q |
d5:q. b4:e g4:q a4:q | f#4:h a4:q d5:q | e5:q. d5:e b4:q g4:q | g4:q. e4:e b4:h |
d5:q. c5:e b4:h | a4:q e5:q d5:q. c5:e | b4:q. f#4:e g4:h | c5:q. a4:e b4:q d#4:q |
e4:q. g4:e b4:q a4:q | c#5:q. b4:e a4:h | g4:q. e4:e g4:q c5:q | a4:h f#4:q d4:q |
e4:q g4:q b4:h | c5:q. b4:e g4:q e4:q | e4:q a4:q c5:h | e4:q f#4:q d#4:h
"""

BASS = """
e2:w | e2 | e2:h g2:h | f#2:h g2:h | c3:w | b2:h d3:h | a2:w | b2:w |
g2:w | f#2 | e2 | c3 | b2 | c3:h d3:h | g2:w | f#2:h b2:h |
e2:w | c#3 | c3 | d3 | g2 | c3 | a2:h f#2:h | b2:w
"""

DESCANT = """
r:h b5:q. a5:e | a5:h. f#5:q | g5:h. d6:q | e6:h d6:q e6:q |
b5:h. a5:q | c6:h a5:q f#5:q | g5:q. a5:e b5:h | a5:h f#5:q b5:q |
g5:q. b5:e e6:h | e6:h c#6:q. b5:e | c6:h e6:h | f#6:h d6:q f#5:q |
b5:h g5:h | g5:q. a5:e g5:h | c6:h a5:h | b5:h a5:q f#5:q
"""

_harmony = parse_harmony(HARMONY, BPB)
_melody = parse_line(MELODY, BPB, part='melody')
_bass = parse_line(BASS, BPB, part='bass')
# E Dorian has the notes of D major, hence key_pc 2 for passing notes.
_counter = override(counter_line(_harmony, _melody, _bass, 67, 86, key_pc=2, seed_pitch=76, above=True),
                    DESCANT, 9, BPB)

SCORE = Score(
    title='fennel-mist', bpm=72.0, beats_per_bar=BPB, bars=24,
    harmony=_harmony, melody=_melody, counter=_counter, bass=_bass,
    intensity=[0.56, 0.58, 0.60, 0.60, 0.64, 0.66, 0.68, 0.64,
               0.66, 0.70, 0.74, 0.76, 0.80, 0.84, 0.86, 0.82,
               0.80, 0.88, 0.92, 0.88, 0.80, 0.72, 0.64, 0.58],
    phrases=[(1, 4), (5, 8), (9, 12), (13, 16), (17, 20), (21, 24)],
    sections={'A': (1, 8), 'B': (9, 16), 'A2': (17, 24)},
    rubato=[(8, 2.0, 0.94), (8, 3.0, 0.90), (16, 3.0, 0.95), (19, 3.0, 0.96), (20, 3.0, 0.94)],
    counter_above=True,
)

# Combat leans on the low register: sustained basses and low drums up, driving basses a little.
MIX_ADJUST = {'combat': {'bass_sus': 4.0, 'low_drum': 3.0, 'bass': 1.0}}

S = SCORE
G = Accompaniment(S)
M, C, B = S.melody, S.counter, S.bass
bars = S.bars_of

#: The combat cello engine per written bass note: twelve semiquavers of root-root-octave-
#: root-fifth-root-octave-root, then a rising minor figure (root, minor third, fourth,
#: fifth) on the bar's last beat; accents on every beat.
ENGINE = [(k * 0.25, [0, 0, 12, 0, 7, 0, 12, 0][k % 8] if k % 16 < 12 else [0, 3, 5, 7][k % 4], 0.25, k % 4 == 0)
          for k in range(16)]


def arrange(mood):
    lead_a = bars(M, 1, 8) + bars(M, 17, 24)
    if mood == 'combat':
        P = [Part('horn_lead', 'horn_sus', bars(M, 1, 24), 'lead'),
             Part('bassoon', 'bassoon_sus', lead_a, 'lead_dbl')]
    else:
        P = [Part('bassoon', 'bassoon_sus', lead_a, 'lead'),
             Part('horn_lead', 'horn_sus', bars(M, 9, 16), 'lead')]
    P += [Part('clarinet_desc', 'clarinet_sus', bars(C, 1, 8), 'counter'),
          Part('flute_desc', 'flute_sus', bars(C, 9, 24), 'counter')]

    if mood == 'calm':
        halo = [n.moved(12) for n in bars(M, 13, 15) if n.dur >= 1.5]
        P += [
            Part('harp', 'harp', G.arpeggio(1, 24, 52, 76, [0, 1, 2, 3, 2, 1, 2, 1], ring=2.0), 'harp'),
            Part('violins_pad', 'violins_sus_q', G.pad(1, 24, 62, 76, 2), 'pad'),
            Part('violas_pad', 'violas_sus_q', G.pad(1, 24, 52, 64, 2), 'pad'),
            Part('cellos', 'cellos_sus_q', B, 'bass'),
            Part('basses', 'basses_sus_q', octave(bars(B, 9, 24), -1), 'bass'),
            Part('glock', 'glockenspiel_vcsl', halo, 'sparkle'),
        ]
    else:
        P += [
            Part('harp', 'harp', G.arpeggio(1, 24, 52, 81, [0, 1, 2, 3, 4, 3, 2, 1], sub=0.25, n=5, ring=3.0), 'harp'),
            Part('violins_pad', 'violins_sus_q', G.pad(1, 24, 62, 76, 2), 'pad'),
            Part('violins_pizz', 'violins_pizz', G.chords_at(1, 24, 64, 79, (0.5, 1.5, 2.5, 3.5)), 'pizz'),
        ]
    if mood == 'building':
        P += [
            Part('violas_pad', 'violas_sus', G.pad(1, 24, 52, 64, 2), 'pad'),
            Part('cellos_pizz', 'cellos_pizz', G.bass_pattern(1, 24, [(0, 0, 1, True), (1, 7, 1, False), (2, 12, 1, False),
                                                                       (3, 7, 1, False)]), 'bass'),
            Part('basses_pizz', 'basses_pizz', G.bass_pattern(1, 24, [(0, -12, 1, True), (2, -12, 1, False)]), 'bass'),
            Part('clarinet_stac', 'clarinet_stac', G.arpeggio(9, 16, 60, 76, [0, 2, 1, 3], ring=1.0), 'pizz'),
            Part('frame_drum', 'frame_drum', G.hits(1, 24, lambda b: [0, 2, 3.5] if b % 4 else [0, 1.5, 2, 3, 3.5],
                                                    lambda b, x: 61 if x in (0, 2) else 64), 'perc'),
            Part('shaker', 'shaker', G.hits(5, 24, [0.5, 1.5, 2.5, 3.5], 63), 'perc_soft'),
            Part('bongos', 'bongos', G.hits(9, 24, [1.75, 3.25], 60), 'perc_soft'),
        ]
    if mood == 'combat':
        P += [
            Part('violas_trem', 'violas_trem', G.pad(1, 24, 52, 66, 2), 'pad'),
            Part('cellos_spic', 'cellos_spic', G.bass_pattern(1, 24, ENGINE), 'bass'),
            Part('basses_spic', 'basses_spic', octave(G.bass_pattern(1, 24, [(k * 0.5, 0, 0.5, k % 2 == 0)
                                                                              for k in range(8)]), -1), 'bass'),
            Part('basses_sus', 'basses_sus', octave(B, -1), 'bass_sus'),
            Part('tuba', 'tuba_sus', fold_into_range(octave(B, -1), 29, 127), 'bass_sus'),
            Part('trombones', 'trombone_stac', G.chords_at(1, 24, 43, 60, (0.0, 2.5, 3.0), dur=0.5, skip_root=False), 'pizz'),
            Part('violins_spic', 'violins_spic', G.arpeggio(1, 24, 64, 84, [0, 1, 2, 1], n=3, ring=1.0), 'ostinato'),
            Part('timpani', 'timpani', G.timpani(1, 24, lambda b: [0, 1.5, 2, 3.5] if b % 4
                                                 else [0, 1, 1.5, 2, 2.5, 3, 3.25, 3.5, 3.75]), 'low_drum'),
            Part('bassdrum', 'bass_drum_2', G.hits(1, 24, [0, 2, 3.5], 62), 'low_drum'),
            Part('toms', 'tom', G.hits(1, 24, lambda b: [1, 3] if b % 2 else [1, 2.5, 3, 3.5],
                                       lambda b, x: 62 if x < 3 else 64), 'perc'),
            Part('snare', 'snare_rope', G.hits(1, 24, lambda b: [1, 3] if b % 4 else [1, 3, 3.25, 3.5, 3.75], 62), 'perc_snare'),
            Part('tamb', 'perc_kit', G.hits(1, 24, [0.5, 1.5, 2.5, 3.5], K['tambourine']), 'perc_soft', pan=0.4),
        ]
    return P
