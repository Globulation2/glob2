# SPDX-License-Identifier: GPL-3.0-or-later
"""Independent layouts, deterministic split domains, no timing-dependent cases."""
import hashlib
import json

SIZES = (64, 128, 256, 512, 1024)
DEVELOPMENT_FAMILIES = ('patches', 'corridors', 'islands', 'rooms')
HELD_OUT_FAMILIES = ('ridges', 'canals')
FAMILIES = DEVELOPMENT_FAMILIES + HELD_OUT_FAMILIES
STRESS = ((1, 1), (1, 257), (257, 1), (7, 13), (63, 129),
          (127, 513), (513, 257), (2048, 64), (2048, 2048))


def seed_for(split, width, height, index):
    text = f'glob2-gradient-qualification-v1/{split}/{width}/{height}/{index}'
    return int.from_bytes(hashlib.sha256(text.encode()).digest()[:8], 'little')


def manifest(split):
    if split not in ('development', 'final', 'stress'):
        raise ValueError('unknown corpus split')
    shapes = STRESS if split == 'stress' else tuple((n, n) for n in SIZES)
    count = 200 if split == 'final' else 8 if split == 'development' else 2
    families = FAMILIES if split == 'final' else DEVELOPMENT_FAMILIES
    return [dict(id=f'{split}-{w}x{h}-{i:03}', split=split, width=w, height=h,
                 seed=seed_for(split, w, h, i), family=families[i % len(families)])
            for w, h in shapes for i in range(count)]


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def fields(layout):
    """Three chronological workloads per independent layout; never count as maps.

    Each field starts from fresh seeds after topology/cost changes, not from an
    old solved field (deletions invalidate that field). All arrays are immutable
    to the runner. Cost planes describe the expanded/source cell, as in runtime.
    """
    import numpy as np
    rng = np.random.Generator(np.random.PCG64(layout['seed']))
    w, h = layout['width'], layout['height']
    yy, xx = np.indices((h, w))
    scale = int(rng.integers(4, 25)); width = int(rng.integers(1, 5))
    phase_x, phase_y = int(rng.integers(w)), int(rng.integers(h))
    x, y = (xx + phase_x) % w, (yy + phase_y) % h
    coarse = rng.random(((h + scale - 1)//scale, (w + scale - 1)//scale))
    patches = coarse[y//scale, x//scale]
    density = float(rng.uniform(.05, .45)); family = layout['family']
    if family == 'patches':
        blocked = patches < density
    elif family == 'corridors':
        blocked = (x % scale >= width) & (y % scale >= width)
    elif family == 'islands':
        blocked = patches < rng.uniform(.25, .65)
    elif family == 'rooms':
        blocked = ((x % scale == 0) | (y % scale == 0)) & ((x+y) % (scale+3) >= width)
    elif family == 'ridges':
        blocked = ((x + y//scale) % scale < width) & (y % (scale*2) > width)
    elif family == 'canals':
        blocked = ((y + (x//scale)*3) % scale < width) & (x % (scale*3) > width)
    else:
        raise ValueError('unknown topology')
    # Sparse independent openings/obstructions prevent identical tiled layouts.
    perturb = rng.random((h, w))
    blocked = (blocked & (perturb > .035)) | (perturb < .005)
    blocked.flat[0] = False
    classes = np.minimum((patches * 4).astype(np.uint32), 3)
    initial_goals = rng.random((h, w))
    cluster_x, cluster_y = int(rng.integers(w)), int(rng.integers(h))
    for stage in range(3):
        obstacles = blocked.copy()
        if stage >= 1:  # building footprint appears
            bx, by = (phase_x+7) % w, (phase_y+11) % h
            obstacles[by:min(h, by+width+2), bx:min(w, bx+width+2)] = True
        if stage == 2:  # a route opens, terrain costs change
            obstacles[(x+y) % (scale*2) < width] = False
        values = np.where(obstacles, 0, 1).astype(np.uint16)
        if stage == 0:
            goals = initial_goals < .0008
            cap = 65533
            cardinal = np.full((h, w), 10, np.uint32)
        elif stage == 1:
            # Disappearing old resources and dense replacement objectives.
            goals = (initial_goals > .0004) & (initial_goals < .035)
            cap = (10, 20, 40)[layout['seed'] % 3]
            cardinal = np.array([5, 10, 20, 30], np.uint32)[classes]
        else:
            goals = (((xx-cluster_x) % w < max(1, w//5)) &
                     ((yy-cluster_y) % h < max(1, h//5)) & (initial_goals < .08))
            cap = (120, 600, 65533)[layout['seed'] % 3]
            cardinal = np.array([7, 10, 13, 20], np.uint32)[(classes+1) % 4]
        values[goals & ~obstacles] = 65535
        values.flat[0] = 65535
        if stage == 2 and w*h > 2:  # deferred seed, possibly outside the cap
            values.flat[-1] = 65000
        packed = cardinal | ((cardinal*14//10) << 16)
        yield dict(layout=layout, stage=stage, cap=cap, width=w, height=h,
                   seeds=values.reshape(-1), costs=packed.reshape(-1),
                   seed_count=int(np.count_nonzero(values > 1)),
                   cost_classes=int(np.unique(packed).size),
                   minimum_step=int(cardinal.min()),
                   obstacle_count=int(np.count_nonzero(values == 0)))


def qualifying_class(candidate, field):
    # Frozen BEFORE measurements. Width/height/cap are existing runtime metadata.
    # seed_count is recorded by fixture generation, not a free runtime feature.
    large = field['width'] * field['height'] >= 256*256
    if candidate == 'bounded':
        return large and field['cap'] <= 16 * field['minimum_step']
    if candidate == 'global':
        return large and field['cap'] <= 40
    if candidate == 'frontier':
        return large and field['seed_count'] <= max(1, field['width']*field['height']//1000)
    raise ValueError(candidate)
