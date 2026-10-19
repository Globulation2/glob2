# SPDX-License-Identifier: GPL-3.0-or-later
"""Reproducible, budgeted match sampling for resource-gradient collection.

Use the same pinned catalog/search domains as the AI rating tournaments. Balance
format quotas and generator exposure, then randomize compatible dimensions,
controls, rosters and seeds. This samples a distribution, not a cross product.
"""
from collections import Counter
import hashlib
import json
import random

from tools.tournaments.model import generator_samples

FORMATS = {
    '1v1': [1, 2],
    '3ffa': [1, 2, 3],
    '4ffa': [1, 2, 3, 4],
    '8ffa': list(range(1, 9)),
    '16ffa': list(range(1, 17)),
    '2v2': [1, 1, 2, 2],
    '4v4': [1]*4 + [2]*4,
    '8v8': [1]*8 + [2]*8,
}
SIZES = (64, 128, 256, 512)


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def display(control, value):
    # info catalog uses internal exponents; map generate --set expects tiles
    # and other displayed values, including repeat controls on old generators.
    return 1 << value if control.get('powerOfTwo') else value


def sample_plan(catalog, games=256, seed=5101, *, generators=None, sizes=None, formats=None, ais=None):
    if catalog.get('schema_version') != 1:
        raise ValueError('Expected engine catalog schema_version 1')
    available = {g['id']: g for g in catalog['generators'] if not g.get('editorOnly')}
    ai_names = {a['name'] for a in catalog['ais']}
    methods = sorted(set(generators if generators is not None else available))
    formats = sorted(set(formats if formats is not None else FORMATS))
    sizes = sorted(set(sizes if sizes is not None else SIZES))
    ais = sorted(set(ais if ais is not None else ai_names))
    if not methods or set(methods)-available.keys():
        raise ValueError('Generators must be non-editor IDs from the supplied engine catalog')
    if not ais or set(ais)-ai_names:
        raise ValueError('AIs must be names from the supplied engine catalog')
    if not formats or set(formats)-FORMATS.keys():
        raise ValueError('Unknown or empty match format pool')
    if not sizes or any(type(s) is not int or s < 1 or s > 512 or s & (s-1) for s in sizes):
        raise ValueError('Map sizes must be powers of two, at most 512 tiles')
    if type(games) is not int or games < len(formats):
        raise ValueError('Game budget must cover every selected format at least once')
    if type(seed) is not int or not 0 <= seed <= 2**32-1:
        raise ValueError('Sample seed must be uint32')
    controls = {name: {c['id']: c for c in available[name]['controls']} for name in methods}
    compatible = {}
    for fmt in formats:
        n = len(FORMATS[fmt])
        for name in methods:
            c = controls[name]
            widths = {display(c['width'], v) for v in c['width']['values']}
            heights = {display(c['height'], v) for v in c['height']['values']}
            shapes = [(w, h) for w in sizes for h in sizes if w in widths and h in heights
                      and max(w, h) <= 2*min(w, h) and 1024*n <= w*h <= 32768*n]
            if n in c['teams']['values'] and shapes:
                compatible[fmt, name] = shapes
        if not any((fmt, name) in compatible for name in methods):
            raise ValueError(f'No compatible generator/dimensions for {fmt}')
    unreachable = [name for name in methods if not any((fmt, name) in compatible for fmt in formats)]
    if unreachable:
        raise ValueError(f'Generators incompatible with the selected formats/sizes: {unreachable}')

    rng = random.Random(seed)
    # Quotas differ by at most one. Shuffling prevents run order from following
    # format, map size or estimated cost, including when several workers run.
    order = formats.copy(); rng.shuffle(order)
    order = [order[i % len(order)] for i in range(games)]; rng.shuffle(order)
    generator_counts, shape_counts, ai_counts = Counter(), Counter(), Counter()
    plan = []
    for fmt in order:
        candidates = [name for name in methods if (fmt, name) in compatible]
        least = min(generator_counts[name] for name in candidates)
        name = rng.choice([name for name in candidates if generator_counts[name] == least])
        shapes = compatible[fmt, name]
        least = min(shape_counts[shape] for shape in shapes)
        width, height = rng.choice([shape for shape in shapes if shape_counts[shape] == least])
        map_seed, game_seed = rng.getrandbits(32), rng.getrandbits(32)
        raw = generator_samples(catalog, available[name]['method'], 1, map_seed)[0]
        params = {key: display(controls[name][key], value) for key, value in raw.items()}
        workers = controls[name].get('workers')
        if workers:
            domain = workers.get('searchValues')
            if not domain or any(v not in workers['values'] for v in domain):
                raise ValueError(f'{name}: invalid starting-worker search domain')
            params['workers'] = display(workers, rng.choice(domain))
        alliances = FORMATS[fmt].copy(); rng.shuffle(alliances)
        roster = []
        homogeneous = rng.randrange(2) == 0
        for seat in range(len(alliances)):
            if homogeneous and seat:
                ai = roster[0]
            else:
                least = min(ai_counts[a] for a in ais)
                ai = rng.choice([a for a in ais if ai_counts[a] == least])
            roster.append(ai); ai_counts[ai] += 1
        plan.append(dict(format=fmt, generator=name, width=width, height=height,
                         players=len(roster), alliances=alliances, ais=roster,
                         map_seed=map_seed, game_seed=game_seed, generator_params=params))
        generator_counts[name] += 1; shape_counts[width, height] += 1
    # Catalog bounds describe individual controls, not all generator-specific
    # relationships. Freeze bounded alternatives now; collection checks actual
    # generation and records every rejection before choosing a playable map.
    for spec in plan:
        alternatives=[]
        names=[name for name in methods if (spec['format'],name) in compatible]
        for attempt in range(31):
            name=rng.choice(names);width,height=rng.choice(compatible[spec['format'],name])
            candidate_seed=rng.getrandbits(32)
            raw=generator_samples(catalog,available[name]['method'],1,candidate_seed)[0]
            params={key:display(controls[name][key],value) for key,value in raw.items()}
            workers=controls[name].get('workers')
            if workers:params['workers']=display(workers,rng.choice(workers['searchValues']))
            alternatives.append(dict(generator=name,width=width,height=height,map_seed=candidate_seed,generator_params=params))
        spec['map_candidates']=alternatives
    design = dict(seed=seed, games=games, generators=methods, sizes=sizes, formats=formats, ais=ais,
                  parameter_domain='search', map_attempts=32, aspect_ratio_max=2,
                  tiles_per_colony_min=1024, tiles_per_colony_max=32768)
    # Keep the actual capability snapshot, not just its hash: future catalog
    # changes must not silently change what this sampling seed meant.
    return dict(schema=1, design=design, catalog=catalog, catalog_sha256=digest(catalog),
                games=plan, coverage=coverage(plan, methods))


def coverage(plan, eligible=()):
    return dict(games=len(plan), formats=dict(sorted(Counter(s['format'] for s in plan).items())),
                generators=dict(sorted(Counter(s['generator'] for s in plan).items())),
                unselected_generators=sorted(set(eligible)-{s['generator'] for s in plan}),
                dimensions=dict(sorted(Counter(f"{s.get('width', s.get('size'))}x{s.get('height', s.get('size'))}"
                                               for s in plan).items())),
                ai_seats=dict(sorted(Counter(a for s in plan for a in s.get('ais', [s.get('ai', 'nicowar')]*s['players'])).items())))


def map_identity(spec):
    """Keep repeats/roster swaps of the same generated map in one fit split."""
    return digest(dict(generator=spec['generator'], width=spec.get('width', spec.get('size')),
                       height=spec.get('height', spec.get('size')), players=spec['players'],
                       map_seed=spec['map_seed'], generator_params=spec.get('generator_params', {})))[:16]
