# SPDX-License-Identifier: GPL-3.0-or-later
"""The symbolic score layer (glob2music.score): notation, model, generators, counter-lines,
humanisation, MIDI, static checks and mixing. Small hand-made scores; no rendering."""
from pathlib import Path
import sys
import tempfile
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from glob2music.score import (Accompaniment, Instrument, Note, Part, Score, check_score, chord_pcs,  # noqa: E402
                              counter_line, find_dropouts, fold_into_range, override, parse_harmony, parse_line,
                              perform_part, player_seed, voice_lead)
from glob2music.score import checks, midi, mix  # noqa: E402
from glob2music.score.notation import NotationError, name_to_midi  # noqa: E402
from glob2music.score.perform import seed_namespace  # noqa: E402
from glob2music.spec import SAMPLE_RATE as SR  # noqa: E402

FLUTE = Instrument('flute', 'sustain', lag=-0.01, low=60, high=96)
HARP = Instrument('harp', 'ring', low=28, high=101)
PIZZ = Instrument('pizz', 'short', low=36, high=86)
INSTRUMENTS = {'flute': FLUTE, 'harp': HARP, 'pizz': PIZZ}


def small_score(**kw):
    """Eight bars of 4/4 in C: a tune, a counter-line below it and a half-note bass."""
    harmony = parse_harmony('C | Am | F | G7 | C | F | Dm7 G7 | C')
    melody = parse_line('e5:q d5 c5 g5 | a5:h e5 | f5:q a5 c6 a5 | g5:w | e5:q g5 c6 g5 | a5:h f5 | '
                        'f5:q d5 b4 d5 | c5:w')
    bass = parse_line('c3:h g2 | a2 e2 | f2 c3 | g2 b2 | c3 e3 | f3 a2 | d3 g2 | c3:w')
    args = dict(title='test-set', bpm=120, beats_per_bar=4, bars=8, harmony=harmony, melody=melody,
                counter=counter_line(harmony, melody, bass, 52, 72, key_pc=0, seed_pitch=60), bass=bass,
                intensity=[0.5] * 8, phrases=[(1, 4), (5, 8)], sections={'A': (1, 4), 'B': (5, 8)})
    args.update(kw)
    return Score(**args)


class NotationTest(unittest.TestCase):
    def test_pitches_and_durations(self):
        self.assertEqual(name_to_midi('c4'), 60)
        self.assertEqual(name_to_midi('bb4'), 70)
        self.assertEqual(name_to_midi('e#5'), 77)
        notes = parse_line('r:e d5:e g5:q. a5:e! b5:q | c5+e5+g5:h g4:q~ g4:q', part='t')
        self.assertEqual([n.start for n in notes], [0.5, 1.0, 2.5, 3.0, 4.0, 6.0])
        self.assertEqual(notes[1].dur, 1.5)
        self.assertIn('!', notes[2].artic)
        self.assertEqual(notes[4].pitches, [72, 76, 79])
        self.assertEqual(notes[5].dur, 2.0)                    # the tie merged two crotchets

    def test_wrong_bar_length_fails_loudly(self):
        with self.assertRaises(NotationError):
            parse_line('c4:q d4 e4 | f4:w', part='melody')

    def test_compound_metre(self):
        notes = parse_line('e5:q a5:e c#6:q a5:e | e5:q. c#5:q.', beats_per_bar=3)
        self.assertEqual([n.start for n in notes], [0, 1, 1.5, 2.5, 3, 4.5])

    def test_harmony(self):
        h = parse_harmony('Gm | Eb F/A | Gm/Bb:2 D7sus4/A:1 D7:1', 4)
        self.assertEqual([(t, d) for t, d, _ in h], [(0, 4), (4, 2), (6, 2), (8, 2), (10, 1), (11, 1)])
        root, pcs, bass = chord_pcs('Gm7/F')
        self.assertEqual((root, pcs, bass), (7, [7, 10, 2, 5], 5))
        with self.assertRaises(NotationError):
            parse_harmony('C:3 F:3', 4)

    def test_voice_leading_moves_little(self):
        prev = voice_lead(None, chord_pcs('C')[1], 55, 72, 3)
        nxt = voice_lead(prev, chord_pcs('F')[1], 55, 72, 3)
        self.assertLessEqual(sum(abs(a - b) for a, b in zip(prev, nxt)), 3)
        self.assertEqual({p % 12 for p in nxt}, {5, 9, 0})


class ModelTest(unittest.TestCase):
    def test_tempo_map_rubato_and_inverse(self):
        s = small_score(rubato=[(4, 3.0, 0.5)])
        self.assertAlmostEqual(s.tempo.sec(15) - s.tempo.sec(14), 0.5)
        self.assertAlmostEqual(s.tempo.sec(16) - s.tempo.sec(15), 1.0)     # beat 4 of bar 4 at half speed
        self.assertAlmostEqual(s.tempo.beat(s.tempo.sec(21.25)), 21.25)
        self.assertEqual(s.loop_frames(SR), int(round(s.seconds * SR)))

    def test_no_rubato_at_the_seam(self):
        with self.assertRaises(ValueError):
            small_score(rubato=[(8, 3.0, 0.9)])

    def test_fold_into_range(self):
        out = fold_into_range([Note(0, 1, [24, 90])], 29, 77)
        self.assertEqual(out[0].pitches, [36, 66])


class GeneratorTest(unittest.TestCase):
    def setUp(self):
        self.s = small_score()
        self.g = Accompaniment(self.s)

    def test_chords_at_offsets_follow_the_harmony(self):
        notes = self.g.chords_at(7, 7, 60, 76, (0.5, 2.5))       # bar 7: Dm7 G7
        self.assertEqual([n.start for n in notes], [24.5, 26.5])
        self.assertTrue(set(p % 12 for p in notes[1].pitches) <= set(chord_pcs('G7')[1]))

    def test_bass_steps_accent_first(self):
        notes = self.g.bass_steps(1, 1, [0, None, 7], 0.5)
        self.assertEqual([n.pitches[0] for n in notes], [48, 55, 48, 43, 50, 43])
        self.assertEqual([('>' in n.artic) for n in notes][:2], [True, False])

    def test_run_up_wraps_to_bar_one(self):
        run = self.g.run_up(9, steps=4)
        self.assertEqual([n.pitches[0] for n in run], [44, 45, 46, 47])
        self.assertEqual(run[-1].end, 32.0)

    def test_pad_holds_unchanged_voicings(self):
        notes = self.g.pad(1, 2, 52, 64, 2)
        self.assertTrue(all(n.start in (0, 4) for n in notes))

    def test_walking_approaches_next_note(self):
        walk = self.g.walking(1, 1, scale_pcs=set(range(12)))
        # C3 then a step above the leap's target G2; G2 then (next note a step away) a
        # chord tone near a fourth above
        self.assertEqual([n.pitches[0] for n in walk], [48, 45, 43, 48])


class CounterTest(unittest.TestCase):
    def test_counter_line_avoids_bass_parallels_and_stays_below(self):
        # The solver judges segment starts; parallels against the melody between
        # segment starts are left to checks.parallels and hand overrides.
        s = small_score()
        self.assertEqual([f for f in checks.parallels(s) if 'counter/bass' in f.message], [])
        for t, _, _ in s.harmony:
            mel = [m.pitches[0] for m in s.melody if m.start <= t < m.end]
            ctr = [n.pitches[0] for n in s.counter if n.start <= t < n.end]
            if mel and ctr:
                self.assertLess(ctr[0], mel[0])

    def test_override_replaces_whole_bars(self):
        line = override(small_score().counter, 'e4:w | f4:w', 3, 4)
        self.assertEqual([n.pitches[0] for n in line if 8 <= n.start < 16], [64, 65])


class PerformTest(unittest.TestCase):
    def test_deterministic_and_mood_dependent(self):
        s = small_score()
        part = Part('flute', 'flute', s.melody, 'lead')
        seed = player_seed(seed_namespace('test-set'), 'flute')
        a = perform_part(s, 'building', part, FLUTE, seed)
        b = perform_part(s, 'building', part, FLUTE, seed)
        self.assertEqual(a.events, b.events)
        calm = perform_part(s, 'calm', part, FLUTE, seed)
        combat = perform_part(s, 'combat', part, FLUTE, seed)
        self.assertLess(np.mean([e[3] for e in calm.events]), np.mean([e[3] for e in combat.events]))
        self.assertTrue(a.cc11)                                 # sustained instruments get expression
        self.assertNotEqual(seed, player_seed(seed_namespace('test-set', 7), 'flute'))

    def test_repeated_key_is_released_before_it_restrikes(self):
        s = small_score()
        notes = [Note(t, 1.0, [60]) for t in range(8)]          # legato-length repeated notes
        for inst, role in ((HARP, 'harp'), (FLUTE, 'pad')):
            pp = perform_part(s, 'calm', Part('x', inst.name, notes, role), inst, 1)
            ev = sorted(pp.events)
            for (on, off, _, _), (on2, _, _, _) in zip(ev, ev[1:]):
                self.assertLessEqual(off, on2 - 0.02 + 1e-9)

    def test_short_and_ring_lengths(self):
        s = small_score()
        pz = perform_part(s, 'calm', Part('p', 'pizz', [Note(0, 4, [60])], 'pizz'), PIZZ, 1)
        self.assertLessEqual(pz.events[0][1] - pz.events[0][0], 0.35 + 1e-9)
        self.assertEqual(pz.cc11, [])


class MidiTest(unittest.TestCase):
    def test_performance_round_trip(self):
        s = small_score()
        pp = perform_part(s, 'calm', Part('flute', 'flute', s.melody, 'lead'), FLUTE, 3)
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / 'flute.mid'
            midi.write_performance(path, pp, s.seconds, preroll_s=2.0, tail_s=4.0)
            notes = midi.read_notes(path, preroll_s=2.0)
            self.assertEqual(len(notes), len(pp.events))
            for (on, off, p), (on2, off2, p2, _) in zip(notes, sorted(pp.events)):
                self.assertAlmostEqual(on, on2, delta=1e-3)
                self.assertEqual(p, p2)
            midi.write_score(Path(d) / 'score.mid', s, {'calm': [Part('flute', 'flute', s.melody, 'lead')]})


class CheckTest(unittest.TestCase):
    def test_range_errors(self):
        s = small_score()
        parts = {'calm': [Part('low', 'flute', [Note(0, 1, [40])], 'lead')]}
        found = check_score(s, parts, INSTRUMENTS)
        self.assertEqual(found[0].level, 'error')
        self.assertEqual(found[0].kind, 'range')

    def test_parallel_octaves_and_repeated_bars(self):
        harmony = parse_harmony('C | G | C | G')
        melody = parse_line('c5:w | d5:w | c5:w | d5:w')
        bass = parse_line('c3:w | d3:w | e3:w | g2:w')
        s = small_score(bars=4, harmony=harmony, melody=melody, bass=bass, counter=parse_line('e4:w | b3:w | e4:w | b3:w'),
                        intensity=[0.5] * 4, phrases=[(1, 4)], sections={'A': (1, 4)})
        kinds = [(f.kind, f.message) for f in check_score(s, {}, INSTRUMENTS)]
        self.assertIn(('parallel', 'octaves melody/bass into bar 2 beat 1'), kinds)
        self.assertTrue(any(k == 'reuse' for k, _ in kinds) is False)      # single-note bars are not 'reuse'
        s.melody = parse_line('c5:h e5 | d5:h b4 | c5:h e5 | d5:h b4')
        self.assertIn('reuse', [f.kind for f in checks.repeated_bars(s)])

    def test_dropout_scan(self):
        stem = np.zeros((int(6 * SR), 2))
        t = np.arange(int(1.5 * SR)) / SR
        tone = 0.1 * np.sin(2 * np.pi * 440 * t)
        stem[int(2.0 * SR):int(3.5 * SR), 0] = tone                    # note 1 sounds throughout
        stem[int(4.0 * SR):int(4.3 * SR), 0] = tone[:int(0.3 * SR)]     # note 2 dies after 0.3 s
        notes = [(0.0, 1.5, 69), (2.0, 3.5, 69), (2.5, 2.7, 69)]       # the last one is too short to judge
        silent, checked = find_dropouts(stem, notes, SR, preroll_s=2.0)
        self.assertEqual(silent, [(2.0, 3.5, 69)])
        self.assertEqual(len(checked), 2)


class MixTest(unittest.TestCase):
    def test_pan_centre_is_unity_and_sides_keep_power(self):
        y = np.random.default_rng(0).standard_normal((1000, 2))
        np.testing.assert_allclose(mix.pan(y, 0.0), y, atol=1e-12)
        left = mix.pan(np.ones((10, 2)), -1.0)
        self.assertAlmostEqual(left[0, 1], 0.0, places=12)

    def test_level_reference(self):
        y = np.zeros((SR * 4, 2))
        y[SR:2 * SR] = 0.1
        self.assertAlmostEqual(mix.level_reference(y, 'sustain'), -20.0, delta=0.1)

    def test_stem_highpass_rules(self):
        self.assertEqual(mix.stem_highpass_hz('lead', FLUTE), 120)
        self.assertEqual(mix.stem_highpass_hz('bass', PIZZ), 30)
        self.assertEqual(mix.stem_highpass_hz('lead', Instrument('horn', 'sustain', highpass_hz=60)), 60)

    def test_ir_is_deterministic_unit_energy(self):
        a, b = mix.make_ir(), mix.make_ir()
        np.testing.assert_array_equal(a, b)
        self.assertAlmostEqual((a ** 2).sum() / 2, 1.0, places=2)


if __name__ == '__main__':
    unittest.main()
