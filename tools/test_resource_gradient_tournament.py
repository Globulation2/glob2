# SPDX-License-Identifier: GPL-3.0-or-later
import copy
from collections import Counter
import unittest

from tools import resource_gradient_model as model
from tools.resource_gradient_tournament import FORMATS, sample_plan, map_identity


def catalog():
    shared = [dict(id='width', values=[6, 7, 8, 9], powerOfTwo=True),
              dict(id='height', values=[6, 7, 8, 9], powerOfTwo=True),
              dict(id='teams', values=list(range(1, 17))),
              dict(id='workers', values=[2, 3, 4, 5], searchValues=[3, 4]),
              dict(id='repeat', values=[0, 1, 2], searchValues=[1, 2], powerOfTwo=True),
              dict(id='resource-amount', values=[0, 50, 100, 150], searchValues=[50, 100, 150])]
    return dict(schema_version=1, ais=[dict(name='nicowar'), dict(name='cortex'), dict(name='maxima')],
                generators=[dict(id=f'generator-{i}', method=i, editorOnly=i == 9, controls=copy.deepcopy(shared))
                            for i in range(10)])


class ResourceGradientTournamentTest(unittest.TestCase):
    def test_reproducible_balanced_diverse_catalog_driven_plan(self):
        c = catalog(); p = sample_plan(c); games = p['games']
        self.assertEqual(p, sample_plan(c))
        shuffled = copy.deepcopy(c)
        shuffled['generators'].reverse(); shuffled['ais'].reverse()
        self.assertEqual(games, sample_plan(shuffled)['games'])
        self.assertNotEqual(games, sample_plan(c, seed=5102)['games'])
        self.assertEqual(Counter(s['format'] for s in games), dict.fromkeys(FORMATS, 32))
        counts = Counter(s['generator'] for s in games)
        self.assertEqual(len(counts), 9)
        self.assertLessEqual(max(counts.values())-min(counts.values()), 1)
        self.assertTrue(any(s['width'] != s['height'] for s in games))
        self.assertEqual({v for s in games for v in [s['width'], s['height']]}, {64, 128, 256, 512})
        self.assertTrue(any(len(set(s['ais'])) > 1 for s in games))
        self.assertTrue(any(len(set(s['ais'])) == 1 for s in games))
        self.assertEqual(set(p['coverage']['ai_seats']), {'nicowar', 'cortex', 'maxima'})
        model.validate_specs(games)
        for s in games:
            self.assertEqual(sorted(Counter(s['alliances']).values()), sorted(Counter(FORMATS[s['format']]).values()))
            self.assertLessEqual(max(s['width'], s['height']), 2*min(s['width'], s['height']))
            self.assertTrue(1024 <= s['width']*s['height']/s['players'] <= 32768)
            self.assertIn(s['generator_params']['repeat'], [2, 4])
            self.assertIn(s['generator_params']['workers'], [3, 4])
            self.assertIn(s['generator_params']['resource-amount'], [50, 100, 150])
            self.assertNotEqual(s['map_seed'], s['game_seed'])

    def test_generator_width_and_team_restrictions_are_respected(self):
        c = catalog()
        g = c['generators'][0]
        next(v for v in g['controls'] if v['id']=='width')['values'] = [6]
        next(v for v in g['controls'] if v['id']=='teams')['values'] = [2]
        p = sample_plan(c, games=128)
        selected = [s for s in p['games'] if s['generator']==g['id']]
        self.assertTrue(selected)
        self.assertTrue(all(s['format']=='1v1' and s['width']==64 for s in selected))

    def test_bounded_budget_and_pool_validation(self):
        c = catalog()
        for kwargs in [dict(games=0), dict(games=7), dict(generators=['generator-9']),
                       dict(ais=[]), dict(formats=['bad']), dict(sizes=[100]),
                       dict(sizes=[64], formats=['16ffa']), dict(seed=-1)]:
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError): sample_plan(c, **kwargs)
        p = sample_plan(c, games=5, formats=['1v1'], generators=['generator-1'], sizes=[64], ais=['nicowar'])
        self.assertEqual(len(p['games']), 5)
        self.assertEqual(set(p['coverage']['generators']), {'generator-1'})
        bad = catalog(); bad['generators'][0]['controls'][-1]['searchValues'] = [999]
        with self.assertRaises(ValueError): sample_plan(bad, generators=['generator-0'])

    def test_map_split_identity_keeps_rosters_alliances_and_game_seeds_together(self):
        spec = sample_plan(catalog())['games'][0]
        changed = copy.deepcopy(spec)
        changed['ais'].reverse(); changed['game_seed'] += 1; changed['alliances'].reverse()
        self.assertEqual(map_identity(spec), map_identity(changed))
        changed['map_seed'] += 1
        self.assertNotEqual(map_identity(spec), map_identity(changed))
        changed = copy.deepcopy(spec); changed['generator_params']['workers'] += 1
        self.assertNotEqual(map_identity(spec), map_identity(changed))

    def test_explicit_plan_cannot_hide_identity_overrides_or_bad_rosters(self):
        spec = sample_plan(catalog())['games'][0]
        for key, value in [('ais', []), ('alliances', [1]), ('map_seed', -1),
                           ('width', 100), ('generator_params', {'seed': 1})]:
            broken = dict(spec, **{key:value})
            with self.subTest(key=key), self.assertRaises(ValueError): model.validate_specs([broken])
        with self.assertRaises(ValueError): model.validate_specs([spec, spec])


if __name__ == '__main__': unittest.main()
