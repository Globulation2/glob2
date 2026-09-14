#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline checks for tools/map_fairness_tournament.py: adjudication, parsing and statistics.

Needs no build and no games: every case is synthetic engine output or a known statistical value.

  python3 test/test_map_fairness_tournament.py
"""
import json
import random
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import map_fairness_tournament as t

RECORD = {'colonies': 4, 'starts': [[0, 0], [1, 1], [2, 2], [3, 3]]}
CONFIG = {'tick_cap': 90000}


def team(result, alive, elim, slot, prestige=0, units=0, buildings=0):
    return {'result': result, 'alive': alive, 'eliminated_tick': elim, 'start': RECORD['starts'][slot],
            'prestige': prestige, 'units': units, 'workers': units, 'explorers': 0, 'warriors': 0,
            'buildings': buildings, 'sites': 0}


def game(rotation, end, teams, reason=None):
    # Through JSON, as a result read back from disk: integer keys become strings.
    return json.loads(json.dumps({'rotation': rotation, 'status': 'ok',
                                  'parsed': {'end': end, 'teams': teams, 'reason': reason,
                                             'cap_checksum': None}}))


def ended(ticks, winner):
    return {'ticks': ticks, 'winner_team': winner, 'seed': 1, 'map': 'x', 'orders': 1}


def check_adjudication():
    slot = lambda team_index, r: (team_index - r) % 4
    # Decisive win on rotation 1: team 2 holds start 1. Eliminations at 100, 300 and 200 ticks.
    a = game(1, ended(4000, 2), {
        0: team('lost', 0, 100, slot(0, 1)), 1: team('lost', 0, 300, slot(1, 1)),
        2: team('won', 1, -1, slot(2, 1), prestige=50), 3: team('lost', 0, 200, slot(3, 1))}, 'elimination')
    o = t.adjudicate(a, RECORD, CONFIG)
    assert (o['winner_slot'], o['winner_team'], o['adjudication']) == (1, 2, 'decisive'), o
    assert o['elimination_order'] == [[3, 100], [2, 200], [0, 300]], o['elimination_order']
    assert o['placement'] == {1: 1, 0: 2, 2: 3, 3: 4}, o['placement']
    assert not o['start_mismatch'] and o['end_reason'] == 'elimination'
    # Tick cap, population decides between two colonies level on prestige.
    b = game(1, ended(90000, -1), {
        0: team('undecided', 1, -1, slot(0, 1), prestige=10, units=5, buildings=9),
        1: team('undecided', 1, -1, slot(1, 1), prestige=12, units=20, buildings=3),
        2: team('lost', 0, 500, slot(2, 1)),
        3: team('undecided', 1, -1, slot(3, 1), prestige=12, units=25, buildings=5)})
    o = t.adjudicate(b, RECORD, CONFIG)
    assert (o['end_reason'], o['adjudication']) == ('cap', 'cap-units'), o
    assert (o['winner_team'], o['winner_slot']) == (3, 2), o
    # Team 1 (start 0) outranks team 0 (start 3) on prestige, so it places second.
    assert o['placement'] == {2: 1, 0: 2, 3: 3, 1: 4}, o['placement']
    # Tick cap, buildings decide when prestige and population are level.
    c = game(0, ended(90000, -1), {
        0: team('undecided', 1, -1, 0, prestige=5, units=10, buildings=7),
        1: team('undecided', 1, -1, 1, prestige=5, units=10, buildings=4),
        2: team('undecided', 1, -1, 2, prestige=5, units=9, buildings=9),
        3: team('undecided', 1, -1, 3, prestige=4, units=30, buildings=30)})
    o = t.adjudicate(c, RECORD, CONFIG)
    assert (o['adjudication'], o['winner_slot']) == ('cap-buildings', 0), o
    # An exact tie at the top stays unresolved, and placements still cover every start.
    d = game(0, ended(90000, -1), {
        0: team('undecided', 1, -1, 0, prestige=7, units=4, buildings=2),
        1: team('undecided', 1, -1, 1, prestige=7, units=4, buildings=2),
        2: team('lost', 0, 900, 2), 3: team('lost', 0, 800, 3)})
    o = t.adjudicate(d, RECORD, CONFIG)
    assert o['winner_slot'] is None and o['adjudication'] == 'cap-tie', o
    assert sorted(o['placement']) == [0, 1, 2, 3] and o['placement'][2] == 3, o['placement']
    # A team standing somewhere other than its rotated start is flagged.
    e = json.loads(json.dumps(a))
    e['parsed']['teams']['0']['start'] = [9, 9]
    assert t.adjudicate(e, RECORD, CONFIG)['start_mismatch']
    print('adjudication checks passed')


def check_parsing():
    text = '\n'.join([
        'nox::game started', 'nox::gui.game.totalPrestigeReached',
        'GLOB2_GAME_END ticks=31234 winner_team=1 seed=5 map="fairness-g9-s1001-r2" orders=812 '
        'players=team0:local,team1:Nicowar',
        'GLOB2_TEAM_RESULT team=0 result=lost alive=1 eliminated_tick=-1 start=84,103 prestige=40 units=20 '
        'workers=15 explorers=2 warriors=3 buildings=12 sites=1',
        'GLOB2_TEAM_RESULT team=1 result=won alive=1 eliminated_tick=-1 start=21,84 prestige=90 units=40 '
        'workers=30 explorers=4 warriors=6 buildings=25 sites=2'])
    p = t.parse_game_log(text)
    assert p['end'] == {'ticks': 31234, 'winner_team': 1, 'seed': 5,
                        'map': 'fairness-g9-s1001-r2', 'orders': 812}, p['end']
    assert p['reason'] == 'prestige' and p['teams'][1]['start'] == [21, 84]
    assert p['teams'][0]['buildings'] == 12 and p['teams'][1]['result'] == 'won'
    study = t.parse_study('\n'.join([
        'SAMPLED,1001,42,5', 'ROTATIONS,4,4,0,1,1,1,1', 'START,0,7,9',
        'MAPFILE,0,/tmp/map-r0.map,1024,123', 'OPTION,width,7',
        'COLONY,0,' + ','.join(['1'] * 15)]))
    assert study['rotation_checks'] == {'teams': 4, 'rotations': 4, 'save_load_stable': 0,
                                       'references_consistent': 1, 'colonies_placed': 1,
                                       'round_trip': 1, 'reload_idempotent': 1}, study['rotation_checks']
    assert study['chosen_seed'] == 42 and study['starts'] == [[7, 9]]
    assert len(study['colony_quality']) == 1 and study['colony_quality'][0]['total'] == 1.0
    assert t.parse_int_list('1001-1003,1007') == [1001, 1002, 1003, 1007]
    print('parsing checks passed')


def check_statistics():
    # Chi-square tail against textbook critical values, and the Wilson interval.
    assert abs(t.gamma_q(1.5, 7.8147 / 2) - 0.05) < 1e-4 and abs(t.gamma_q(1.5, 11.345 / 2) - 0.01) < 1e-4
    assert abs(t.gamma_q(0.5, 3.8415 / 2) - 0.05) < 1e-4 and abs(t.gamma_q(3.5, 14.067 / 2) - 0.05) < 1e-4
    assert [round(v, 4) for v in t.wilson(5, 10)] == [0.2366, 0.7634]
    # Exact multinomial test: every game won by one start out of four is (1/4)^3.
    assert abs(t.exact_uniform_p([4, 0, 0, 0], 1)[0] - 0.015625) < 1e-9
    assert t.exact_uniform_p([1, 1, 1, 1], 1)[0] > 0.999
    assert t.exact_uniform_p([20, 0, 0, 0, 0, 0, 0, 12], 3)[1] == 'monte-carlo'
    assert [round(v, 3) for v in t.benjamini_hochberg([0.01, 0.04, 0.03, 0.2])] == [0.04, 0.053, 0.053, 0.2]
    assert [round(v, 3) for v in t.holm([0.01, 0.04, 0.03, 0.2])] == [0.04, 0.09, 0.09, 0.2]
    assert t.spearman([1, 2, 3, 4], [10, 20, 30, 40]) == 1.0 and t.spearman([1, 2, 3, 4], [4, 3, 2, 1]) == -1.0
    # Student t quantiles: tabulated to 30 degrees of freedom, approximated past it (t(120)=1.9799).
    assert t.t975(2) == 4.303 and t.t975(30) == 2.042 and abs(t.t975(120) - 1.9799) < 0.005
    # The bias estimate is unbiased: zero for fair maps, the true value for a skewed one.
    rng = random.Random(5)
    for probabilities, expected in (([0.25] * 4, 0.0), ([0.4, 0.2, 0.2, 0.2], 0.03)):
        estimates = []
        for _ in range(4000):
            counts = [0] * 4
            for _ in range(16):
                value, total = rng.random(), 0.0
                for i, p in enumerate(probabilities):
                    total += p
                    if value < total:
                        counts[i] += 1
                        break
                else:
                    counts[-1] += 1
            estimates.append(t.squared_bias(counts))
        assert abs(statistics.mean(estimates) - expected) < 0.01, (probabilities, statistics.mean(estimates))
    # A t interval over maps covers zero when the per-map estimates are noisy, and the fair-map
    # floor is deterministic and sits where a small sample's noise does.
    low, high = t.bias_interval([0.1429, 0.0353, 0.0353], 4)
    assert low == 0.0 and 20 < high < 27, (low, high)
    floor = t.fair_map_floor([8, 8, 8], 4)
    assert 11 < floor < 18 and t.fair_map_floor([8, 8, 8], 4) == floor, floor
    assert t.fair_map_floor([48] * 12, 4) < floor, 'more games per map must lower the floor'
    # A map where one start wins nearly everything is flagged, with dominance 4x its fair share.
    table = t.share_table([12, 2, 1, 1], 1)
    assert table['best'] == 0 and table['p'] < 0.001 and table['rms_points'] > 20, table
    assert t.share_table([4, 0, 0, 0], 1)['dominance'] == 4.0
    print('statistics checks passed')


def check_scorer():
    # Quality matching wins exactly on three maps: the permutation test should notice.
    points = [(f'm{m}', q, q / 10, q / 10) for m in range(3) for q in (-1.5, -0.5, 0.5, 1.5)]
    rho = t.spearman([p[1] for p in points], [p[2] for p in points])
    assert rho == 1.0
    assert t.scorer_permutation_p(points, rho, 2, 500, 1) < 0.02
    # Quality unrelated to wins: no signal.
    shuffled = [(f'm{m}', q, w, w) for m in range(3)
                for q, w in zip((-1.5, -0.5, 0.5, 1.5), (0.1, -0.1, 0.15, -0.15))]
    rho = t.spearman([p[1] for p in shuffled], [p[2] for p in shuffled])
    assert t.scorer_permutation_p(shuffled, rho, 2, 500, 1) > 0.1
    print('scorer checks passed')


if __name__ == '__main__':
    check_adjudication()
    check_parsing()
    check_statistics()
    check_scorer()
    print('all harness checks passed')
