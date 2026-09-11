#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Map fairness tournament for Globulation 2's random map generators.

Asks whether generated maps are fair in real games, not only by the generator's own
start-quality score. For every generator and map seed the tournament:

1. generates one playable map the way the custom-game lobby does (the best-scoring of five
   rolls derived from the map seed) and writes one copy per cyclic rotation, in which the
   generator's colony t plays as team (t + r) mod N (MapGeneratorStudy save= rotations=);
2. plays free-for-all games with the same AI in every slot on every rotation, differing only in
   the engine seed (GLOB2_TEST_SEED), headlessly through glob2 -test-games-nox;
3. tallies wins by start position and, separately, by team index. Every team index plays every
   start equally often, so a start that keeps winning is the map's doing and a team index that
   keeps winning is the engine's own processing-order bias.

See docs/map-generators/FAIRNESS_TOURNAMENT.md for the metrics and how to read the report.

  python3 tools/map_fairness_tournament.py run smoke
  python3 tools/map_fairness_tournament.py run standard --jobs 3
  python3 tools/map_fairness_tournament.py summarize artifacts/map-fairness/smoke
"""
import argparse
import concurrent.futures
import csv
import datetime
import json
import math
import os
import random
import re
import shutil
import statistics
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PRESET_DIR = ROOT / 'tools' / 'map-fairness'
BASELINE_METHOD = 15  # Symmetric arena: exactly symmetric starts, the fairness baseline.
Z95 = 1.959963984540054
FACTORS = ['wheat', 'wood', 'fertility', 'depth', 'room', 'isolation']
COLONY_FIELDS = ['wheat_distance', 'wood_distance', 'catchment_tiles', 'build_sites',
                 'resource_amount', 'rival_distance', 'rivals_within_threat', 'mean_fertility',
                 *FACTORS, 'total']
RUN_FILES = ['config.json', 'summary.json', 'summary.md', 'games.csv', 'colonies.csv', 'maps.csv',
             'verification.json']
RUN_DIRECTORIES = ['maps', 'games', 'profiles', 'verify']

DEFAULTS = {
    'name': 'custom',
    'description': '',
    'generators': [BASELINE_METHOD],
    'map_seeds': [1001],
    'games_per_rotation': 1,
    'engine_seed_base': 1,
    'rotations': 'all',
    'size': 128,
    'colonies': 4,
    'workers': None,
    'controls': {},
    'candidates': 5,
    'ai': 'nicowar',
    'tick_cap': 90000,
    'jobs': 3,
    'game_timeout_seconds': 3600,
    'keep_replays': False,
    'timeline': False,
    'verify_games': 0,
    'bootstrap_draws': 4000,
}


def remove_tree(path, inside):
    """Delete a directory, but only one strictly inside `inside` (the run directory)."""
    target, root = Path(path).resolve(), Path(inside).resolve()
    if target == root or not target.is_relative_to(root):
        raise RuntimeError(f'refusing to delete {target}: not inside {root}')
    shutil.rmtree(target, ignore_errors=True)


# ---------------------------------------------------------------------------- configuration

def parse_int_list(spec):
    """'1001-1004', '1001,1005', a JSON list or {"start", "count"} as a list of ints."""
    if isinstance(spec, dict):
        start = int(spec['start'])
        return list(range(start, start + int(spec['count'])))
    if isinstance(spec, (list, tuple)):
        return [int(s) for s in spec]
    values = []
    for part in str(spec).split(','):
        part = part.strip()
        if not part:
            continue
        if '-' in part:
            first, last = part.split('-', 1)
            values.extend(range(int(first), int(last) + 1))
        else:
            values.append(int(part))
    return values


def load_config(preset, overrides):
    path = Path(preset)
    if path.suffix != '.json':
        path = PRESET_DIR / f'{preset}.json'
    if not path.exists():
        raise SystemExit(f'error: preset not found: {path}')
    config = dict(DEFAULTS)
    config.update(json.loads(path.read_text()))
    for key, value in overrides.items():
        if value is not None:
            config[key] = value
    resolved = path.resolve()
    config['preset_file'] = str(resolved.relative_to(ROOT)) if resolved.is_relative_to(ROOT) else str(resolved)
    config['map_seeds'] = parse_int_list(config['map_seeds'])
    generators = config['generators']
    if isinstance(generators, str):
        generators = parse_int_list(generators)
    config['generators'] = [g if isinstance(g, dict) else {'method': int(g)} for g in generators]
    size = config['size']
    width, height = (size, size) if isinstance(size, int) else (int(size[0]), int(size[1]))
    for side in (width, height):
        if side < 16 or side & (side - 1):
            raise SystemExit(f'error: map sides must be powers of two >= 16 (got {width}x{height})')
    config['width'], config['height'] = width, height
    colonies = int(config['colonies'])
    rotations = colonies if config['rotations'] == 'all' else int(config['rotations'])
    if not 1 <= rotations <= colonies:
        raise SystemExit(f'error: rotations must be "all" or 1..{colonies}')
    config['rotation_count'] = rotations
    return config


def generator_controls(config, generator):
    controls = dict(config.get('controls', {}).get(str(generator['method']), {}))
    controls.update(generator.get('controls', {}))
    return controls


def git_revision():
    def git(*args):
        return subprocess.run(['git', *args], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    revision = git('rev-parse', 'HEAD')
    dirty = bool(git('status', '--porcelain', '--untracked-files=no'))
    return revision + ('-dirty' if dirty else '')


# ---------------------------------------------------------------------------- map production

def map_key(method, seed):
    return f'g{method}-s{seed}'


def parse_study(stdout):
    record = {'colonies': [], 'starts': [], 'files': [], 'options': {}}
    for line in stdout.splitlines():
        parts = line.strip().split(',')
        tag = parts[0]
        if tag == 'STUDY':
            record['generated'] = parts[3] == '1'
            record['generation_seconds'] = float(parts[14])
        elif tag == 'SAMPLED':
            record['root_seed'], record['chosen_seed'], record['candidates'] = map(int, parts[1:4])
        elif tag == 'QUALITY':
            record['quality'] = {'measured': parts[1] == '1', 'score': float(parts[2]),
                                 'fairness': float(parts[3]), 'worst': float(parts[4]),
                                 'best': float(parts[5])}
        elif tag == 'COLONY':
            record['colonies'].append(dict(zip(COLONY_FIELDS, (float(v) for v in parts[2:]))))
        elif tag == 'REQUEST':
            record['request'] = {'generator_id': parts[1], 'revision': int(parts[2]),
                                 'seed': int(parts[3]), 'wDec': int(parts[4]), 'hDec': int(parts[5]),
                                 'teams': int(parts[6]), 'workers': int(parts[7])}
        elif tag == 'OPTION':
            record['options'][parts[1]] = int(parts[2])
        elif tag == 'START':
            record['starts'].append([int(parts[2]), int(parts[3])])
        elif tag == 'MAPFILE':
            record['files'].append({'rotation': int(parts[1]), 'path': ','.join(parts[2:-2]),
                                    'bytes': int(parts[-2]), 'fnv1a64': parts[-1]})
        elif tag == 'ROTATIONS':
            record['rotation_checks'] = dict(zip(
                ['teams', 'rotations', 'save_load_stable', 'references_consistent',
                 'colonies_placed', 'round_trip'], map(int, parts[1:7])))
    return record


def produce_map(config, paths, generator, seed):
    method = generator['method']
    key = map_key(method, seed)
    directory = paths['maps'] / key
    manifest = directory / 'map.json'
    if manifest.exists():
        record = json.loads(manifest.read_text())
        if record.get('ok') or record.get('failure') == 'generation':
            return record
    remove_tree(directory, paths['out'])
    directory.mkdir(parents=True)
    profile = paths['profiles'] / f'study-{key}'
    remove_tree(profile, paths['out'])
    profile.mkdir(parents=True)
    colonies = int(config['colonies'])
    command = [str(paths['study']), str(method), str(seed), 'glob2-fairness',
               f'w={int(math.log2(config["width"]))}', f'h={int(math.log2(config["height"]))}',
               f'teams={colonies}', 'quality', f'candidates={int(config["candidates"])}',
               f'save={directory / "map"}', f'rotations={config["rotation_count"]}',
               f'name=fairness-{key}']
    if config.get('workers'):
        command.append(f'workers={int(config["workers"])}')
    controls = generator_controls(config, generator)
    command += [f'{k}={v}' for k, v in controls.items()]
    env = dict(os.environ, GLOB2_USER_DIR=str(profile))
    begun = time.monotonic()
    try:
        result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=1800)
        stdout, exit_code = result.stdout, result.returncode
        (directory / 'study.log').write_text(result.stdout + result.stderr)
    except subprocess.TimeoutExpired:
        stdout, exit_code = '', 'timeout'
    remove_tree(profile, paths['out'])
    record = parse_study(stdout)
    record.update({
        'key': key, 'method': method, 'map_seed': seed, 'colonies': colonies,
        'width': config['width'], 'height': config['height'], 'controls': controls,
        'study_command': [str(Path(c).relative_to(ROOT)) if c.startswith(str(ROOT)) else c
                          for c in command],
        'exit_code': exit_code, 'wall_seconds': round(time.monotonic() - begun, 3),
        'git_revision': paths['git_revision'],
    })
    record.setdefault('chosen_seed', seed)
    checks = record.get('rotation_checks', {})
    if exit_code == 4 or (exit_code == 0 and not record.get('generated', False)):
        record.update(ok=False, failure='generation')
    elif exit_code != 0 or not checks or not all(
            checks[k] for k in ('references_consistent', 'colonies_placed', 'round_trip')):
        record.update(ok=False, failure=f'study exit {exit_code}')
    elif len(record['starts']) != colonies or len(record['files']) != config['rotation_count']:
        record.update(ok=False, failure='incomplete study output')
    else:
        record['ok'] = True
    manifest.write_text(json.dumps(record, indent=2))
    return record


# ---------------------------------------------------------------------------- matches

END_RE = re.compile(r'^GLOB2_GAME_END ticks=(\d+) winner_team=(-?\d+) seed=(\d+) '
                    r'map="([^"]*)" orders=(\d+)')


def parse_game_log(text):
    parsed = {'end': None, 'teams': {}, 'reason': None, 'cap_checksum': None}
    for line in text.splitlines():
        if line.startswith('GLOB2_GAME_END'):
            match = END_RE.match(line)
            if match:
                parsed['end'] = {'ticks': int(match[1]), 'winner_team': int(match[2]),
                                 'seed': int(match[3]), 'map': match[4], 'orders': int(match[5])}
        elif line.startswith('GLOB2_TEAM_RESULT '):
            fields = dict(item.split('=', 1) for item in line.split()[1:])
            team = int(fields.pop('team'))
            start = [int(v) for v in fields.pop('start').split(',')]
            entry = {k: (v if k == 'result' else int(v)) for k, v in fields.items()}
            entry['start'] = start
            parsed['teams'][team] = entry
        elif line.startswith('nox::gui.game.totalPrestigeReached'):
            parsed['reason'] = parsed['reason'] or 'prestige'
        elif line.startswith('nox::gui.game.isGameEnded'):
            parsed['reason'] = parsed['reason'] or 'elimination'
        elif line.startswith('nox::gui.game.checkSum() = '):
            parsed['cap_checksum'] = line.split('=', 1)[1].strip()
    return parsed


def adjudicate(game, record, config):
    """Winner, placement and elimination order by start position, from a parsed game."""
    n = record['colonies']
    rotation = game['rotation']
    teams = {int(t): e for t, e in game['parsed']['teams'].items()}
    end = game['parsed']['end']
    slot_of = lambda team: (team - rotation) % n
    outcome = {'status': game['status'], 'end_reason': None, 'ticks': None, 'winner_team': None,
               'winner_slot': None, 'adjudication': 'none', 'start_mismatch': False,
               'elimination_order': [], 'placement': {}}
    if game['status'] != 'ok' or end is None or len(teams) != n:
        outcome['status'] = game['status'] if game['status'] != 'ok' else 'incomplete'
        return outcome
    outcome['ticks'] = end['ticks']
    for team, entry in teams.items():
        if entry['start'] != record['starts'][slot_of(team)]:
            outcome['start_mismatch'] = True
    cap = int(config['tick_cap'])
    winners = [t for t, e in teams.items() if e['result'] == 'won']
    alive = [t for t, e in teams.items() if e['alive']]
    strength = lambda t: (teams[t]['prestige'], teams[t]['buildings'], teams[t]['units'])
    if end['winner_team'] >= 0:
        outcome['end_reason'] = game['parsed']['reason'] or 'elimination'
        winner = end['winner_team']
        outcome['adjudication'] = 'decisive' if len(winners) == 1 else 'decisive-shared'
    elif end['ticks'] >= cap:
        outcome['end_reason'] = 'cap'
        ranked = sorted(alive, key=strength, reverse=True)
        if ranked and (len(ranked) == 1 or strength(ranked[0]) != strength(ranked[1])):
            winner = ranked[0]
            outcome['adjudication'] = 'cap-prestige'
        else:
            winner = None
            outcome['adjudication'] = 'cap-tie' if ranked else 'cap-none-alive'
    else:
        outcome['end_reason'] = game['parsed']['reason'] or 'no-winner'
        winner = None
        outcome['adjudication'] = 'no-winner'
    if winner is not None:
        outcome['winner_team'] = winner
        outcome['winner_slot'] = slot_of(winner)
    eliminated = sorted((e['eliminated_tick'], t) for t, e in teams.items() if e['eliminated_tick'] >= 0)
    outcome['elimination_order'] = [[slot_of(t), tick] for tick, t in eliminated]
    # Placement 1 is the winner; other survivors by prestige, then eliminated colonies, the
    # later-eliminated ahead. Ties break towards the lower start index, only to stay deterministic.
    survivors = sorted((t for t in teams if t != winner and teams[t]['eliminated_tick'] < 0),
                       key=lambda t: (tuple(-v for v in strength(t)), slot_of(t)))
    fallen = [t for _, t in sorted(((-e['eliminated_tick'], slot_of(t)), t)
                                   for t, e in teams.items() if t != winner and e['eliminated_tick'] >= 0)]
    order = ([winner] if winner is not None else []) + survivors + fallen
    outcome['placement'] = {slot_of(t): place + 1 for place, t in enumerate(order)}
    return outcome


def game_jobs(config, records):
    jobs = []
    per_rotation = int(config['games_per_rotation'])
    for k in range(per_rotation):
        for rotation in range(config['rotation_count']):
            for record in records:
                if record.get('ok'):
                    seed = int(config['engine_seed_base']) + rotation * per_rotation + k
                    jobs.append({'map': record['key'], 'rotation': rotation, 'game': k, 'seed': seed})
    return jobs


def run_game(config, paths, record, job, directory=None, force=False):
    directory = directory or paths['games'] / record['key'] / f'r{job["rotation"]}-k{job["game"]}'
    result_file = directory / 'result.json'
    if result_file.exists() and not force:
        return json.loads(result_file.read_text())
    remove_tree(directory, paths['out'])
    profile = directory / 'profile'
    (profile / 'maps').mkdir(parents=True)
    map_file = next(f for f in record['files'] if f['rotation'] == job['rotation'])
    map_name = f'fairness-{record["key"]}-r{job["rotation"]}'
    os.symlink(Path(map_file['path']).resolve(), profile / 'maps' / f'{map_name}.map')
    env = dict(os.environ)
    env.update(GLOB2_USER_DIR=str(profile), GLOB2_TEST_SEED=str(job['seed']),
               GLOB2_TEAM_RESULTS='1', GLOB2_TEST_MAX_TICKS=str(int(config['tick_cap'])),
               GLOB2_REPLAY_PATH=str(directory / 'game.replay'))
    if config.get('timeline'):
        env['GLOB2_TEAM_TIMELINE'] = '1'
    command = [str(paths['glob2']), '-test-games-nox', '1', '--map', map_name,
               '--matchup', ','.join([config['ai']] * record['colonies'])]
    log = directory / 'game.log'
    begun = time.monotonic()
    with open(log, 'wb') as out:
        try:
            code = subprocess.run(command, cwd=ROOT, env=env, stdout=out, stderr=subprocess.STDOUT,
                                  timeout=float(config['game_timeout_seconds'])).returncode
            status = 'ok' if code == 0 else f'exit_{code}'
        except subprocess.TimeoutExpired:
            status = 'timeout'
    wall = time.monotonic() - begun
    text = log.read_text(errors='replace')
    parsed = parse_game_log(text)
    remove_tree(profile, paths['out'])
    replay = directory / 'game.replay'
    if not config.get('keep_replays') and replay.exists():
        replay.unlink()
    game = {**job, 'status': status, 'wall_seconds': round(wall, 3), 'parsed': parsed,
            'command': command[1:], 'env': {k: env[k] for k in (
                'GLOB2_TEST_SEED', 'GLOB2_TEAM_RESULTS', 'GLOB2_TEST_MAX_TICKS')}}
    game['outcome'] = adjudicate(game, record, config)
    # Keep only what the analysis reads; the full engine output can be large.
    if game['outcome']['status'] == 'ok':
        keep = [l for l in text.splitlines() if l.startswith(('GLOB2_', 'nox::', 'Random Seed'))]
        log.write_text('\n'.join(keep) + '\n')
    # Round-trip through JSON now so a fresh result and one read back from disk compare equal.
    game = json.loads(json.dumps(game))
    result_file.write_text(json.dumps(game, indent=1))
    return game


def run_pool(tasks, jobs, work, describe):
    results = [None] * len(tasks)
    lock = threading.Lock()
    done = 0
    begun = time.monotonic()
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = {pool.submit(work, task): i for i, task in enumerate(tasks)}
        for future in concurrent.futures.as_completed(futures):
            i = futures[future]
            results[i] = future.result()
            with lock:
                done += 1
                elapsed = time.monotonic() - begun
                eta = elapsed / done * (len(tasks) - done)
                print(f'[{done}/{len(tasks)} {elapsed:.0f}s, eta {eta:.0f}s] {describe(tasks[i], results[i])}',
                      flush=True)
    return results


# ---------------------------------------------------------------------------- statistics

def gamma_q(a, x):
    """Regularized upper incomplete gamma Q(a, x)."""
    if x <= 0:
        return 1.0
    log_prefix = -x + a * math.log(x) - math.lgamma(a)
    if x < a + 1:
        term = total = 1.0 / a
        ap = a
        for _ in range(100000):
            ap += 1
            term *= x / ap
            total += term
            if abs(term) < abs(total) * 1e-15:
                break
        return max(0.0, 1.0 - total * math.exp(log_prefix))
    tiny = 1e-300
    b = x + 1 - a
    c = 1 / tiny
    d = 1 / b
    h = d
    for i in range(1, 100000):
        an = -i * (i - a)
        b += 2
        d = an * d + b
        d = d if abs(d) > tiny else tiny
        c = b + an / c
        c = c if abs(c) > tiny else tiny
        d = 1 / d
        h *= d * c
        if abs(d * c - 1) < 1e-15:
            break
    return min(1.0, math.exp(log_prefix) * h)


def chi_square_uniform(counts):
    n, k = sum(counts), len(counts)
    if n == 0 or k < 2:
        return None, None
    expected = n / k
    statistic = sum((c - expected) ** 2 / expected for c in counts)
    return statistic, gamma_q((k - 1) / 2, statistic / 2)


def compositions(n, k):
    if k == 1:
        yield (n,)
        return
    for first in range(n + 1):
        for rest in compositions(n - first, k - 1):
            yield (first,) + rest


def exact_uniform_p(counts, seed, draws=20000):
    """Multinomial goodness of fit against uniform: P(an outcome no more likely than observed).

    Exact by enumeration while the outcome space is small, otherwise a seeded Monte Carlo."""
    n, k = sum(counts), len(counts)
    if n == 0 or k < 2:
        return None, 'none'
    base = math.lgamma(n + 1) - n * math.log(k)
    log_p = lambda xs: base - sum(math.lgamma(x + 1) for x in xs)
    observed = log_p(counts) + 1e-9
    if math.comb(n + k - 1, k - 1) <= 250000:
        return min(1.0, sum(math.exp(log_p(c)) for c in compositions(n, k) if log_p(c) <= observed)), 'exact'
    rng = random.Random(seed)
    hits = 0
    for _ in range(draws):
        xs = [0] * k
        for _ in range(n):
            xs[rng.randrange(k)] += 1
        hits += log_p(xs) <= observed
    return (hits + 1) / (draws + 1), 'monte-carlo'


def wilson(k, n):
    if n == 0:
        return None, None
    p = k / n
    denominator = 1 + Z95 ** 2 / n
    centre = (p + Z95 ** 2 / (2 * n)) / denominator
    half = Z95 * math.sqrt(p * (1 - p) / n + Z95 ** 2 / (4 * n * n)) / denominator
    return max(0.0, centre - half), min(1.0, centre + half)


def benjamini_hochberg(p_values):
    m = len(p_values)
    order = sorted(range(m), key=lambda i: p_values[i])
    adjusted = [1.0] * m
    running = 1.0
    for rank in range(m, 0, -1):
        i = order[rank - 1]
        running = min(running, p_values[i] * m / rank)
        adjusted[i] = running
    return adjusted


def holm(p_values):
    m = len(p_values)
    order = sorted(range(m), key=lambda i: p_values[i])
    adjusted = [1.0] * m
    running = 0.0
    for rank, i in enumerate(order):
        running = max(running, min(1.0, (m - rank) * p_values[i]))
        adjusted[i] = running
    return adjusted


def ranks(values):
    order = sorted(range(len(values)), key=lambda i: values[i])
    result = [0.0] * len(values)
    i = 0
    while i < len(order):
        j = i
        while j + 1 < len(order) and values[order[j + 1]] == values[order[i]]:
            j += 1
        for position in range(i, j + 1):
            result[order[position]] = (i + j) / 2 + 1
        i = j + 1
    return result


def pearson(x, y):
    if len(x) < 3:
        return None
    mx, my = sum(x) / len(x), sum(y) / len(y)
    sxx = sum((a - mx) ** 2 for a in x)
    syy = sum((b - my) ** 2 for b in y)
    if sxx <= 1e-18 or syy <= 1e-18:
        return None
    return sum((a - mx) * (b - my) for a, b in zip(x, y)) / math.sqrt(sxx * syy)


def spearman(x, y):
    return pearson(ranks([round(v, 9) for v in x]), ranks([round(v, 9) for v in y]))


def bootstrap(items, statistic, draws, seed):
    """Percentile 95% interval of statistic over items resampled with replacement."""
    if len(items) < 2:
        return None
    rng = random.Random(seed)
    values = []
    for _ in range(draws):
        value = statistic([items[rng.randrange(len(items))] for _ in items])
        if value is not None:
            values.append(value)
    if len(values) < draws / 2:
        return None
    values.sort()
    return [values[int(0.025 * (len(values) - 1))], values[int(0.975 * (len(values) - 1))]]


def squared_bias(counts):
    """Unbiased estimate of sum_s (p_s - 1/k)^2 from multinomial counts.

    E[chi2] = (k - 1) + (n - 1) k sum_s (p_s - 1/k)^2, so the plain squared deviation of the
    observed shares, which is positive even for a perfectly fair map, is corrected for chance."""
    n, k = sum(counts), len(counts)
    if n < 2 or k < 2:
        return None
    statistic, _ = chi_square_uniform(counts)
    return (statistic - (k - 1)) / ((n - 1) * k)


def rms_points(mean_squared_bias, k):
    """Root-mean-square deviation of per-start win probability from 1/k, in percentage points."""
    if mean_squared_bias is None:
        return None
    return 100 * math.sqrt(max(0.0, mean_squared_bias) / k)


def share_table(counts, seed):
    n, k = sum(counts), len(counts)
    statistic, p_chi2 = chi_square_uniform(counts)
    p_exact, method = exact_uniform_p(counts, seed)
    best = max(range(k), key=lambda i: (counts[i], -i)) if n else None
    table = {'counts': counts, 'n': n, 'chi2': statistic, 'p_chi2': p_chi2, 'p': p_exact,
             'p_method': method, 'best': best, 'best_share': counts[best] / n if n else None,
             'best_share_wilson': list(wilson(counts[best], n)) if n else None,
             'dominance': counts[best] / n * k if n else None,
             'squared_bias': squared_bias(counts)}
    table['rms_points'] = rms_points(table['squared_bias'], k)
    table['shares_wilson'] = [list(wilson(c, n)) if n else None for c in counts]
    return table


def hash_text(text):
    value = 2166136261
    for ch in text.encode():
        value = ((value ^ ch) * 16777619) % (2 ** 32)
    return value


def seed_for(*parts):
    return hash_text('/'.join(str(p) for p in parts))


# ---------------------------------------------------------------------------- analysis

def load_results(out):
    records = {}
    for manifest in sorted((out / 'maps').glob('*/map.json')):
        record = json.loads(manifest.read_text())
        records[record['key']] = record
    games = [json.loads(result.read_text()) for result in sorted((out / 'games').glob('*/*/result.json'))]
    return records, games


def analyse_map(record, games, config):
    n = record['colonies']
    played = [g for g in games if g['outcome']['status'] == 'ok']
    wins = [0] * n
    decisive = [0] * n
    team_wins = [0] * n
    placement_points = [0.0] * n
    for game in played:
        outcome = game['outcome']
        if outcome['winner_slot'] is not None:
            wins[outcome['winner_slot']] += 1
            team_wins[outcome['winner_team']] += 1
            if outcome['adjudication'].startswith('decisive'):
                decisive[outcome['winner_slot']] += 1
        for slot, place in outcome['placement'].items():
            placement_points[int(slot)] += (n - place) / (n - 1) if n > 1 else 1.0
    quality = [c['total'] for c in record['colonies']] if len(record['colonies']) == n else None
    analysis = {
        'key': record['key'], 'method': record['method'], 'map_seed': record['map_seed'],
        'chosen_seed': record.get('chosen_seed'), 'games': len(games), 'played': len(played),
        'errors': len(games) - len(played),
        'cap_games': sum(g['outcome']['end_reason'] == 'cap' for g in played),
        'prestige_games': sum(g['outcome']['end_reason'] == 'prestige' for g in played),
        'unresolved': sum(g['outcome']['winner_slot'] is None for g in played),
        'start_mismatches': sum(g['outcome']['start_mismatch'] for g in played),
        'ticks_median': statistics.median([g['outcome']['ticks'] for g in played]) if played else None,
        'wall_seconds_mean': statistics.mean([g['wall_seconds'] for g in played]) if played else None,
        'slot': share_table(wins, seed_for(record['key'], 'slot')),
        'decisive_slot': share_table(decisive, seed_for(record['key'], 'decisive')),
        'team': share_table(team_wins, seed_for(record['key'], 'team')),
        'placement_score': [p / len(played) if played else None for p in placement_points],
        'quality': quality, 'quality_score': record.get('quality', {}).get('score'),
        'starts': record['starts'],
        'record_colonies': record['colonies'] if len(record['colonies']) == n else [],
    }
    analysis['quality_spearman'] = spearman(quality, wins) if quality and sum(wins) and n >= 3 else None
    return analysis


def scorer_points(maps, factor):
    """(map, within-map centred factor, centred win share, centred placement score) per colony."""
    points = []
    for m in maps:
        wins = m['slot']['counts']
        n_wins = sum(wins)
        if not m['record_colonies'] or n_wins == 0:
            continue
        values = [c[factor] for c in m['record_colonies']]
        if max(values) - min(values) < 1e-9:
            continue
        k = len(values)
        mean_value = sum(values) / k
        mean_place = sum(m['placement_score']) / k
        for slot in range(k):
            points.append((m['key'], values[slot] - mean_value, wins[slot] / n_wins - 1 / k,
                           m['placement_score'][slot] - mean_place))
    return points


def scorer_check(maps, draws, seed):
    check = {}
    for factor in ['total', *FACTORS]:
        points = scorer_points(maps, factor)
        keys = sorted({p[0] for p in points})
        if len(points) < 3:
            check[factor] = None
            continue

        def rho(sample_keys, column):
            chosen = [p for key in sample_keys for p in points if p[0] == key]
            return spearman([p[1] for p in chosen], [p[column] for p in chosen])
        check[factor] = {
            'maps': len(keys), 'colonies': len(points),
            'wins_rho': rho(keys, 2), 'wins_rho_ci': bootstrap(keys, lambda s: rho(s, 2), draws, seed),
            'placement_rho': rho(keys, 3),
            'placement_rho_ci': bootstrap(keys, lambda s: rho(s, 3), draws, seed + 1),
        }
    # Does the start the scorer rates best actually win more than its fair share?
    top_wins = top_n = 0
    for m in maps:
        values = [c['total'] for c in m['record_colonies']]
        if not values or max(values) - min(values) < 1e-9:
            continue
        top = max(range(len(values)), key=lambda i: values[i])
        top_wins += m['slot']['counts'][top]
        top_n += m['slot']['n']
    check['top_rated_start'] = {'wins': top_wins, 'n': top_n,
                                'share': top_wins / top_n if top_n else None,
                                'wilson': list(wilson(top_wins, top_n)) if top_n else None}
    return check


def analyse_generator(method, maps, config, catalog):
    n = int(config['colonies'])
    draws = int(config['bootstrap_draws'])
    usable = [m for m in maps if m['slot']['n'] >= 2]
    p_values = [m['slot']['p'] for m in usable]
    bh = benjamini_hochberg(p_values) if p_values else []
    hl = holm(p_values) if p_values else []
    for m, q, h in zip(usable, bh, hl):
        m['slot']['q_bh'] = q
        m['slot']['p_holm'] = h
    biases = [m['slot']['squared_bias'] for m in usable]
    mean_bias = statistics.mean(biases) if biases else None
    seed = seed_for('generator', method)
    ci = bootstrap(biases, lambda s: rms_points(statistics.mean(s), n), draws, seed) if biases else None
    pooled_slot = [sum(m['slot']['counts'][s] for m in maps) for s in range(n)]
    pooled_team = [sum(m['team']['counts'][t] for m in maps) for t in range(n)]
    # Is any of this generator's maps biased? Sum of per-map chi-square statistics, calibrated
    # by simulating every map's winners under a fair map (seeded, so the report reproduces).
    observed = sum(m['slot']['chi2'] for m in usable) if usable else None
    pooled_p = None
    if usable:
        rng = random.Random(seed + 7)
        simulations = 4000
        hits = 0
        for _ in range(simulations):
            total = 0.0
            for m in usable:
                xs = [0] * n
                for _ in range(m['slot']['n']):
                    xs[rng.randrange(n)] += 1
                total += chi_square_uniform(xs)[0]
            hits += total >= observed - 1e-9
        pooled_p = (hits + 1) / (simulations + 1)
    info = catalog.get(method, {})
    played = sum(m['played'] for m in maps)
    return {
        'method': method, 'name': info.get('nameKey', str(method)), 'id': info.get('id'),
        'revision': info.get('revision'), 'maps': len(maps), 'maps_with_winners': len(usable),
        'games': sum(m['games'] for m in maps), 'played': played,
        'errors': sum(m['errors'] for m in maps),
        'cap_share': sum(m['cap_games'] for m in maps) / played if played else None,
        'prestige_share': sum(m['prestige_games'] for m in maps) / played if played else None,
        'unresolved': sum(m['unresolved'] for m in maps),
        'start_mismatches': sum(m['start_mismatches'] for m in maps),
        'headline_rms_points': rms_points(mean_bias, n), 'headline_rms_points_ci': ci,
        'mean_squared_bias': mean_bias,
        'maps_p05': sum(p < 0.05 for p in p_values), 'maps_bh05': sum(q < 0.05 for q in bh),
        'maps_holm05': sum(h < 0.05 for h in hl),
        'expected_p05_by_chance': 0.05 * len(p_values),
        'dominance': {'min': min(m['slot']['dominance'] for m in usable),
                      'median': statistics.median(m['slot']['dominance'] for m in usable),
                      'max': max(m['slot']['dominance'] for m in usable)} if usable else None,
        'pooled_chi2': observed, 'pooled_p': pooled_p,
        'slot_index': share_table(pooled_slot, seed + 11),
        'team_index': share_table(pooled_team, seed + 13),
        'scorer': scorer_check(maps, draws, seed + 17),
        'ticks_median': statistics.median([m['ticks_median'] for m in maps if m['ticks_median']])
        if played else None,
        'wall_seconds_mean': statistics.mean([m['wall_seconds_mean'] for m in maps if m['wall_seconds_mean']])
        if played else None,
        'map_keys': [m['key'] for m in maps],
    }


def load_catalog(study):
    try:
        entries = json.loads(subprocess.run([str(study), '--catalog'], cwd=ROOT, capture_output=True,
                                            text=True, timeout=60).stdout)
        return {e['method']: e for e in entries}
    except Exception:
        return {}


def summarize(out, config, paths):
    records, games = load_results(out)
    catalog = load_catalog(paths['study'])
    by_map = {}
    for game in games:
        by_map.setdefault(game['map'], []).append(game)
    maps = [analyse_map(record, by_map.get(key, []), config)
            for key, record in records.items() if record.get('ok')]
    generators = []
    for method in [g['method'] for g in config['generators']]:
        chosen = [m for m in maps if m['method'] == method]
        failed = [r for r in records.values() if r['method'] == method and not r.get('ok')]
        summary = analyse_generator(method, chosen, config, catalog)
        summary['failed_maps'] = [{'map_seed': r['map_seed'], 'failure': r.get('failure')} for r in failed]
        generators.append(summary)
    n = int(config['colonies'])
    all_team = [sum(m['team']['counts'][t] for m in maps) for t in range(n)]
    baseline_team = [sum(m['team']['counts'][t] for m in maps if m['method'] == BASELINE_METHOD)
                     for t in range(n)]
    played = [g for g in games if g['outcome']['status'] == 'ok']
    walls = [g['wall_seconds'] for g in played]
    ticks = [g['outcome']['ticks'] for g in played]
    summary = {
        'config': config, 'git_revision': paths['git_revision'],
        'generated_at': datetime.datetime.now().isoformat(timespec='seconds'),
        'balanced': config['rotation_count'] == n,
        'games': len(games), 'played': len(played),
        'wall_seconds': {'mean': statistics.mean(walls) if walls else None,
                         'median': statistics.median(walls) if walls else None,
                         'max': max(walls) if walls else None,
                         'total': sum(walls),
                         'per_1000_ticks': sum(walls) / sum(ticks) * 1000 if ticks and sum(ticks) else None},
        'ticks': {'median': statistics.median(ticks) if ticks else None, 'max': max(ticks) if ticks else None},
        'end_reasons': {reason: sum(g['outcome']['end_reason'] == reason for g in played)
                        for reason in sorted({g['outcome']['end_reason'] for g in played}, key=str)},
        'engine_team_index': {'all_generators': share_table(all_team, 101),
                              'baseline': share_table(baseline_team, 103)},
        'scorer_all_generators': scorer_check(maps, int(config['bootstrap_draws']), 107),
        'generators': generators,
        'maps': [{k: v for k, v in m.items() if k != 'record_colonies'} for m in maps],
        'verification': json.loads((out / 'verification.json').read_text())
        if (out / 'verification.json').exists() else None,
    }
    (out / 'summary.json').write_text(json.dumps(summary, indent=2))
    write_csvs(out, records, games)
    (out / 'summary.md').write_text(markdown(summary, config))
    return summary


def write_csvs(out, records, games):
    ordered = sorted(games, key=lambda g: (g['map'], g['rotation'], g['game']))
    with open(out / 'games.csv', 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['generator', 'map_seed', 'map_roll_seed', 'rotation', 'engine_seed', 'status',
                         'end_reason', 'adjudication', 'ticks', 'wall_seconds', 'winner_start',
                         'winner_team', 'winner_x', 'winner_y', 'elimination_order', 'placement_by_start',
                         'start_mismatch', 'orders', 'cap_checksum'])
        for g in ordered:
            record = records[g['map']]
            o = g['outcome']
            end = g['parsed']['end'] or {}
            start = record['starts'][o['winner_slot']] if o['winner_slot'] is not None else [None, None]
            writer.writerow([record['method'], record['map_seed'], record.get('chosen_seed'), g['rotation'],
                             g['seed'], o['status'], o['end_reason'], o['adjudication'], o['ticks'],
                             g['wall_seconds'], o['winner_slot'], o['winner_team'], start[0], start[1],
                             ';'.join(f'{s}@{t}' for s, t in o['elimination_order']),
                             ';'.join(f'{s}:{p}' for s, p in sorted(o['placement'].items(), key=lambda i: int(i[0]))),
                             int(o['start_mismatch']), end.get('orders'), g['parsed']['cap_checksum']])
    team_fields = ['result', 'alive', 'eliminated_tick', 'prestige', 'units', 'workers', 'explorers',
                   'warriors', 'buildings', 'sites']
    with open(out / 'colonies.csv', 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['generator', 'map_seed', 'rotation', 'engine_seed', 'start', 'team', 'start_x',
                         'start_y', 'placement', 'won', *team_fields, 'quality_total',
                         *[f'quality_{f}' for f in FACTORS]])
        for g in ordered:
            record = records[g['map']]
            n = record['colonies']
            for team, entry in sorted(g['parsed']['teams'].items(), key=lambda i: int(i[0])):
                slot = (int(team) - g['rotation']) % n
                quality = record['colonies'][slot] if len(record['colonies']) == n else {}
                writer.writerow([record['method'], record['map_seed'], g['rotation'], g['seed'], slot, team,
                                 *record['starts'][slot], g['outcome']['placement'].get(str(slot)),
                                 int(g['outcome']['winner_slot'] == slot),
                                 *[entry.get(k) for k in team_fields], quality.get('total'),
                                 *[quality.get(f) for f in FACTORS]])
    with open(out / 'maps.csv', 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['generator', 'map_seed', 'roll_seed', 'ok', 'failure', 'quality_score',
                         'quality_fairness', 'starts', 'quality_by_start', 'map_fnv1a64_r0',
                         'generation_seconds'])
        for record in records.values():
            writer.writerow([record['method'], record['map_seed'], record.get('chosen_seed'),
                             int(bool(record.get('ok'))), record.get('failure'),
                             record.get('quality', {}).get('score'), record.get('quality', {}).get('fairness'),
                             ';'.join(f'{x}:{y}' for x, y in record.get('starts', [])),
                             ';'.join(f'{c["total"]:.3f}' for c in record.get('colonies', [])),
                             next((f['fnv1a64'] for f in record.get('files', []) if f['rotation'] == 0), None),
                             record.get('generation_seconds')])


# ---------------------------------------------------------------------------- report

def pct(value, digits=1):
    return '-' if value is None else f'{100 * value:.{digits}f}%'


def num(value, digits=2):
    return '-' if value is None else f'{value:.{digits}f}'


def pval(value):
    if value is None:
        return '-'
    return '<0.001' if value < 0.001 else f'{value:.3f}'


def interval(ci, fmt=lambda v: f'{v:.1f}'):
    return '' if not ci or ci[0] is None else f' [{fmt(ci[0])}, {fmt(ci[1])}]'


def markdown(summary, config):
    n = int(config['colonies'])
    lines = [f'# Map fairness tournament: {config["name"]}', '']
    if config.get('description'):
        lines += [config['description'], '']
    wall = summary['wall_seconds']
    ends = ', '.join(f'{count} {reason}' for reason, count in summary['end_reasons'].items())
    lines += [
        f'- Revision `{summary["git_revision"]}`, generated {summary["generated_at"]}, preset `{config["preset_file"]}`.',
        f'- {config["width"]}x{config["height"]} maps, {n} colonies, `{config["ai"]}` in every slot, free for all '
        f'(prestige victory on, as in the lobby), tick cap {config["tick_cap"]}.',
        f'- Each map is the best of {config["candidates"]} lobby rolls of its seed; {config["rotation_count"]} '
        f'rotation(s) x {config["games_per_rotation"]} engine seed(s) = '
        f'{config["rotation_count"] * int(config["games_per_rotation"])} games per map.'
        + ('' if summary['balanced'] else ' **Not rotation-balanced: start position and team index are confounded.**'),
        f'- {summary["played"]} of {summary["games"]} games completed ({ends}); wall time per game mean '
        f'{num(wall["mean"], 1)} s, median {num(wall["median"], 1)} s, max {num(wall["max"], 1)} s '
        f'({num(wall["per_1000_ticks"], 2)} s per 1000 ticks); median game length {summary["ticks"]["median"]} ticks.',
        '- A game that reaches the tick cap goes to the surviving colony with the most prestige '
        '(then buildings, then units); an exact tie stays unresolved and is left out of win counts.',
    ]
    verification = summary.get('verification')
    if verification:
        lines.append(f'- Reproducibility: {verification["identical"]} of {verification["checked"]} re-run games '
                     f'matched their first run exactly (ticks, winner, order count, every team result).')
    lines += ['', '## Headline by generator', '',
              'Position bias is the root-mean-square gap between each start\'s true win rate and the fair '
              f'1/{n}, corrected for the scatter raw win shares show even on a fair map, averaged over maps '
              '(95% bootstrap interval over maps). Biased maps counts maps whose wins by start reject a '
              'uniform split: raw p < 0.05, and after Benjamini-Hochberg across that generator\'s maps. '
              'Scorer rho is the within-map rank correlation between a colony\'s start-quality score and its '
              'win share.', '',
              '| Generator | Maps | Games | Cap | Position bias (pp) | Biased maps p<.05 / BH | Best start / fair (median) | Any bias p | Scorer rho (wins) |',
              '| --- | ---: | ---: | ---: | --- | --- | ---: | ---: | --- |']
    for g in summary['generators']:
        scorer = (g['scorer'] or {}).get('total') or {}
        lines.append(
            f'| {g["name"]} ({g["method"]}) | {g["maps_with_winners"]}/{g["maps"]} | {g["played"]} | {pct(g["cap_share"], 0)} '
            f'| {num(g["headline_rms_points"], 1)}{interval(g["headline_rms_points_ci"])} '
            f'| {g["maps_p05"]} / {g["maps_bh05"]} (chance {g["expected_p05_by_chance"]:.1f}) '
            f'| {num((g["dominance"] or {}).get("median"))} | {pval(g["pooled_p"])} '
            f'| {num(scorer.get("wins_rho"))}{interval(scorer.get("wins_rho_ci"), lambda v: f"{v:.2f}")} |')
    lines += ['', '## Engine team-index bias', '',
              'Wins by team index, pooled over rotations, so every index played every start equally often. '
              'A skew here is the engine\'s processing order (team 0 also carries the passive local player of '
              '`-test-games-nox` and its AI polls last), not the map.', '',
              '| Scope | Decided games | Wins by team ' + ' / '.join(str(t) for t in range(n))
              + ' | p (uniform) | Top team share [95%] | Bias (pp) |',
              '| --- | ---: | --- | ---: | --- | ---: |']
    for label, table in (('Symmetric arena (baseline)', summary['engine_team_index']['baseline']),
                         ('All generators', summary['engine_team_index']['all_generators'])):
        if table['n']:
            lines.append(f'| {label} | {table["n"]} | {" / ".join(map(str, table["counts"]))} | {pval(table["p"])} '
                         f'| team {table["best"]}: {pct(table["best_share"])}{interval(table["best_share_wilson"], pct)} '
                         f'| {num(table["rms_points"], 1)} |')
    for g in summary['generators']:
        lines += ['', f'## {g["name"]} (generator {g["method"]}, id `{g["id"]}`, revision {g["revision"]})', '']
        if g['failed_maps']:
            lines += ['Maps that failed to generate: ' + ', '.join(
                f'{f["map_seed"]} ({f["failure"]})' for f in g['failed_maps']) + '.', '']
        lines += ['| Map seed (roll) | Games (cap) | Wins by start ' + ' / '.join(str(s) for s in range(n))
                  + ' | p | BH q | Best start share [95%] | Best / fair | Bias (pp) | Quality by start | Quality rho |',
                  '| --- | --- | --- | ---: | ---: | --- | ---: | ---: | --- | ---: |']
        for m in summary['maps']:
            if m['method'] != g['method']:
                continue
            s = m['slot']
            best = f'start {s["best"]}: {pct(s["best_share"])}{interval(s["best_share_wilson"], pct)}' \
                if s['n'] else '-'
            lines.append(
                f'| {m["map_seed"]} ({m["chosen_seed"]}) | {m["played"]} ({m["cap_games"]}) | {" / ".join(map(str, s["counts"]))} '
                f'| {pval(s["p"])} | {pval(s.get("q_bh"))} | {best} | {num(s["dominance"])} | {num(s["rms_points"], 1)} '
                f'| {" / ".join(f"{q:.2f}" for q in (m["quality"] or []))} | {num(m["quality_spearman"])} |')
        slot, team = g['slot_index'], g['team_index']
        dominance = ' / '.join(num(g['dominance'][k]) for k in ('min', 'median', 'max')) if g['dominance'] else '-'
        lines += ['',
                  f'- Wins by generator colony index, pooled over maps: {" / ".join(map(str, slot["counts"]))} '
                  f'(p {pval(slot["p"])}); a skew means the generator favours a colony it places early or late.',
                  f'- Wins by team index, pooled: {" / ".join(map(str, team["counts"]))} (p {pval(team["p"])}).',
                  f'- Maps significant after Holm: {g["maps_holm05"]}; best-start dominance min / median / max: {dominance}.']
        scorer = g['scorer'] or {}
        if scorer.get('total'):
            top = scorer['top_rated_start']
            lines.append('- Scorer check: rho with win share ' + num(scorer['total']['wins_rho'])
                         + interval(scorer['total']['wins_rho_ci'], lambda v: f'{v:.2f}')
                         + ', with placement ' + num(scorer['total']['placement_rho'])
                         + interval(scorer['total']['placement_rho_ci'], lambda v: f'{v:.2f}')
                         + f'; the top-rated start won {top["wins"]} of {top["n"]} ({pct(top["share"])}'
                         + interval(top['wilson'], pct) + f', fair {pct(1 / n)}). By factor (wins rho): '
                         + ', '.join(f'{f} {num((scorer.get(f) or {}).get("wins_rho"))}' for f in FACTORS) + '.')
        elif g['method'] == BASELINE_METHOD:
            lines.append('- Scorer check: not applicable, every colony scores the same by construction.')
    overall = (summary['scorer_all_generators'] or {}).get('total')
    if overall:
        lines += ['', '## Scorer check across generators', '',
                  f'Within-map rank correlation between start-quality score and win share over {overall["colonies"]} '
                  f'colonies on {overall["maps"]} maps: rho {num(overall["wins_rho"])}'
                  f'{interval(overall["wins_rho_ci"], lambda v: f"{v:.2f}")}; with placement '
                  f'{num(overall["placement_rho"])}{interval(overall["placement_rho_ci"], lambda v: f"{v:.2f}")}.']
    lines.append('')
    return '\n'.join(lines)


# ---------------------------------------------------------------------------- driver

def resolve_paths(args, out):
    return {'out': out, 'maps': out / 'maps', 'games': out / 'games', 'profiles': out / 'profiles',
            'glob2': Path(args.bin).resolve(), 'study': Path(args.study).resolve(),
            'git_revision': git_revision()}


def verify(config, paths, records, games):
    """Re-run a few finished games from scratch and require identical outcomes."""
    count = int(config.get('verify_games') or 0)
    finished = sorted((g for g in games if g['outcome']['status'] == 'ok'),
                      key=lambda g: (g['map'], g['rotation'], g['game']))
    checks = []
    for game in finished[:count]:
        job = {k: game[k] for k in ('map', 'rotation', 'game', 'seed')}
        directory = paths['out'] / 'verify' / game['map'] / f'r{game["rotation"]}-k{game["game"]}'
        again = run_game(config, paths, records[game['map']], job, directory=directory, force=True)
        same = (again['parsed']['end'] == game['parsed']['end'] and
                again['parsed']['teams'] == game['parsed']['teams'])
        checks.append({'map': game['map'], 'rotation': game['rotation'], 'seed': game['seed'],
                       'identical': same, 'first': game['parsed']['end'], 'again': again['parsed']['end']})
        print(f'verify {game["map"]} r{game["rotation"]} seed {game["seed"]}: {"identical" if same else "DIFFERENT"}',
              flush=True)
    result = {'checked': len(checks), 'identical': sum(c['identical'] for c in checks), 'games': checks}
    (paths['out'] / 'verification.json').write_text(json.dumps(result, indent=2))
    return result


def command_run(args):
    overrides = {'generators': args.generators, 'map_seeds': args.map_seeds, 'jobs': args.jobs,
                 'games_per_rotation': args.games_per_rotation, 'tick_cap': args.tick_cap,
                 'size': args.size, 'colonies': args.colonies, 'verify_games': args.verify,
                 'name': args.name}
    config = load_config(args.preset, overrides)
    out = Path(args.out).resolve() if args.out else ROOT / 'artifacts' / 'map-fairness' / config['name']
    out.mkdir(parents=True, exist_ok=True)
    if args.fresh:
        # Only this tool's own outputs, and never the run directory itself.
        for name in RUN_DIRECTORIES:
            remove_tree(out / name, out)
        for name in RUN_FILES:
            (out / name).unlink(missing_ok=True)
    paths = resolve_paths(args, out)
    for binary in (paths['glob2'], paths['study']):
        if not binary.exists():
            raise SystemExit(f'error: {binary} not found; build with scons release=1 and '
                             f'scons release=1 map-generator-study')
    ignored = ('jobs', 'verify_games', 'description', 'git_revision')
    comparable = {k: v for k, v in config.items() if k not in ignored}
    previous = out / 'config.json'
    if previous.exists():
        old = {k: v for k, v in json.loads(previous.read_text()).items() if k not in ignored}
        if old != json.loads(json.dumps(comparable)):
            raise SystemExit(f'error: {out} holds a run with a different configuration; use --fresh or --out')
    previous.write_text(json.dumps(config | {'git_revision': paths['git_revision']}, indent=2))
    jobs = int(config['jobs'])
    print(f'map fairness tournament "{config["name"]}" -> {out}', flush=True)

    tasks = [(g, s) for g in config['generators'] for s in config['map_seeds']]
    records = run_pool(tasks, jobs, lambda t: produce_map(config, paths, t[0], t[1]),
                       lambda t, r: f'map {r["key"]}: ' + ('ok' if r.get('ok') else r.get('failure', '?')))
    by_key = {r['key']: r for r in records}

    def describe(job, game):
        o = game['outcome']
        who = f'start {o["winner_slot"]} (team {o["winner_team"]})' if o['winner_slot'] is not None else 'no winner'
        return (f'{job["map"]} r{job["rotation"]} seed {job["seed"]}: {o["status"]}, {o["end_reason"]}, {who}, '
                f'{o["ticks"]} ticks, {game["wall_seconds"]:.0f}s')
    games = run_pool(game_jobs(config, records), jobs,
                     lambda j: run_game(config, paths, by_key[j['map']], j), describe)
    if config.get('verify_games'):
        verify(config, paths, by_key, games)
    remove_tree(paths['profiles'], out)
    summary = summarize(out, config, paths)
    print(f'\nwrote {out / "summary.md"}, summary.json, games.csv, colonies.csv, maps.csv')
    for g in summary['generators']:
        print(f'  {g["name"]} ({g["method"]}): position bias {num(g["headline_rms_points"], 1)} pp'
              f'{interval(g["headline_rms_points_ci"])}, biased maps {g["maps_p05"]}/{g["maps_with_winners"]} '
              f'(BH {g["maps_bh05"]}), cap {pct(g["cap_share"], 0)}')
    return 0


def command_summarize(args):
    out = Path(args.out).resolve()
    config = json.loads((out / 'config.json').read_text())
    paths = resolve_paths(args, out)
    summarize(out, config, paths)
    print(f'wrote {out / "summary.md"}')
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)
    run = sub.add_parser('run', help='produce maps, play games and summarize')
    run.add_argument('preset', help='preset name in tools/map-fairness/ or a path to a JSON preset')
    run.add_argument('--out', help='output directory (default: artifacts/map-fairness/<name>)')
    run.add_argument('--name', help='run name, overriding the preset (also names the default output directory)')
    run.add_argument('--fresh', action='store_true', help='discard earlier results in the output directory')
    run.add_argument('--generators', help='comma-separated generator ids, overriding the preset')
    run.add_argument('--map-seeds', help='e.g. 1001-1008 or 1001,1005')
    run.add_argument('--games-per-rotation', type=int, help='engine seeds per map rotation')
    run.add_argument('--size', type=int, help='map side in tiles (power of two)')
    run.add_argument('--colonies', type=int)
    run.add_argument('--tick-cap', type=int)
    run.add_argument('--jobs', type=int, help='concurrent processes')
    run.add_argument('--verify', type=int, help='re-run this many games to check reproducibility')
    summarize_parser = sub.add_parser('summarize', help='recompute the summary of an existing run')
    summarize_parser.add_argument('out', help='run directory')
    for p in (run, summarize_parser):
        p.add_argument('--bin', default=str(ROOT / 'build' / 'src' / 'glob2'))
        p.add_argument('--study', default=str(ROOT / 'build' / 'src' / 'MapGeneratorStudy'))
    args = parser.parse_args()
    return command_run(args) if args.command == 'run' else command_summarize(args)


if __name__ == '__main__':
    sys.exit(main())
