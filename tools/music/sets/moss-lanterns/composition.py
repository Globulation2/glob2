# SPDX-License-Identifier: GPL-3.0-or-later
"""Moss Lanterns: G minor, 4/4, 92 bpm, 32 bars (83.8 s). Woodwinds, harp, glockenspiel, strings.

The original soundtrack's calm and building tracks are G minor at about 92 bpm, with a
high, whistle-like lead over a moving bass. This piece keeps that identity (key, tempo,
a high woodwind lead, a contrapuntal texture) and adds what the original lacks: real
harmonic movement, phrases that develop and return, and an acoustic chamber colour.

Form and harmony
    A   1-8   The theme on flute in G minor: a rising D-G-A-Bb figure. A half
              cadence on D at bar 4, then a turn to Bb (the relative major) at bar 8.
    B   9-16  Development on clarinet in Bb/Eb: the opening figure in sequence over
              Ebmaj7 - Dm7 Gm7 - Cm7 F7, with a flute counter-line *above* it. A chromatic
              turn (Edim7, G7b9, Ab) lands on D7 at bar 16.
    C   17-24 The lift: G major on oboe, with the motif inverted and a staccato clarinet
              answer. A chromatic-mediant climb Eb -> F -> G (bars 21-23) is the climax;
              Cm6 at bar 24 darkens it back towards the minor.
    A'  25-32 The theme returns on flute with a new continuation and a lyrical violin
              counter-line. Bar 32 ends on a D7 pickup that falls straight into bar 1.

The intensity arc rises from 0.40 to 0.94 at bar 23 and returns to 0.44, so the
loop seam does not jump in level. Phrase-end rubato sits at bars 4, 8, 12, 16, 24
and 28, and bar 32 stays in strict time.

Moods (one timeline; melody and harmony identical in all three)
    calm      Lead and a sparse clarinet counter-line, rolled harp chords, a quiet viola
              pad from B on, sustained cellos, a few glockenspiel sparkles.
    building  Adds drive: harp in quavers, pizzicato off-beats, a walking pizzicato
              bass, staccato bassoon, and light hand percussion (conga, quinto, shaker,
              triangle, log drum) that thickens section by section.
    combat    Adds weight and urgency, keeping the tune: violins doubling the lead an
              octave down, semiquaver spiccato violas, driving cellos and basses with a
              sustained contrabass floor, horn on the counter-line, horn stabs,
              timpani with phrase-end rolls, and a gentle "toy soldier" bass-drum,
              snare and tambourine pattern.

Approved by the maintainer by ear.
"""
from glob2music.backends.sfizz import PERC_KIT as K
from glob2music.score import Accompaniment, Note, Part, Score, octave, parse_harmony, parse_line

BPB = 4

HARMONY = """
Gm | Eb F/A | Gm Dm/F | Cm6 D | Gm | Eb Cm7 | F7 | Bb Bb/D |
Ebmaj7 | Dm7 Gm7 | Cm7 F7 | Bb Bb7/Ab | Eb/G Edim7 | Bb/F G7b9 | Cm Ab | D7sus4 D7 |
G G/B | Em C | Am7 D7 | G/B C | Eb/G | F | G Em7 | Cm6 D7 |
Gm | Eb F/A | Gm Bb/F | Ebmaj7 D7 | Gm Gm/F | Eb Cm6 | Gm/D D7/C | Gm/Bb:2 D7sus4/A:1 D7:1
"""

MELODY = """
r:e d5:e g5 a5 bb5:q a5:e g5 | g5:q bb5:e g5 a5:q. c6:e | d6:q. bb5:e a5 f5 d5:q | eb5:e g5 c6 a5 f#5:h |
r:e d5:e g5 a5 bb5 c6 d6:q | eb6:q d6:e c6 bb5 g5 eb5:q | f5:e a5 c6 eb6 d6:q c6 | bb5:h r:h |
r:e bb4:e eb5 f5 g5:q. d5:e | f5:q d5 r:e g4:e bb4 c5 | eb5:q. d5:e c5 a4 f4 eb5 | d5:h r:e f5:e d5 ab4 |
r:e g4:e bb4 eb5 g5:q e5:e db5 | d5:e f5 bb5:q b4:e d5 f5 ab5 | g5:q eb5:e c5 c5:q eb5:e ab5 | g5:q. f#5:e d5 e5 f#5 a5 |
b5:q a5:e g5 d5:q g5:e a5 | b5:q. g5:e e5:q g5:e c6 | c6:q b5:e a5 f#5:q a5:e c6 | b5:q. a5:e g5 e5 g5:q |
bb5:q. g5:e eb5:q g5:e bb5 | c6:q. a5:e f5:q a5:e c6 | d6:q. b5:e g5 b5 d6 e6 | eb6:q. c6:e a5 f#5 d5:q |
r:e d5:e g5 a5 bb5:q a5:e g5 | g5:q bb5:e g5 a5:q. c6:e | d6:q. bb5:e c6 bb5 f5:q | g5:e bb5 d6 c6 a5:q f#5 |
g5:q d6 d6:e c6 bb5 a5 | g5:q. bb5:e a5:q. g5:e | bb5:q a5:e g5 f#5:q. a5:e | g5:h r:h
"""

# Hand-written counter-line: below the melody in A, C and A' (clarinet, staccato
# clarinet, violins), above it in B (flute).
COUNTER = """
bb4:h g4:h | bb4:h c5:h | bb4:h f4:h | g4:q eb4 a4:e c5 d5 c5 |
bb4:h g4:h | g4:h eb4:h | a4:h eb5:h | d5:q bb4:e c5 d5 c5 bb4 g4 |
bb5:h c6:q bb5 | a5:h bb5:q d6 | eb6:h c6:q a5 | d6:h. c6:q |
eb6:h bb5:h | d6:h d6:q b5 | eb6:h c6:h | d6:h c6:q a5 |
r:q d5:e! b4! r:q g4:e! b4! | r:q e5:e! b4! r:q e4:e! g4! | r:q c5:e! a4! r:q f#4:e! a4! | r:q d5:e! b4! r:q e5:e! c5! |
bb4:h g4:h | a4:h c5:h | b4:h d5:q b4 | c5:q eb5 d5 c5 |
d5:h bb4:q d5 | bb4:h c5:h | bb4:h d5:h | bb4:h c5:h |
bb4:w | bb4:h c5:h | g4:h a4:h | d5:h c5:h
"""

BASS = """
g2:h d3 | eb3 a2 | g2 f2 | c3 d3 | g2 bb2 | eb3 c3 | f2 a2 | bb2 d3 |
eb3 bb2 | d3 g2 | c3 f2 | bb2 ab2 | g2 e2 | f2 g2 | c3 ab2 | d3 d2 |
g2 b2 | e3 c3 | a2 d3 | b2 c3 | g2 bb2 | f3 c3 | g3 e3 | c3 d3 |
g2 d3 | eb3 a2 | g2 f2 | eb3 d3 | g3 f3 | eb3 c3 | d3 c3 | bb2:h a2:q d3
"""

SCORE = Score(
    title='moss-lanterns', bpm=92.0, beats_per_bar=BPB, bars=32,
    harmony=parse_harmony(HARMONY, BPB),
    melody=parse_line(MELODY, BPB, part='melody'),
    counter=parse_line(COUNTER, BPB, part='counter'),
    bass=parse_line(BASS, BPB, part='bass'),
    intensity=[0.40, 0.42, 0.46, 0.44, 0.47, 0.52, 0.56, 0.50,
               0.55, 0.58, 0.62, 0.60, 0.64, 0.70, 0.74, 0.78,
               0.66, 0.70, 0.74, 0.72, 0.80, 0.86, 0.94, 0.84,
               0.70, 0.68, 0.66, 0.64, 0.60, 0.56, 0.50, 0.44],
    phrases=[(1, 4), (5, 8), (9, 12), (13, 16), (17, 20), (21, 24), (25, 28), (29, 32)],
    sections={'A': (1, 8), 'B': (9, 16), 'C': (17, 24), 'A2': (25, 32)},
    rubato=[(4, 3.0, 0.95), (8, 2.0, 0.93), (8, 3.0, 0.90), (16, 3.0, 0.95),
            (24, 2.0, 0.94), (24, 3.0, 0.90), (12, 3.0, 0.97), (28, 3.0, 0.96)],
    counter_above=[(9, 16)],
)

# The harp sits 2 dB under its catalogue level in every mood.
MIX_ADJUST = {mood: {'harp': -2.0} for mood in ('calm', 'building', 'combat')}

#: Scale tones the walking bass may approach through: G natural minor plus the raised
#: 6th and 7th (E, F#) of G melodic minor and the B of the G major section.
WALK_SCALE = {7, 9, 10, 0, 2, 3, 5, 11, 4, 6}

#: Seats of the players who share the VSCO percussion kit.
KIT_PAN = {'conga': 0.3, 'quinto': 0.45, 'shaker': -0.35, 'triangle': -0.5, 'log': -0.22,
           'bassdrum': 0.0, 'snare': 0.06, 'tamb': 0.4, 'snare_roll': 0.0}

S = SCORE
G = Accompaniment(S)
M, C, B = S.melody, S.counter, S.bass
bars = S.bars_of


def kit(name, notes, role):
    """A player on the shared percussion kit, at its own seat."""
    return Part(name, 'perc_kit', notes, role, pan=KIT_PAN[name])


def bass_timp_note(bar):
    """The bar's first written bass pitch class between B1 and Bb2 (a B drops to B1)."""
    p = 36 + (bars(B, bar, bar)[0].pitches[0] - 36) % 12
    return p - 12 if p > 46 else p


def arrange(mood):
    P = [
        Part('flute', 'flute_sus', bars(M, 1, 8) + bars(M, 25, 32), 'lead'),
        Part('clarinet', 'clarinet_sus', bars(M, 9, 16), 'lead'),
        Part('oboe', 'oboe_sus', bars(M, 17, 24), 'lead'),
    ]
    if mood == 'calm':
        P.append(Part('clarinet_ctr', 'clarinet_sus', bars(C, 5, 8) + bars(C, 21, 24) + bars(C, 25, 32), 'counter'))
    else:
        P.append(Part('clarinet_ctr', 'clarinet_sus', bars(C, 1, 8) + bars(C, 21, 24), 'counter'))
        P.append(Part('violins_ctr', 'violins_sus', bars(C, 25, 32), 'counter'))
    P.append(Part('flute_ctr', 'flute_sus', bars(C, 9, 16), 'counter'))
    P.append(Part('clarinet_stac', 'clarinet_stac', bars(C, 17, 20), 'counter'))

    if mood == 'calm':
        harp = (G.rolled(1, 8, 55, 79, 4, spread=0.09, hold='stagger')
                + G.rolled(9, 16, 55, 79, 4, spread=0.09, every=2, hold='stagger')
                + G.arpeggio(17, 20, 55, 84, [0, 2, 1, 3, 2, 1, None, 3], ring=1.6)
                + G.rolled(21, 24, 55, 84, 5, spread=0.09, hold='stagger')
                + G.rolled(25, 32, 55, 79, 4, spread=0.09, every=2, hold='stagger'))
        sparkle = [n for n in bars(M, 1, 1) + bars(M, 17, 17) + bars(M, 21, 23) + bars(M, 25, 25) if n.dur <= 1.0][::2]
        P += [
            Part('harp', 'harp', harp, 'harp'),
            Part('violas_pad', 'violas_sus_q', G.pad(9, 32, 53, 72, 2, drop_fifth=True), 'pad'),
            Part('cellos', 'cellos_sus_q', B, 'bass'),
            Part('basses', 'basses_sus_q', octave(bars(B, 13, 16) + bars(B, 21, 28), -1), 'bass'),
            Part('glock', 'glockenspiel', sparkle, 'sparkle'),
        ]
    else:
        outer = [0, 1, 2, 3, 4, 3, 2, 1] if mood == 'building' else [0, 2, 4, 2, 1, 3, 4, 3]
        harp = (G.arpeggio(1, 16, 55, 84, outer, n=5, ring=1.6)
                + G.arpeggio(17, 24, 55, 86, [0, 2, 4, 2, 3, 1, 4, 2], n=5, ring=1.6)
                + G.arpeggio(25, 32, 55, 84, outer, n=5, ring=1.6))
        offbeats = (0.5, 1.5, 2.5, 3.5)
        P += [
            Part('harp', 'harp', harp, 'harp'),
            Part('violas_pad', 'violas_sus', G.pad(1, 32, 53, 72, 2, drop_fifth=True), 'pad'),
            Part('violins_pizz', 'violins_pizz', G.chords_at(1, 24, 62, 79, offbeats) + G.chords_at(25, 32, 62, 79, offbeats), 'pizz'),
        ]
        if mood == 'building':
            walk = G.walking(1, 32, WALK_SCALE)
            P.append(Part('cellos_pizz', 'cellos_pizz', walk, 'bass'))
            P.append(Part('basses_pizz', 'basses_pizz', octave([n for n in walk if n.start % 2 < 1e-6], -1), 'bass'))
        P.append(Part('bassoon', 'bassoon_stac', G.walking(9, 16, WALK_SCALE) + G.walking(21, 32, WALK_SCALE), 'bass'))
        P.append(Part('glock', 'glockenspiel', bars(M, 17, 24) if mood == 'building' else bars(M, 21, 24), 'sparkle'))

    if mood == 'building':
        def conga(bar):                       # density grows section by section
            if bar <= 8:
                return [0, 2.5] if bar % 2 else [0, 1.5, 2.5]
            if bar <= 16:
                return [0, 1.5, 2.5, 3.5] if bar != 16 else [0, 1.5, 2, 2.5, 3, 3.5]
            if bar <= 24:
                return [0, 0.75, 1.5, 2.5, 3.5] if bar != 24 else [0, 1, 2, 2.5, 3, 3.25, 3.5, 3.75]
            return [0, 1.5, 2.5, 3.5] if bar != 32 else [0, 1.5, 2.5]
        P += [
            kit('conga', G.hits(1, 32, conga, K['conga']), 'perc'),
            kit('quinto', G.hits(5, 32, lambda b: [1, 3] if b % 4 else [1, 3, 3.75], K['quinto_tap']), 'perc'),
            kit('shaker', G.hits(9, 32, [0.5, 1.5, 2.5, 3.5], K['tambourine_shake']), 'perc_soft'),
            kit('triangle', G.hits(1, 32, lambda b: [0] if b in (1, 9, 17, 21, 25) else [], K['triangle']), 'perc_soft'),
            kit('log', G.hits(17, 24, [1.5, 3.0], K['log_drum_high']) + G.hits(4, 4, [3, 3.5], K['log_drum_low'])
                + G.hits(12, 12, [3, 3.5], K['log_drum_low']) + G.hits(28, 28, [3, 3.5], K['log_drum_low']), 'perc'),
        ]

    if mood == 'combat':
        def timp(bar):
            return [0, 2, 3, 3.5] if bar in (8, 16, 24) else ([0, 2] if bar % 2 else [0, 1.5, 2])

        def snare(bar):
            if bar in (8, 16, 24, 32):
                return [1, 2, 2.5, 3, 3.25, 3.5, 3.75]
            return [1, 3, 3.75] if 17 <= bar <= 24 else ([1, 3] if bar % 2 else [1, 2.75, 3])
        stabs = [Note((bar - 1) * BPB, 0.5, [bass_timp_note(bar) + 12], {'>'}) for bar in (2, 6, 10, 14, 18, 22, 26, 30)]
        rolls = [Note(31 * BPB + 2, 2.0, [bass_timp_note(32)], set()), Note(15 * BPB + 2, 2.0, [bass_timp_note(16)], set()),
                 Note(23 * BPB + 2, 2.0, [bass_timp_note(24)], set())]
        P += [
            Part('flute_dbl', 'violins_sus', octave(bars(M, 1, 8) + bars(M, 25, 32), -1), 'lead_dbl'),
            Part('oboe_dbl', 'violins_sus', octave(bars(M, 21, 24), -1), 'lead_dbl'),
            Part('violas_spic', 'violas_spic', G.arpeggio(1, 16, 50, 69, [0, 2, 1, 2], sub=0.25, n=3, ring=1.0, accent_every=4)
                 + G.arpeggio(17, 24, 52, 71, [0, None, 2, 1], sub=0.25, n=3, ring=1.0, accent_every=4)
                 + G.arpeggio(25, 32, 50, 69, [0, 2, 1, 2], sub=0.25, n=3, ring=1.0, accent_every=4), 'ostinato'),
            Part('cellos_spic', 'cellos_spic', G.bass_steps(1, 32, [0, 0, 12, 0], 0.5, accent_every=4), 'bass'),
            Part('basses_spic', 'basses_spic', octave(G.bass_steps(1, 32, [0, None, 0, None], 0.5, accent_every=4), -1), 'bass'),
            Part('basses_sus', 'basses_sus', octave(B, -1), 'bass_sus'),
            Part('horn', 'horn_sus', octave(bars(C, 9, 16), -1) + bars(C, 25, 32), 'counter'),
            Part('horn_stac', 'horn_stac', stabs, 'perc'),
            Part('timpani', 'timpani', G.timpani(1, 32, timp), 'low_drum'),
            Part('timp_roll', 'timpani_roll', rolls, 'perc'),
            kit('bassdrum', G.hits(1, 32, lambda b: [0, 2.5] if b % 4 else [0, 2, 2.5], K['bass_drum']), 'low_drum'),
            kit('snare', G.hits(1, 32, snare, K['snare']), 'perc_snare'),
            kit('tamb', G.hits(1, 32, [0.5, 1.5, 2.5, 3.5], K['tambourine']), 'perc_soft'),
            kit('log', G.hits(1, 32, lambda b: [1.75, 3.5] if b % 2 == 0 else [3.5], K['log_drum_low']), 'perc'),
            kit('snare_roll', [Note(15 * BPB + 2, 2.0, [K['snare_roll']], set()),
                               Note(23 * BPB + 2, 2.0, [K['snare_roll']], set())], 'perc_soft'),
        ]
    return P
