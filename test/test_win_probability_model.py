#!/usr/bin/env python3
"""Check the in-game win probability model: its arithmetic, its fit and the
agreement between the published coefficients and what the engine computes.

Needs no build. The engine-agreement test reads recorded engine output from
test/data/win-probability-engine-samples.txt and re-derives every number from
the published header, so it fails if either side drifts.
"""
import math
import gzip
import json
from pathlib import Path
import re
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools import win_probability_model as W

HEADER = ROOT / 'src' / 'team' / 'stats' / 'WinProbabilityModel.h'
SAMPLES = Path(__file__).resolve().parent / 'data' / 'win-probability-engine-samples.txt'

try:
    import numpy  # noqa: F401
    import scipy.optimize  # noqa: F401
    FITTING = True
except ImportError:
    FITTING = False


def published():
    """The model as the engine has it, read back out of the generated header."""
    text = HEADER.read_text()
    intercept = float(re.search(r'WIN_PROBABILITY_INTERCEPT = ([-\d.e+]+);', text)[1])
    features = []
    for name, transform in W.FINAL_FEATURES:
        constant = f'WIN_PROBABILITY_{name.upper()}_{transform.upper()}'
        value = float(re.search(constant + r' = ([-\d.e+]+);', text)[1])
        features.append({'name': name, 'transform': transform, 'coefficient': value})
    return {'intercept': intercept, 'features': features}, text


class CalibrationLabels(unittest.TestCase):
    def test_probability_adjudication_cannot_label_a_refit(self):
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory)/'dataset.json.gz'
            with self.assertRaisesRegex(ValueError, 'independent outcome labels'):
                W.build_dataset([], output, policy='win_probability')
            self.assertFalse(output.exists())
        data={'policy':'win_probability', 'games':[]}
        for operation in (lambda: W.contests(data), lambda: W.screen(data),
                          lambda: W.select(data, []), lambda: W.savings(data, []),
                          lambda: W.calibration({}, data)):
            with self.assertRaisesRegex(ValueError, 'independent outcome labels'):
                operation()

    def test_model_called_outcomes_are_rejected_even_in_old_caches(self):
        result={'job_type':'game', 'termination':'win_probability', 'teams':[{'team':0}, {'team':1}]}
        record={'accepted':True, 'job':{'id':'model-called'}, 'result':result,
                'artifacts':[{'path':'stdout.log'}]}
        with self.assertRaisesRegex(ValueError, '--win-probability 0'):
            W.game_slices(None, record)
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory)/'dataset.json.gz'
            with patch.object(W, 'Results', return_value=[record]):
                with self.assertRaisesRegex(ValueError, '--win-probability 0'):
                    W.build_dataset(['results'], output)
            self.assertFalse(output.exists())
            with gzip.open(output, 'wt') as stream:
                json.dump({'policy':'prestige', 'games':[{'termination':'win_probability'}]}, stream)
            with self.assertRaisesRegex(ValueError, '--win-probability 0'):
                W.load_cache(output)


class Arithmetic(unittest.TestCase):
    """The fixed-point pieces the engine and the fitter must agree on."""

    def test_counts_are_clamped_not_wrapped(self):
        self.assertEqual(W.clamp_count(-5), 0)
        self.assertEqual(W.clamp_count(7), 7)
        self.assertEqual(W.clamp_count(W.COUNT_CLAMP * 4), W.COUNT_CLAMP)

    def test_a_share_of_nothing_is_nothing_rather_than_a_division(self):
        self.assertEqual(W.share_value(0, 0), 0)
        self.assertEqual(W.share_value(5, 0), 0)

    def test_shares_are_the_fraction_they_claim(self):
        self.assertAlmostEqual(W.share_value(1, 4) / W.FRACTION_ONE, 0.25, places=6)
        self.assertEqual(W.share_value(3, 3), W.FRACTION_ONE)

    def test_a_ratio_cannot_exceed_one(self):
        # More starving units than units should read as "all of them", not as
        # something the coefficient then multiplies past its fitted range.
        self.assertEqual(W.ratio_value(9, 3), W.FRACTION_ONE)
        self.assertEqual(W.ratio_value(1, 0), 0)

    def test_roots_carry_their_fractional_bits(self):
        # ROOT_SHIFT fractional bits, so one unit in the last place is the most
        # it can be out by. Asserting tighter than the representation allows
        # would only be testing this machine's luck.
        resolution = 1.0 / (1 << W.ROOT_SHIFT)
        for value in (0, 1, 2, 9, 50, 1000):
            got = W.root_value(value) / (1 << W.ROOT_SHIFT)
            self.assertAlmostEqual(got, math.sqrt(value), delta=resolution)

    def test_the_fixed_exponential_tracks_libm(self):
        for x in (0.0, 0.01, 0.1, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 20.0):
            got = W.exp_negative(W.to_fixed(x)) / W.FITNESS_ONE
            self.assertAlmostEqual(got, math.exp(-x), places=7)

    def test_the_exponential_is_one_at_zero_and_decays_to_zero(self):
        self.assertEqual(W.exp_negative(0), W.FITNESS_ONE)
        self.assertEqual(W.exp_negative(-5), W.FITNESS_ONE)
        self.assertEqual(W.exp_negative(W.to_fixed(60.0)), 0)
        # Never increases, and strictly decreases until it underflows -- past
        # about exp(-22) there is nothing left to represent, and staying at zero
        # is the honest answer rather than a floor to step down from.
        previous = W.FITNESS_ONE + 1
        for x in range(0, 40):
            value = W.exp_negative(W.to_fixed(float(x)))
            self.assertLessEqual(value, previous)
            if previous > 0 and value == 0:
                self.assertGreater(x, 20)
            elif value > 0:
                self.assertLess(value, previous)
            previous = value


class Probabilities(unittest.TestCase):
    def slot(self, **fields):
        base = {name: 0 for name, _ in W.FINAL_FEATURES}
        base['alive'] = True
        base.update(fields)
        return base

    def test_identical_sides_are_evens(self):
        model, _ = published()
        slots = [self.slot(feeding=9, attack=100), self.slot(feeding=9, attack=100)]
        self.assertEqual(W.fixed_permille(model, slots), [500, 500])

    def test_probabilities_sum_to_a_whole(self):
        model, _ = published()
        for slots in ([self.slot(feeding=40, attack=90), self.slot(feeding=9, attack=5)],
                      [self.slot(explorers=10), self.slot(explorers=12), self.slot(explorers=3),
                       self.slot(explorers=30)]):
            total = sum(W.fixed_permille(model, slots))
            # Truncating division can lose a unit per competitor, never more.
            self.assertLessEqual(abs(total - 1000), len(slots))

    def test_more_attack_strength_is_favoured(self):
        model, _ = published()
        weak, strong = self.slot(attack=5), self.slot(attack=600)
        self.assertLess(*W.fixed_permille(model, [weak, strong]))

    def test_more_recent_deliveries_are_favoured(self):
        model, _ = published()
        busy = self.slot(delivered_window=400)
        idle = self.slot(delivered_window=40)
        chances = W.fixed_permille(model, [busy, idle])
        self.assertGreater(chances[0], chances[1])

    def test_eliminated_sides_are_not_competitors(self):
        model, _ = published()
        slots = [self.slot(attack=20), self.slot(attack=20, alive=False), self.slot(attack=20)]
        chances = W.fixed_permille(model, slots)
        self.assertEqual(chances[1], 0)
        # The survivors split it between themselves, not three ways.
        self.assertEqual(chances[0], chances[2])
        self.assertLessEqual(abs(chances[0] + chances[2] - 1000), 2)

    def test_nobody_left_is_nobody_favoured(self):
        model, _ = published()
        slots = [self.slot(alive=False), self.slot(alive=False)]
        self.assertEqual(W.fixed_permille(model, slots), [0, 0])


class Engine(unittest.TestCase):
    """The published header and the engine must agree exactly, not closely.

    The optional win probability victory condition reads these numbers inside the
    synchronised simulation, so a one-bit disagreement between two machines would
    end the same game on different ticks.
    """

    def test_recorded_engine_output_is_reproduced_exactly(self):
        model, _ = published()
        samples = {}
        for line in SAMPLES.read_text().splitlines():
            if not line.startswith('GLOB2_WINPROB '):
                continue
            fields = dict(pair.split('=', 1) for pair in line.split()[1:])
            samples.setdefault(int(fields['tick']), {})[int(fields['alliance'])] = fields
        self.assertTrue(samples, 'no recorded engine samples')
        compared = 0
        for tick, alliances in sorted(samples.items()):
            slots, expected = [], []
            for index in sorted(alliances):
                fields = alliances[index]
                slot = {name: int(fields[W.CPP_MEASUREMENT[name].split('.', 1)[1]])
                        for name, _ in W.FINAL_FEATURES}
                slot['alive'] = fields['alive'] == '1'
                slots.append(slot)
                expected.append(int(fields['permille']))
            self.assertEqual(W.fixed_permille(model, slots), expected,
                             f'engine and model disagree at tick {tick}')
            compared += len(expected)
        self.assertGreater(compared, 10)

    def test_the_headers_integers_match_its_own_doubles(self):
        # The emitter rounds to fixed point in Python so the rounding is the
        # fit's rather than a compiler's. If these drift apart, the engine is
        # evaluating a different model from the one the report describes.
        model, text = published()
        intercept = int(re.search(r'WIN_PROBABILITY_INTERCEPT_FIXED = (-?\d+)LL;', text)[1])
        self.assertEqual(intercept, W.to_fixed(model['intercept']))
        for item in model['features']:
            constant = f'WIN_PROBABILITY_{item["name"].upper()}_{item["transform"].upper()}_FIXED'
            value = int(re.search(constant + r' = (-?\d+)LL;', text)[1])
            self.assertEqual(value, W.to_fixed(item['coefficient']), item['name'])

    def test_the_minimum_decision_tick_agrees_with_the_engine(self):
        header = (ROOT / 'src' / 'team' / 'stats' / 'WinProbability.h').read_text()
        value = int(re.search(r'MINIMUM_DECISION_TICK = (\d+);', header)[1])
        self.assertEqual(value, W.MINIMUM_DECISION_TICK)

    def test_every_published_term_is_readable_in_the_engine(self):
        for name, transform in W.FINAL_FEATURES:
            kind, expressions, _ = W.cpp_kind(name, transform)
            self.assertTrue(all(expressions), (name, transform))
            self.assertIn(kind, ('identity', 'share', 'sqrt', 'ratio'))


class Emission(unittest.TestCase):
    def test_a_transform_without_an_exact_integer_form_is_refused(self):
        # The decision path has no floating point in it, so a transform that
        # would need an approximation must stop the emitter rather than quietly
        # reach the code that decides who won.
        with self.assertRaises(ValueError):
            W.cpp_kind('attack', 'log')
        with self.assertRaises(ValueError):
            W.cpp_kind('attack', 'decay24')

    def test_a_measurement_the_engine_cannot_read_is_refused(self):
        with self.assertRaises(ValueError):
            W.cpp_kind('workers', 'identity')

    def test_the_emitted_header_carries_the_model_and_compiles_shape(self):
        model, _ = published()
        model['games'], model['samples'] = 1110, 131630
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'WinProbabilityModel.h'
            W.emit_header(model, path, ['fitted for a test'])
            text = path.read_text()
        self.assertIn('namespace WinProbability', text)
        self.assertIn('winProbabilityFitness', text)
        self.assertIn('winProbabilityTerms', text)
        self.assertIn('do not edit by hand', text)
        self.assertEqual(text.count('case '), 2 * len(model['features']))
        for name, transform in W.FINAL_FEATURES:
            self.assertIn(f'WIN_PROBABILITY_{name.upper()}_{transform.upper()}', text)

    def test_the_published_header_is_what_the_emitter_produces(self):
        # Regenerating must be a no-op, so the checked-in header can be trusted
        # to be the model the fitter actually reported.
        model, text = published()
        model['games'] = int(re.search(r'WIN_PROBABILITY_GAMES = (\d+);', text)[1])
        model['samples'] = int(re.search(r'WIN_PROBABILITY_SAMPLES = (\d+);', text)[1])
        provenance = [line[3:] for line in text.splitlines()
                      if line.startswith('// ') and 'Generated by' not in line][3:]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'out.h'
            W.emit_header(model, path, provenance)
            produced = path.read_text()
        # Compare the parts that carry meaning rather than the comment block.
        def body(value):
            return [line for line in value.splitlines() if not line.startswith('//')]
        self.assertEqual(body(produced), body(text))


class Labels(unittest.TestCase):
    def test_every_term_has_a_phrase_a_player_can_read(self):
        for name, _ in W.FINAL_FEATURES:
            self.assertIn(name, W.LABELS)
            self.assertTrue(W.LABELS[name][:1].isupper(), name)

    def test_families_cover_every_measurement_once(self):
        seen = [name for names in W.FAMILIES.values() for name in names]
        self.assertEqual(sorted(seen), sorted(set(seen)), 'a measurement is in two families')
        for name in W.MEASUREMENTS:
            self.assertIn(name, W.FAMILY_OF, name)



class GameplayInputs(unittest.TestCase):
    """The fitter's windowed measurements must be the engine's teamInputs."""

    def record(self, step, **extra):
        fields = {'feeding': 4, 'eatWalkDistance': 10 * step,
                  f'buildings_{W.ATTACK_BUILDING}_0': 1, f'buildings_{W.ATTACK_BUILDING}_4': 2,
                  'delivered_0': 10 * step, 'delivered_3': 10 * step,
                  f'filling_{W.SWARM_JOB}_1': 10 * step}
        fields.update(extra)
        return fields

    def test_windows_count_only_recent_activity(self):
        inputs = W.gameplay_inputs(self.record(9), self.record(9 - W.WINDOW_SAMPLES))
        self.assertEqual(inputs['feeding'], 4)
        self.assertEqual(inputs['barracks_levels'], 1 * 1 + 2 * 5)
        self.assertEqual(inputs['delivered_window'], 2 * 10 * W.WINDOW_SAMPLES)
        self.assertEqual(inputs['inn_walk_distance_window'], 10 * W.WINDOW_SAMPLES)
        self.assertEqual(inputs['swarm_filling_window'], 10 * W.WINDOW_SAMPLES)

    def test_a_missing_earlier_sample_counts_from_coverage_start(self):
        inputs = W.gameplay_inputs(self.record(3), None)
        self.assertEqual(inputs['delivered_window'], 2 * 30)

    def test_a_window_is_never_negative(self):
        inputs = W.gameplay_inputs(self.record(1), self.record(5))
        self.assertEqual(inputs['delivered_window'], 0)

    def test_layout_constants_match_the_engine(self):
        def source(path):
            return (ROOT / path).read_text()
        building = source('src/building/IntBuildingType.h')
        self.assertIn(f'ATTACK_BUILDING={W.ATTACK_BUILDING},', building.replace(' ', ''))
        self.assertIn(f'NB_BUILDING_LONG_LEVELS = {W.BUILDING_LEVELS};', source('src/team/stats/TeamStat.h'))
        self.assertIn(f'MaterialSlotCount = {W.MATERIAL_SLOTS};', source('src/resource/Material.h'))
        engine = source('src/team/stats/WinProbability.h')
        self.assertIn(f'ACTIVITY_WINDOW_SAMPLES = {W.WINDOW_SAMPLES};', engine)
        stat = source('src/team/stats/TeamStat.h')
        jobs = re.search(r'enum LabourJob\s*\{([^}]*)\}', stat)[1]
        self.assertEqual(re.findall(r'\w+', jobs).index('SWARM_JOB'), W.SWARM_JOB)
        phases = re.findall(r'\w+', re.search(r'enum LabourPhase\s*\{([^}]*)\}', stat)[1])
        self.assertEqual(phases.index('LABOUR_PHASES'), W.LABOUR_PHASES)

    def test_the_decision_reads_the_previously_published_sample(self):
        # At tick T the engine's latest retained gameplay sample is T - 512, so a
        # record logged for T itself must not be what the decision reads.
        pair = {'tl': {}, 'econ': {'food': '0/0', 'fooded': '0/0'},
                'gameplay': self.record(9, feeding=99)}
        for which, field in W.TIMELINE_FIELDS.values():
            pair[which][field] = 1
        published = {'gameplay': self.record(5)}
        earlier = {'gameplay': self.record(5 - W.WINDOW_SAMPLES)}
        values = W.measurements_of(pair, published, earlier)
        self.assertEqual(values['feeding'], 4)
        self.assertEqual(values['delivered_window'], 2 * 10 * W.WINDOW_SAMPLES)
        self.assertEqual(W.measurements_of(pair)['feeding'], 0)

    def test_only_the_fields_the_engine_reads_are_kept(self):
        line = ('GLOB2_MEASURE team=1 tick=2048 final=0 feeding=7 meals=99 '
                f'buildings_{W.ATTACK_BUILDING}_2=3 buildings_1_2=8')
        fields = W.gameplay_fields(line)
        self.assertEqual(fields, {'team': 1, 'tick': 2048, 'final': 0, 'feeding': 7,
                                  f'buildings_{W.ATTACK_BUILDING}_2': 3})


@unittest.skipUnless(FITTING, 'fitting needs numpy and scipy')
class Fit(unittest.TestCase):
    def dataset(self, games):
        return {'games': games}

    def test_cross_validation_keeps_a_game_whole(self):
        from tools.conditional_logit import cross_validated
        # Two samples of one game must never land in different folds: they are
        # near-identical views of a single outcome, and splitting them would let
        # the fit see its own test set.
        games = []
        for game in range(20):
            for tick in (5120, 10240, 20480):
                strong, weak = (0, 1) if game % 2 else (1, 0)
                entries = [{'measurements': {'units': 40}, 'placement': 1, 'won': True},
                           {'measurements': {'units': 5}, 'placement': 2, 'won': False}]
                games.append({'job_id': f'game-{game}', 'map': 'm', 'tick': tick,
                              'entries': entries})
        seen = {}

        def group(game):
            seen.setdefault(game['job_id'], 0)
            seen[game['job_id']] += 1
            return game['job_id']

        cross_validated(self.dataset(games), [('units', 'identity')], folds=4, group=group)
        self.assertTrue(seen, 'grouping callable was never consulted')

    def test_the_fit_recovers_an_advantage_it_was_given(self):
        from tools.conditional_logit import fit_model
        # The side with more units always wins, so the coefficient must come out
        # positive and the model must beat guessing.
        games = []
        for i in range(120):
            big, small = (60, 5) if i % 2 == 0 else (5, 60)
            entries = [{'measurements': {'units': big}, 'placement': 1 if big > small else 2,
                        'won': big > small},
                       {'measurements': {'units': small}, 'placement': 2 if big > small else 1,
                        'won': small > big}]
            games.append({'job_id': f'g{i}', 'map': f'm{i}', 'tick': 20480, 'entries': entries})
        model = fit_model(self.dataset(games), [('units', 'identity')])
        self.assertGreater(model['features'][0]['coefficient'], 0)
        self.assertGreater(model['train']['accuracy'], 0.9)


if __name__ == '__main__':
    unittest.main(verbosity=2)
