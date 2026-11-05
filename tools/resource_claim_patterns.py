"""Temporary resource-claims experiment: deterministic, deduplicated probe arrays.

Offsets use signed 16-bit coordinates and an origin byte (unit/resource/midpoint).
Generation is offline; no engine random stream or floating-point geometry is used.
The runtime evaluator should cache repeated (origin, x, y) probes across patterns.
"""
import argparse
from dataclasses import asdict, dataclass, replace
from functools import lru_cache
import hashlib
import itertools
import json
from pathlib import Path
import random
import struct


SCALES = (0, 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256)
FAMILIES = ('spiral', 'cone_toward', 'cone_away', 'cone_perpendicular',
            'random', 'rays', 'rays_grouped', 'rays_toward')
ORIGINS = ('unit', 'resource', 'midpoint', 'interleaved')
DIRECTIONS = ((1, 0), (1, 1), (0, 1), (-1, 1),
              (-1, 0), (-1, -1), (0, -1), (1, -1))
PROBE = struct.Struct('<hhB')


@dataclass(frozen=True)
class Pattern:
    family: str
    radius: int
    origin: str = 'unit'
    stride: int = 1
    subset: str = 'every'
    order: str = 'forward'
    direction: int = 0
    seed: int = 1


def seeded_random(spec, domain):
    # Stable across enumeration order and independent of Python's salted hash().
    settings = asdict(spec)
    settings.pop('origin')
    if domain in ('base', 'order'):
        # A sparse variant samples the same dense ordering as its siblings.
        settings.pop('stride')
        settings.pop('subset')
    if domain == 'base':
        settings.pop('order')
    payload = json.dumps([settings, domain], sort_keys=True).encode()
    return random.Random(int.from_bytes(hashlib.sha256(payload).digest(), 'big'))


def spiral(radius):
    """Square spiral, clipped to an integer Euclidean disk, starting at zero."""
    yield 0, 0
    for r in range(1, radius + 1):
        for x, y in itertools.chain(((r, y) for y in range(1-r, r+1)),
                                    ((x, r) for x in range(r-1, -r-1, -1)),
                                    ((-r, y) for y in range(r-1, -r-1, -1)),
                                    ((x, -r) for x in range(1-r, r+1))):
            if x*x + y*y <= radius*radius:
                yield x, y


def coordinates(spec):
    if spec.family not in FAMILIES or spec.origin not in ORIGINS:
        raise ValueError('unknown family or origin')
    if type(spec.radius) is not int or not 0 <= spec.radius <= 256:
        raise ValueError('radius must be an integer in 0..256')
    if type(spec.stride) is not int or spec.stride < 1:
        raise ValueError('stride must be positive; zero sparsity is undefined')
    if spec.subset not in ('every', 'random_block') or spec.order not in ('forward', 'reverse', 'shuffle'):
        raise ValueError('unknown subset or order')
    if type(spec.direction) is not int or not 0 <= spec.direction < 8:
        raise ValueError('direction must be an octant in 0..7')
    points = ordered_coordinates(replace(spec, origin='unit', stride=1, subset='every'))
    if spec.subset == 'every':
        points = points[::spec.stride]
    else:
        rng = seeded_random(spec, 'subset')
        points = [points[rng.randrange(i, min(i+spec.stride, len(points)))]
                  for i in range(0, len(points), spec.stride)]
    return [(0, 0), *points]


@lru_cache(maxsize=8)
def ordered_coordinates(spec):
    """Bounded reuse across sparsities/origins; never regenerate their geometry."""
    dx, dy = DIRECTIONS[spec.direction]
    if spec.family.startswith('rays'):
        directions = list(DIRECTIONS)
        if spec.family == 'rays_toward':
            directions.sort(key=lambda d: -(d[0]*dx + d[1]*dy))
        if spec.family == 'rays_grouped':
            points = [(x*r, y*r) for x, y in directions for r in range(1, spec.radius+1)
                      if (x*x+y*y)*r*r <= spec.radius*spec.radius]
        else:
            points = [(x*r, y*r) for r in range(1, spec.radius+1) for x, y in directions
                      if (x*x+y*y)*r*r <= spec.radius*spec.radius]
    else:
        points = list(spiral(spec.radius))[1:]
        if spec.family.startswith('cone_'):
            def inside(p):
                along, across = p[0]*dx+p[1]*dy, p[1]*dx-p[0]*dy
                if spec.family == 'cone_toward':
                    return along >= abs(across)
                if spec.family == 'cone_away':
                    return -along >= abs(across)
                return abs(across) >= abs(along)
            points = [p for p in points if inside(p)]
        elif spec.family == 'random':
            seeded_random(spec, 'base').shuffle(points)
    if spec.order == 'reverse':
        points.reverse()
    elif spec.order == 'shuffle':
        seeded_random(spec, 'order').shuffle(points)
    return tuple(points)


def encode(spec):
    """First probe always preserves the unit's current-gradient destination."""
    origins = range(3) if spec.origin == 'interleaved' else (ORIGINS.index(spec.origin),)
    result = bytearray(PROBE.pack(0, 0, 0))
    for x, y in coordinates(spec):
        for origin in origins:
            if x or y or origin:
                result.extend(PROBE.pack(x, y, origin))
    return bytes(result)


def specifications(families=FAMILIES, radii=SCALES, origins=ORIGINS,
                   strides=SCALES[1:], subsets=('every', 'random_block'),
                   orders=('forward', 'reverse', 'shuffle'), directions=range(8), seed=1):
    for family, radius, direction, order, origin, stride, subset in itertools.product(
            families, radii, directions, orders, origins, strides, subsets):
        # These variants cannot add information. Other duplicates are folded by
        # exact array contents when publishing, retaining every parameter alias.
        if stride == 1 and subset != 'every':
            continue
        if direction and family not in ('cone_toward', 'cone_away', 'cone_perpendicular', 'rays_toward'):
            continue
        if radius == 0 and (stride != 1 or order != 'forward' or direction):
            continue
        if stride > max(1, (2*radius+1)**2-1):
            continue
        yield Pattern(family, radius, origin, stride, subset, order, direction, seed)


def header(name, data):
    probes = list(PROBE.iter_unpack(data))
    lines = ['#pragma once', '#include <cstdint>', 'namespace ResourceClaimExperiment {']
    for index, (ctype, field) in enumerate((('std::int16_t', 'x'), ('std::int16_t', 'y'),
                                           ('std::uint8_t', 'origin'))):
        lines.append(f'inline constexpr {ctype} {name}_{field}[] = {{')
        for start in range(0, len(probes), 32):
            lines.append(','.join(str(p[index]) for p in probes[start:start+32]) + ',')
        lines.append('};')
    lines.append('}')
    return ('\n'.join(lines)+'\n').encode()


def pilot_header(seed=1):
    """Small explicit discovery grid; never a full-scale policy comparison."""
    profiles = (('unit', 1, 'every', 'forward', 0),
                ('resource', 2, 'every', 'reverse', 1),
                ('midpoint', 4, 'random_block', 'shuffle', 2),
                ('interleaved', 1, 'every', 'forward', 4),
                ('unit', 4, 'every', 'forward', 6),
                ('resource', 1, 'every', 'forward', 7))
    specs = [Pattern(family, radius, origin, stride, subset, order,
                     direction if family.startswith('cone_') or family == 'rays_toward' else 0, seed)
             for family in FAMILIES for radius in (1, 2, 4, 8, 16)
             for origin, stride, subset, order, direction in profiles]
    lines = ['// SPDX-License-Identifier: GPL-3.0-or-later',
             '// Generated by tools/resource_claim_patterns.py --pilot-header; temporary pilot only.',
             '// 240 parameter aliases; K=1,2,4,8,16. Not the full powers/midpoints experiment.',
             '#pragma once', '#include "ResourceClaimPatternEvaluation.h"', '#include <array>',
             'namespace ResourceClaims::Experiment {',
             'struct PilotPattern { const char* name; std::array<PatternView,8> views;',
             'PatternView viewForDirection(unsigned octant) const { return views[octant & 7]; } };']
    keys, rows = set(), []
    for spec in specs:
        views = []
        directional = spec.family.startswith('cone_') or spec.family == 'rays_toward'
        for octant in range(8):
            data = encode(replace(spec, direction=(spec.direction+octant)%8) if directional else spec)
            key = 'pilot_'+hashlib.sha256(data).hexdigest()
            if key not in keys:
                # Reuse the catalog's SoA format without its wrapper namespace.
                lines.extend(header(key, data).decode().splitlines()[3:-1])
                keys.add(key)
            views.append(f'{{{key}_x,{key}_y,{key}_origin}}')
        name = (f'{spec.family}:k{spec.radius}:{spec.origin}:s{spec.stride}:'
                f'{spec.subset}:{spec.order}:d{spec.direction}:seed{spec.seed}')
        rows.append(f'{{"{name}",{{{{'+','.join(views)+'}}},')
    lines.extend(['inline constexpr PilotPattern PilotPatterns[] = {', *rows, '};',
                  'inline std::span<const PilotPattern> pilotPatterns() { return PilotPatterns; }', '}'])
    return ('\n'.join(lines)+'\n').encode()


def write_catalog(output, specs, *, headers=False, max_bytes=256*1024**2, max_variants=100000):
    """Bound disk and resident data: hold only one pattern, plus unique hashes."""
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    total, variants, unique = 0, 0, set()
    complete = False
    try:
        with (output/'patterns.jsonl').open('wb') as manifest:
            for spec in specs:
                if variants >= max_variants:
                    raise ValueError('variant limit reached; narrow the explicit pattern grid')
                data = encode(spec)
                key = hashlib.sha256(data).hexdigest()
                row = json.dumps({'spec': asdict(spec), 'sha256': key,
                                  'probes': len(data)//PROBE.size}, sort_keys=True).encode()+b'\n'
                generated_header = header('pattern_'+key, data) if headers and key not in unique else b''
                added = len(row) + (len(data) if key not in unique else 0) + len(generated_header)
                if total + added > max_bytes:
                    raise ValueError('byte limit reached; incomplete catalog retained')
                if key not in unique:
                    (output/(key+'.bin')).write_bytes(data)
                    if headers:
                        (output/(key+'.h')).write_bytes(generated_header)
                    unique.add(key)
                elif (output/(key+'.bin')).read_bytes() != data:
                    raise ValueError('pattern content hash collision')
                manifest.write(row)
                total += added
                variants += 1
        complete = True
    finally:
        (output/'summary.json').write_text(json.dumps({'schema_version': 1,
            'complete': complete, 'variants': variants, 'unique_patterns': len(unique),
            'payload_bytes': total, 'probe_encoding': '<int16 x,int16 y,uint8 origin>',
            'origins': list(ORIGINS[:3]), 'max_bytes': max_bytes,
            'max_variants': max_variants}, indent=2)+'\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    destination = parser.add_mutually_exclusive_group(required=True)
    destination.add_argument('--output', type=Path)
    destination.add_argument('--pilot-header', type=Path)
    parser.add_argument('--families', nargs='+', choices=FAMILIES, default=list(FAMILIES))
    parser.add_argument('--radii', nargs='+', type=int, default=[0, 1, 2, 3, 4, 6, 8])
    parser.add_argument('--origins', nargs='+', choices=ORIGINS, default=['unit'])
    parser.add_argument('--strides', nargs='+', type=int, default=[1, 2, 3, 4])
    parser.add_argument('--subsets', nargs='+', choices=('every', 'random_block'), default=['every', 'random_block'])
    parser.add_argument('--orders', nargs='+', choices=('forward', 'reverse', 'shuffle'), default=['forward', 'reverse', 'shuffle'])
    parser.add_argument('--directions', nargs='+', type=int, default=[0])
    parser.add_argument('--seed', type=int, default=1)
    parser.add_argument('--headers', action='store_true')
    parser.add_argument('--max-bytes', type=int, default=256*1024**2)
    parser.add_argument('--max-variants', type=int, default=100000)
    args = parser.parse_args()
    if args.pilot_header:
        with args.pilot_header.open('xb') as stream:
            stream.write(pilot_header(args.seed))
        return
    if args.max_bytes <= 0 or args.max_variants <= 0:
        parser.error('limits must be positive')
    specs = specifications(args.families, args.radii, args.origins, args.strides,
                           args.subsets, args.orders, args.directions, args.seed)
    write_catalog(args.output, specs, headers=args.headers,
                  max_bytes=args.max_bytes, max_variants=args.max_variants)


if __name__ == '__main__':
    main()
