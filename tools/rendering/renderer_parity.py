#!/usr/bin/env python3
"""Draw a matrix of scenes on every renderer and check that they settle to one image.

Each renderer may take its own path (direct terrain, pages, device composition,
software rasterisation), but after the view has been held still long enough for
streaming and background composition to finish, the GPU-backed renderers
(legacy-gl, portable-sdl, sdl-gpu, programmable-gl) must show the same picture.
For every scene this runner draws each available renderer in a fresh process
and profile, holds a fixed camera for --settle-frames paced frames and captures
the last one. It then builds a consensus image (the per-pixel median of the
GPU renderers) and measures every renderer against it and every GPU pair
against each other. The software renderer is drawn and measured for
information only.

    python3 tools/dev_build.py perf=1 glob2 torus-render-benchmark
    python3 tools/rendering/renderer_parity.py --prepare --quick --label mine   # everyday: ~10 scenes
    python3 tools/rendering/renderer_parity.py --label master                  # renderer PRs: the matrix
    python3 tools/rendering/renderer_parity.py --list-scenes

A scene fails when a GPU renderer is more than --max-over-8 percent of its
pixels (5% by default with HD block compression, 2% without) more than 8 levels from the consensus, or when two GPU
renderers are that far apart. Results go to artifacts/renderer-parity/<label>/:
index.html (every scene's contact sheet, to scroll through), <scene>.png (the
captures side by side, heat maps against the consensus and zoomed crops of the
worst regions, flat terrain and the framed unit or building), report.md,
summary.json and each scene's captures and compressed logs. The exit status is
1 when a scene fails or a GPU renderer fails to run.
"""
import argparse
import concurrent.futures
import gzip
import html
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import threading
import time

try:
    import numpy as np
    from PIL import Image, ImageDraw
except ImportError as error:  # pragma: no cover - environment guidance
    raise SystemExit(f'{error}: renderer_parity.py needs numpy and Pillow; install the development '
                     'requirements: uv pip install -r requirements-dev.txt')

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / 'artifacts/renderer-parity'
SAVE_DIR = ROOT / 'artifacts/render-memory'
# The memory scenario's mid-game save (memory_scenario.py --prepare), and the
# same game played on to tick 40000: fights, damaged buildings and swimmers.
SAVES = {
    'honeycomb': SAVE_DIR / 'honeycomb-256-tick-30000.game.gz',
    'battle': SAVE_DIR / 'honeycomb-256-tick-40000.game.gz',
}
RENDERERS = ('legacy-gl', 'portable-sdl', 'sdl-gpu', 'programmable-gl', 'software')
# Drawn and reported, but outside the parity bar: the software renderer has no
# filtering, draws native artwork only and leaves out several effects. Its
# column still shows a crash or a blank capture.
INFORMATIONAL = ('software',)
PANEL_WIDTH = 160  # GameGUI::RIGHT_MENU_WIDTH, logical pixels
TOP_BAR = 16


def scene(description, source='honeycomb', zoom=1, gui=True, display=1, hd=True, quick=False, focus=None,
          team=None, select=None, tile=None, env=None, map_size=None):
    """One scene: what to load, the camera, the display and the pinned inputs.

    source: a SAVES key, or a map under content/ (fresh, no synthetic units).
    zoom: screen pixels a logical map pixel; 0 is the overview (minimum) zoom.
    gui: the whole game screen (map, HUD panel, minimap, bars) as the local
        team sees it, or False for the bare map with every tile visible.
    display: 2 draws on an emulated 2x (HiDPI) display: a window twice the
        size whose interface stays at the 1x logical size.
    focus: centre a game-screen scene on a building or unit kind, or a tile
        (GLOB2_BENCH_FOCUS); team views the game as that team's player.
    tile: centre a bare-map scene on this tile (map_size gives the map's size).
    """
    return dict(description=description, source=source, zoom=zoom, gui=gui, display=display, hd=hd, quick=quick,
                focus=focus, team=team, select=select, tile=tile, env=env or {}, map_size=map_size)


ZOOMS = {'overview': 0, 'far': 0.25, '0.5x': 0.5, '1x': 1, '1.5x': 1.5, '2x': 2, '3x': 3, '4x': 4}


def matrix():
    scenes = {}
    # The local colony (Honeycomb 256, team 0) at every zoom, on a 1x display with HD artwork.
    for label, zoom in ZOOMS.items():
        scenes[f'colony-{label}'] = scene(f'Game screen at the colony, zoom {label}', zoom=zoom,
                                          select='unit' if label == '1x' else None,
                                          quick=label in ('overview', '1x', '3x'))
    # Either side of the HD sprite policy's threshold (docs/architecture/rendering.md#hd-sprite-policy).
    scenes['colony-0.9x'] = scene('Game screen just below the HD sprite threshold: native frames', zoom=0.9)
    scenes['colony-1.1x'] = scene('Game screen just above the HD sprite threshold: half-resolution HD', zoom=1.1)
    # A 2x display: drawable pixels twice the window's logical size, HUD in logical points.
    for label in ('overview', '0.5x', '1x', '2x', '4x'):
        scenes[f'colony-{label}-hidpi'] = scene(f'Game screen at the colony, zoom {label}, 2x display',
                                                zoom=ZOOMS[label], display=2, quick=label in ('1x', '4x'))
    # HD artwork off.
    for label in ('0.5x', '1x', '2x', '4x'):
        scenes[f'colony-{label}-nohd'] = scene(f'Game screen at the colony, zoom {label}, HD off', zoom=ZOOMS[label],
                                               hd=False, quick=label == '2x')
    for label in ('1x', '2x'):
        scenes[f'colony-{label}-hidpi-nohd'] = scene(f'Game screen at the colony, zoom {label}, 2x display, HD off',
                                                     zoom=ZOOMS[label], display=2, hd=False)
    # Building states and unit actions in the battle save, as their owner sees them.
    entities = {
        'building-site': ('site', 0, 2, 'a construction site'),
        'building-upgrade': ('upgrade', 0, 2, 'a building being upgraded'),
        'building-complete': ('complete', 0, 2, 'a complete building, selected'),
        'building-damaged': ('damaged', 3, 2, 'a damaged building'),
        'flag': ('flag', 1, 1.5, 'a flag and its area'),
        'unit-carrying': ('carrying', 0, 3, 'a worker carrying a resource'),
        'unit-explorer': ('explorer', 0, 3, 'an explorer'),
        'unit-warrior': ('warrior', 0, 3, 'a warrior'),
        'unit-fighting': ('fighting', 1, 3, 'warriors fighting'),
        'unit-swimming': ('swimming', 1, 3, 'a unit swimming'),
    }
    for name, (kind, team, zoom, what) in entities.items():
        scenes[name] = scene(f'Game screen framing {what}, zoom {zoom}', source='battle', zoom=zoom, focus=kind,
                             team=team, select='building' if kind == 'complete' else None,
                             quick=name in ('building-site', 'unit-fighting'))
    for name, zoom in (('building-site', 2), ('unit-fighting', 2), ('unit-carrying', 3)):
        kind, team, _, what = entities[name]
        scenes[f'{name}-hidpi'] = scene(f'Game screen framing {what}, zoom {zoom}, 2x display', source='battle',
                                        zoom=zoom, focus=kind, team=team, display=2)
    for name in ('building-upgrade', 'unit-fighting'):
        kind, team, zoom, what = entities[name]
        scenes[f'{name}-nohd'] = scene(f'Game screen framing {what}, zoom {zoom}, HD off', source='battle',
                                       zoom=zoom, focus=kind, team=team, hd=False)
    # Bare maps with every tile visible: terrain types and their transitions,
    # resources, and the torus wrap seams.
    honeycomb = (256, 256)
    scenes['map-far'] = scene('Bare map at the overview zoom, health bars on', zoom=0, gui=False,
                              env={'GLOB2_BENCH_BARS': '1'})
    scenes['map-areas'] = scene('Bare map at the colony with areas drawn, zoom 1', zoom=1, gui=False,
                                tile=(183, 75), map_size=honeycomb, env={'GLOB2_BENCH_AREAS': '1'})
    # The torus wraps where the map's edges meet: centre on the corner and
    # across each seam, on maps whose land reaches the edges.
    oazis, archipelago = ('maps/Oazis.map.gz', (256, 256)), ('maps/Archipelago.map.gz', (128, 128))
    for name, (path, size), zoom, tile, display, quick in (
            ('map-wrap-1x', oazis, 1, (0, 0), 1, True),
            ('map-wrap-0.5x', archipelago, 0.5, (0, 0), 1, False),
            ('map-wrap-1x-hidpi', oazis, 1, (0, 0), 2, False),
            ('map-seam-top-2x', ('maps/Sand_River.map.gz', (128, 128)), 2, (64, 0), 1, False),
            ('map-seam-left-3x', archipelago, 3, (0, 64), 1, False)):
        where = 'the corner where the torus wraps both ways' if tile == (0, 0) else \
            'the top wrap seam' if tile[1] == 0 else 'the left wrap seam'
        scenes[name] = scene(f'Bare {Path(path).name.split(".")[0]} map centred on {where}, zoom {zoom}'
                             + (', 2x display' if display == 2 else ''), source=path, zoom=zoom, gui=False,
                             tile=tile, map_size=size, display=display, quick=quick)
    # Honeycomb's hexes each hold one terrain mix: stone, sand, forest and
    # fields, shores with algae, and the bridges across the river.
    for name, zoom, tile, what in (('map-hex-stone-2x', 2, (99, 127), 'a stone hex'),
                                   ('map-hex-sand-2x', 2, (69, 75), 'a sand hex'),
                                   ('map-hex-fields-1.5x', 1.5, (86, 49), 'a wooded hex beside fields'),
                                   ('map-bridge-2x', 2, (112, 104), 'a bridge over the river'),
                                   ('map-shore-algae-2x', 2, (30, 41), 'a shore with algae')):
        scenes[name] = scene(f'Bare Honeycomb map at {what}, zoom {zoom}', zoom=zoom, gui=False, tile=tile,
                             map_size=honeycomb)
    maps = {
        # name: (map, size, [(zoom label, tile)])
        'oazis': ('maps/Oazis.map.gz', (256, 256), [('overview', None), ('1x', (128, 128)), ('2x', (64, 96))]),
        'archipelago': ('maps/Archipelago.map.gz', (128, 128), [('1x', (64, 64)), ('3x', (40, 30))]),
        'sand-river': ('maps/Sand_River.map.gz', (128, 128), [('1x', (64, 64)), ('2x', (96, 40))]),
        'wild-river': ('maps/Wild_River.map.gz', (256, 64), [('1x', (128, 32)), ('1.5x', (0, 0))]),
        'mazury': ('maps/Mazury.map.gz', (128, 128), [('1.5x', (64, 64))]),
        'small-for-two': ('maps/SmallForTwo.map.gz', (64, 64), [('overview', None), ('4x', (32, 32))]),
    }
    for name, (path, size, views) in maps.items():
        for label, tile in views:
            scenes[f'map-{name}-{label}'] = scene(f'Bare {Path(path).name.split(".")[0]} map, zoom {label}'
                                                  + (f', centred on tile {tile[0]},{tile[1]}' if tile else ''),
                                                  source=path, zoom=ZOOMS[label], gui=False, tile=tile, map_size=size,
                                                  quick=name == 'oazis' and label == '1x')
    scenes['map-oazis-1x-hidpi'] = scene('Bare Oazis map, zoom 1, 2x display', source='maps/Oazis.map.gz', zoom=1,
                                         gui=False, tile=(128, 128), map_size=(256, 256), display=2)
    scenes['map-archipelago-3x-hidpi'] = scene('Bare Archipelago map, zoom 3, 2x display',
                                               source='maps/Archipelago.map.gz', zoom=3, gui=False, tile=(40, 30),
                                               map_size=(128, 128), display=2)
    scenes['map-oazis-1x-nohd'] = scene('Bare Oazis map, zoom 1, HD off', source='maps/Oazis.map.gz', zoom=1,
                                        gui=False, tile=(128, 128), map_size=(256, 256), hd=False)
    scenes['map-archipelago-3x-nohd'] = scene('Bare Archipelago map, zoom 3, HD off', source='maps/Archipelago.map.gz',
                                              zoom=3, gui=False, tile=(40, 30), map_size=(128, 128), hd=False)
    scenes['gui-small-map'] = scene('Game screen on the small SmallForTwo map at its start, zoom 1',
                                    source='maps/SmallForTwo.map.gz', zoom=1)
    return scenes


SCENES = matrix()


def newest(relative):
    builds = [path for path in (ROOT / 'build').glob('*/client/perf*/' + relative) if path.is_file()]
    builds += [path for path in (ROOT / 'build').glob('*/client/perf/o3/' + relative) if path.is_file()]
    builds += [path for path in (ROOT / 'build').glob('*/client/perf/' + relative + '.exe') if path.is_file()]
    if not builds:
        raise SystemExit(f'No perf build of {relative}; run: python3 tools/dev_build.py perf=1 torus-render-benchmark')
    return max(builds, key=lambda path: path.stat().st_mtime)


def window_size(args, spec):
    width, height = (int(v) for v in args.window_size.split('x'))
    if spec['display'] == 2 and args.hidpi == 'emulate':
        return width * 2, height * 2
    return width, height


def scene_environment(args, name, directory, renderer):
    """The pinned inputs: device tier, HD artwork, desktop HUD, display scale, a fresh profile."""
    spec = SCENES[name]
    profile = directory / f'profile-{renderer}'
    shutil.rmtree(profile, ignore_errors=True)
    env = dict(os.environ)
    for key in [key for key in env if key.startswith('GLOB2_BENCH_') or key == 'GLOB2_UI_SCALE']:
        del env[key]
    hd = spec['hd'] if args.hd == 'scene' else args.hd == 'on'
    env.update({
        'GLOB2_USER_DATA_DIR': str(profile),
        'GLOB2_DEVICE_TIER': args.device_tier,
        'GLOB2_BENCH_HD': '1' if hd else '0',
        'GLOB2_HD_BLOCK_COMPRESSION': '1' if args.compression == 'on' else '0',
        'GLOB2_MOBILE_UI': '0',
        'GLOB2_BENCH_FLAT': '1',
        'GLOB2_BENCH_MODE': '2D no clouds',
        'GLOB2_BENCH_FRAMES': '1',
        'GLOB2_BENCH_WARMUP_FRAMES': str(args.settle_frames),
        'GLOB2_BENCH_FRAME_INTERVAL_MS': str(args.interval_ms),
        'GLOB2_BENCH_PAUSE_PRESENTATION': '1',
        'GLOB2_BENCH_CAPTURE': str(directory / f'{renderer}.ppm'),
        # The hidden window never reaches the screen: a swap that waits for
        # vertical blank on it (NVIDIA, Mesa) costs a minute of start-up
        # progress frames and changes nothing in the capture.
        '__GL_SYNC_TO_VBLANK': '0',
        'vblank_mode': '0',
    })
    if spec['source'] in SAVES:
        env['GLOB2_BENCH_GAME'] = str(SAVES[spec['source']])
    else:
        env['GLOB2_BENCH_MAP'] = spec['source']
        env['GLOB2_BENCH_UNITS'] = '0'
    if spec['zoom']:
        env['GLOB2_BENCH_ZOOM'] = str(spec['zoom'])
    if spec['gui']:
        env['GLOB2_BENCH_GUI'] = '1'
    if spec['focus']:
        env['GLOB2_BENCH_FOCUS'] = spec['focus']
    if spec['team'] is not None:
        env['GLOB2_BENCH_TEAM'] = str(spec['team'])
    if spec['select']:
        env['GLOB2_BENCH_SELECT'] = spec['select']
    if spec['tile']:
        # The bare map's fixed camera takes its top-left tile: centre the tile
        # in the map viewport (the window less the panel's width).
        width, height = (int(v) for v in args.window_size.split('x'))
        map_w, map_h = spec['map_size']
        visible_x = (width - PANEL_WIDTH) / 32 / spec['zoom']
        visible_y = height / 32 / spec['zoom']
        env['GLOB2_BENCH_PAN_X'] = str(int(spec['tile'][0] - visible_x / 2) % map_w)
        env['GLOB2_BENCH_PAN_Y'] = str(int(spec['tile'][1] - visible_y / 2) % map_h)
    if spec['display'] == 2 and args.hidpi == 'emulate':
        env['GLOB2_UI_SCALE'] = '2'
    env.update(spec['env'])
    if args.shader_dir:
        env['GLOB2_RENDERER_SHADER_DIR'] = str(Path(args.shader_dir).resolve())
    if platform.system() == 'Linux' and 'SDL_VIDEO_DRIVER' not in env and env.get('DISPLAY'):
        env['SDL_VIDEO_DRIVER'] = 'x11'
    return env


def run_renderer(args, binary, name, directory, renderer):
    """Draw one scene on one renderer; returns its status record."""
    env = scene_environment(args, name, directory, renderer)
    capture = directory / f'{renderer}.ppm'
    capture.unlink(missing_ok=True)
    log = directory / f'{renderer}.log'
    width, height = window_size(args, SCENES[name])
    command = [str(binary), '--renderer', renderer, '--no-fullscreen', '--mute', '--window-size', f'{width}x{height}']
    started = time.monotonic()
    try:
        with log.open('w') as output:
            code = subprocess.run(command, cwd=ROOT, env=env, stdout=output, stderr=subprocess.STDOUT,
                                  timeout=args.timeout).returncode
    except subprocess.TimeoutExpired:
        code = 'timeout'
    text = log.read_text(errors='replace')
    with gzip.open(str(log) + '.gz', 'wt') as packed:
        packed.write(text)
    log.unlink()
    shutil.rmtree(directory / f'profile-{renderer}', ignore_errors=True)
    record = {'renderer': renderer, 'log': str(log.relative_to(ROOT)) + '.gz',
              'seconds': round(time.monotonic() - started, 1)}
    backend = re.search(r'^BACKEND requested=(\S+) active=(\S+) fallback_reason=(.*)$', text, re.M)
    settings = re.search(r'^BENCH_SETTINGS (.*)$', text, re.M)
    if settings:
        record['settings'] = dict(field.split('=', 1) for field in settings.group(1).split())
    focus = re.search(r'^BENCH_FOCUS (.*)$', text, re.M)
    if focus:
        record['focus'] = focus.group(1)
    if backend and backend.group(2) != renderer:
        record.update(status='unavailable', reason=f'fell back to {backend.group(2)}: {backend.group(3).strip()}')
        return record
    if code != 0 or not capture.exists():
        tail = '; '.join(line for line in text.strip().splitlines()[-3:])
        if 'GLOB2_BENCH_FOCUS found no' in text:
            record.update(status='setup', reason=re.search(r'GLOB2_BENCH_FOCUS found no \S+', text).group(0))
        elif not backend and code != 0 and re.search(r'unavailable|not available|cannot create|no shader', text, re.I):
            record.update(status='unavailable', reason=tail[-300:])
        else:
            record.update(status='failed', reason=f'exit {code}: {tail[-300:]}')
        return record
    image = Image.open(capture).convert('RGB')
    image.save(directory / f'{renderer}.png')
    capture.unlink()
    record.update(status='ok', size=list(image.size))
    return record


def reuse_capture(previous, name, directory, renderer):
    """The record of an earlier run's capture, for --reanalyse."""
    old = next((r for r in previous.get(name, {}).get('renderers', []) if r['renderer'] == renderer), {})
    record = {key: value for key, value in old.items() if key not in ('consensus', 'reference')}
    record.setdefault('renderer', renderer)
    record.setdefault('seconds', 0)
    if (directory / f'{renderer}.png').exists():
        record.update(status='ok', size=list(Image.open(directory / f'{renderer}.png').size))
    elif record.get('status') in (None, 'ok'):
        record.update(status='failed', reason='no capture to reanalyse')
    return record


def difference(reference, image, shift):
    """Per-pixel largest channel difference, forgiving moves of up to `shift` pixels.

    Each pixel takes the smallest difference to the other image's pixels within
    the shift window, in both directions, so an edge one pixel out of place
    does not count while a missing sprite or wrong colour does.
    """
    a = reference.astype(np.int16)
    b = image.astype(np.int16)

    def nearest(source, target):
        best = None
        height, width = source.shape[:2]
        padded = np.pad(target, ((shift, shift), (shift, shift), (0, 0)), mode='edge')
        for dy in range(-shift, shift + 1):
            for dx in range(-shift, shift + 1):
                window = padded[shift + dy:shift + dy + height, shift + dx:shift + dx + width]
                delta = np.abs(source - window).max(axis=2)
                best = delta if best is None else np.minimum(best, delta)
        return best

    if shift <= 0:
        return np.abs(a - b).max(axis=2)
    return np.maximum(nearest(a, b), nearest(b, a))


def metrics(delta):
    pixels = delta.size
    return {'over_8_percent': round(100.0 * np.count_nonzero(delta > 8) / pixels, 3),
            'over_32_percent': round(100.0 * np.count_nonzero(delta > 32) / pixels, 3),
            'mean': round(float(delta.mean()), 3)}


def heat_map(reference, delta):
    """A dimmed grey reference with differences over 8 levels in yellow to red."""
    grey = reference.astype(np.float32).mean(axis=2, keepdims=True) * 0.35
    out = np.repeat(grey, 3, axis=2)
    strength = np.clip(delta.astype(np.float32) / 64.0, 0, 1)
    mask = delta > 8
    heat = np.stack([np.full_like(strength, 255), 255 * (1 - strength), np.zeros_like(strength)], axis=2)
    out[mask] = heat[mask]
    return out.astype(np.uint8)


def window_sums(mask, size):
    """Sum of `mask` over every size-by-size window (top-left indexed)."""
    integral = np.pad(mask.astype(np.int64).cumsum(0).cumsum(1), ((1, 0), (1, 0)))
    return integral[size:, size:] - integral[:-size, size:] - integral[size:, :-size] + integral[:-size, :-size]


def worst_regions(mask, size, count, box):
    """Up to `count` non-overlapping windows with the most differing pixels, inside box."""
    x0, y0, x1, y1 = box
    sums = window_sums(mask[y0:y1, x0:x1], size).astype(np.float64)
    regions = []
    for _ in range(count):
        if sums.size == 0 or sums.max() <= 0:
            break
        y, x = np.unravel_index(int(sums.argmax()), sums.shape)
        regions.append((x0 + int(x), y0 + int(y), int(sums[y, x])))
        sums[max(0, y - size + 1):y + size, max(0, x - size + 1):x + size] = -1
    return regions


def flat_region(image, size, box):
    """A terrain window without sprites: lit, textured, with the fewest strong edges."""
    x0, y0, x1, y1 = box
    lum = image[y0:y1, x0:x1].astype(np.float32) @ np.array([0.299, 0.587, 0.114], np.float32)
    gy, gx = np.gradient(lum)
    grad = np.hypot(gx, gy)
    edges = window_sums(grad > 40, size)
    texture = window_sums(grad > 3, size)
    dark = window_sums(lum < 30, size)
    area = size * size
    score = edges.astype(np.float64) - texture / (4.0 * area)
    score[dark > area // 20] = np.inf
    stride = max(1, size // 4)
    grid = score[::stride, ::stride]
    if not np.isfinite(grid).any():
        return None
    y, x = np.unravel_index(int(np.argmin(grid)), grid.shape)
    return x0 + int(x) * stride, y0 + int(y) * stride


def sharpness(image, mask):
    """Fine detail where `mask` is set: the mean squared difference of each pixel's
    luminance from its 3x3 neighbourhood. HD artwork drawn at its density holds
    several times the detail of native artwork magnified to the same size."""
    lum = image.astype(np.float32) @ np.array([0.299, 0.587, 0.114], np.float32)
    padded = np.pad(lum, 1, mode='edge')
    height, width = lum.shape
    blur = sum(padded[dy:dy + height, dx:dx + width] for dy in range(3) for dx in range(3)) / 9
    return float(np.mean(((lum - blur) ** 2)[mask]))


def residency_split(args, gpu, images, deltas):
    """Splits the GPU renderers into sharp and soft groups where they disagree.

    A renderer that draws native (low-resolution) artwork where the others draw
    HD, or the other way round, differs from them over whole stretches of
    terrain or sprites. The median consensus sides with the majority even when
    the majority is the one missing HD, so the pixel bar alone can blame the
    one renderer that is right. Comparing the detail each holds over the pixels
    that differ names the split directly. Returns {renderer: detail relative to
    the sharpest} and the sharp group, or an empty group without a clear split.
    """
    mask = np.zeros(next(iter(deltas.values())).shape, bool)
    for renderer in gpu:
        mask |= deltas[renderer] > 8
    if mask.mean() * 100 < args.max_over_8 / 4:
        return {}, []
    energy = {renderer: sharpness(images[renderer], mask) for renderer in gpu}
    top = max(energy.values())
    if top <= 0:
        return {}, []
    relative = {renderer: round(value / top, 3) for renderer, value in energy.items()}
    ordered = sorted(gpu, key=lambda r: -relative[r])
    # The widest drop in detail between neighbours in that order, if any is
    # steeper than the split ratio, separates the sharp group from the soft.
    drops = [(relative[ordered[i + 1]] / max(relative[ordered[i]], 1e-9), i) for i in range(len(ordered) - 1)]
    ratio, index = min(drops)
    if ratio >= args.sharpness_split:
        return relative, []
    return relative, ordered[:index + 1]


def compare_scene(args, name, directory, records):
    """Consensus and pairwise distances; marks records and returns the scene verdict."""
    available = {record['renderer']: record for record in records if record['status'] == 'ok'}
    images = {renderer: np.asarray(Image.open(directory / f'{renderer}.png').convert('RGB'))
              for renderer in available}
    gpu = [renderer for renderer in available if renderer not in INFORMATIONAL]
    shapes = {images[renderer].shape for renderer in gpu}
    result = {'gpu': gpu, 'pairs': {}, 'consensus': {}, 'verdict': 'ok', 'problems': []}
    if len(shapes) > 1:
        sizes = ', '.join(f'{r} {images[r].shape[1]}x{images[r].shape[0]}' for r in gpu)
        result.update(verdict='fail', problems=[f'capture sizes differ: {sizes}'])
        return result, images, None, {}
    if not gpu:
        result.update(verdict='fail', problems=['no GPU renderer drew the scene'])
        return result, images, None, {}
    consensus = np.median(np.stack([images[r] for r in gpu]), axis=0).round().astype(np.uint8)
    deltas = {}
    for renderer in available:
        if images[renderer].shape != consensus.shape:
            available[renderer].update(status='failed', reason='capture size differs from the GPU renderers')
            continue
        deltas[renderer] = difference(consensus, images[renderer], args.shift)
        available[renderer]['consensus'] = result['consensus'][renderer] = metrics(deltas[renderer])
    for i, first in enumerate(gpu):
        for second in gpu[i + 1:]:
            result['pairs'][f'{first}|{second}'] = metrics(difference(images[first], images[second], args.shift))
    for renderer in gpu:
        if result['consensus'][renderer]['over_8_percent'] > args.max_over_8:
            result['problems'].append(f'{renderer} {result["consensus"][renderer]["over_8_percent"]:.2f}% '
                                      'from the consensus')
    for pair, value in result['pairs'].items():
        if value['over_8_percent'] > args.max_over_8:
            result['problems'].append(f'{pair.replace("|", " vs ")} {value["over_8_percent"]:.2f}% apart')
    if result['pairs']:
        worst = max(result['pairs'], key=lambda pair: result['pairs'][pair]['over_8_percent'])
        result['worst_pair'] = {'pair': worst, **result['pairs'][worst]}
    if len(gpu) > 2 and result['worst_pair']['over_8_percent'] > args.max_over_8 / 4:
        # Group the renderers that agree within the bar. In a three-to-one
        # split the odd one out is the outlier; a two-to-two split has none,
        # and the median consensus sits between the groups, so name both.
        groups = [{r} for r in gpu]
        for pair, value in result['pairs'].items():
            if value['over_8_percent'] <= args.max_over_8:
                first, second = (next(g for g in groups if r in g) for r in pair.split('|'))
                if first is not second:
                    first |= second
                    groups.remove(second)
        groups.sort(key=lambda g: (-len(g), sorted(g)))
        result['groups'] = [sorted(g, key=gpu.index) for g in groups]
        if len(groups) == 1:
            # Chained agreement: name the furthest from the consensus only if
            # some pair is still over the bar.
            worst = max(gpu, key=lambda r: result['consensus'][r]['over_8_percent'])
            result['outlier'] = worst if result['worst_pair']['over_8_percent'] > args.max_over_8 else ''
        elif len(groups) == 2 and len(groups[1]) == 1 and len(groups[0]) > 1:
            result['outlier'] = next(iter(groups[1]))
        else:
            result['outlier'] = 'split: ' + ' / '.join('+'.join(g) for g in result['groups'])
    if len(gpu) > 1 and result['problems']:
        result['sharpness'], sharp = residency_split(args, gpu, images, deltas)
        if sharp:
            soft = [r for r in gpu if r not in sharp]
            result['residency'] = {'sharp': sharp, 'soft': soft}
            # Above a density of one with HD on, the policy has every renderer
            # draw HD: a split there is HD present on some and missing on others.
            spec = SCENES[name]
            hd_scene = spec['hd'] and spec['zoom'] * spec['display'] > 1
            kind = 'HD residency mismatch' if hd_scene else 'detail split'
            text = f'{kind}: sharp {"+".join(sharp)} / soft {"+".join(soft)}'
            result['problems'].append(text + ' (detail ' + ', '.join(
                f'{r} {result["sharpness"][r]:.2f}' for r in sharp + soft) + ')')
            # The consensus follows the majority; when the majority is the soft
            # group it blames the renderers that do draw HD.
            if len(sharp) < len(soft):
                text += f' (the consensus follows the soft {"+".join(soft)})'
            result['outlier'] = text
    if result['problems']:
        result['verdict'] = 'fail'
    return result, images, consensus, deltas


def label(draw, xy, text, fill=(255, 255, 255)):
    draw.rectangle([xy[0] - 2, xy[1] - 1, xy[0] + 7 * len(text) + 2, xy[1] + 12], fill=(0, 0, 0))
    draw.text(xy, text, fill=fill)


def contact_sheet(args, name, directory, records, result, images, consensus, deltas):
    """Captures side by side, heat maps against the consensus, and zoomed crops."""
    spec = SCENES[name]
    gpu = [r for r in result['gpu'] if r in deltas]
    if consensus is None or not gpu:
        return None
    height, width = consensus.shape[:2]
    scale = spec['display'] if args.hidpi == 'emulate' else 1
    shown_w, shown_h = width // scale, height // scale
    crop = args.crop * scale
    zoom_to = args.crop * args.crop_zoom
    pad, header = 8, 54
    info = [r for r in deltas if r in INFORMATIONAL]
    # Regions: the densest differences from the consensus over all GPU
    # renderers, a stretch of flat terrain and the framed unit or building.
    map_box = (0, (TOP_BAR if spec['gui'] else 0) * scale, width - PANEL_WIDTH * scale, height)
    combined = np.zeros(consensus.shape[:2], bool)
    for renderer in gpu:
        combined |= deltas[renderer] > 8
    regions = [('worst #%d (%d px)' % (i + 1, count), x, y)
               for i, (x, y, count) in enumerate(worst_regions(combined, crop, args.worst_crops, (0, 0, width, height)))]
    flat = flat_region(consensus, crop, map_box)
    if flat:
        regions.append(('flat terrain', *flat))
    centre_x = (map_box[0] + map_box[2]) // 2 - crop // 2
    centre_y = (map_box[1] + map_box[3]) // 2 - crop // 2
    regions.append(('view centre' + (f' ({spec["focus"]})' if spec['focus'] else ''), centre_x, centre_y))
    result['regions'] = [{'label': text, 'x': x, 'y': y, 'size': crop} for text, x, y in regions]
    columns = gpu + info + ['consensus', 'difference']
    row_a = len(gpu) * (shown_w + pad)
    row_b = (len(gpu) + 2 * len(info)) * (shown_w // 2 + pad)
    row_c = len(columns) * (zoom_to + pad) + 160
    sheet_w = max(row_a, row_b, row_c) + pad
    sheet_h = header + (shown_h + 20) + (shown_h // 2 + 20) + len(regions) * (zoom_to + 20) + pad
    sheet = Image.new('RGB', (sheet_w, sheet_h), (24, 24, 32))
    draw = ImageDraw.Draw(sheet)
    verdict = 'FAIL: ' + '; '.join(result['problems']) if result['problems'] else 'ok'
    label(draw, (pad, 6), f'{name}: {spec["description"]}')
    label(draw, (pad, 22), f'{verdict}  (bar: {args.max_over_8}% of pixels more than 8 levels; shift {args.shift})',
          fill=(255, 120, 120) if result['problems'] else (140, 255, 140))
    if result.get('outlier'):
        label(draw, (pad, 38), f'outlier: {result["outlier"]}', fill=(255, 220, 120))
    y = header
    for column, renderer in enumerate(gpu):
        x = pad + column * (shown_w + pad)
        image = Image.fromarray(images[renderer])
        if scale != 1:
            image = image.resize((shown_w, shown_h), Image.BOX)
        sheet.paste(image, (x, y + 16))
        value = result['consensus'][renderer]['over_8_percent']
        label(draw, (x, y), f'{renderer}: {value:.2f}% from consensus' + (' (shown at 1/2)' if scale != 1 else ''),
              fill=(255, 120, 120) if value > args.max_over_8 else (255, 255, 255))
    y += shown_h + 20
    half = (shown_w // 2, shown_h // 2)
    panels = [(f'{r} vs consensus', heat_map(consensus, deltas[r])) for r in gpu]
    for renderer in info:
        panels += [(f'{renderer} (info)', images[renderer]), (f'{renderer} vs consensus (info)',
                                                              heat_map(consensus, deltas[renderer]))]
    for column, (text, pixels) in enumerate(panels):
        x = pad + column * (half[0] + pad)
        sheet.paste(Image.fromarray(pixels).resize(half, Image.BOX), (x, y + 16))
        label(draw, (x, y), text)
    y += half[1] + 20
    worst_gpu = max(gpu, key=lambda r: result['consensus'][r]['over_8_percent'])
    for text, rx, ry in regions:
        label(draw, (pad, y + 2), text, fill=(255, 220, 120))
        label(draw, (pad, y + 18), f'at {rx},{ry}')
        label(draw, (pad, y + 34), f'{crop}x{crop} px')
        for column, renderer in enumerate(columns):
            x = pad + 160 + column * (zoom_to + pad)
            if renderer == 'consensus':
                pixels = consensus
            elif renderer == 'difference':
                pixels = heat_map(consensus, deltas[worst_gpu])
            else:
                pixels = images[renderer]
            tile = Image.fromarray(pixels[ry:ry + crop, rx:rx + crop]).resize((zoom_to, zoom_to), Image.NEAREST)
            sheet.paste(tile, (x, y + 16))
            label(draw, (x, y), f'{worst_gpu} diff' if renderer == 'difference' else renderer)
        y += zoom_to + 20
    path = directory.parent / f'{name}.png'
    sheet.save(path, optimize=False, compress_level=6)
    return path


def table(results):
    gpu = [r for r in RENDERERS if any(r in result.get('consensus', {}) for result in results.values())]
    lines = ['| Scene | ' + ' | '.join(gpu) + ' | Worst pair | Outlier | Result |',
             '| --- | ' + ' | '.join('---:' for _ in gpu) + ' | --- | --- | --- |']
    for name, result in results.items():
        cells = []
        for renderer in gpu:
            record = next((r for r in result['renderers'] if r['renderer'] == renderer), None)
            if record is None:
                cells.append('')
            elif record['status'] != 'ok' or renderer not in result.get('consensus', {}):
                cells.append(record['status'])
            else:
                value = result['consensus'][renderer]['over_8_percent']
                text = f'{value:.2f}%'
                if renderer in INFORMATIONAL:
                    text += ' (info)'
                elif value > result['bar']:
                    text = f'**{text}**'
                cells.append(text)
        worst = result.get('worst_pair')
        worst_text = f'{worst["pair"].replace("|", " / ")} {worst["over_8_percent"]:.2f}%' if worst else ''
        verdict = 'ok' if result['verdict'] == 'ok' else '**fail**'
        lines.append(f'| {name} | ' + ' | '.join(cells) + f' | {worst_text} | {result.get("outlier", "")} | {verdict} |')
    return '\n'.join(lines)


def write_index(output, results, args, wall):
    rows = []
    for name, result in results.items():
        cls = 'ok' if result['verdict'] == 'ok' else 'fail'
        consensus = ', '.join(f'{r} {v["over_8_percent"]:.2f}%' for r, v in result.get('consensus', {}).items())
        problems = html.escape('; '.join(result['problems'])) or 'ok'
        sheet = result.get('sheet')
        image = (f'<a href="{html.escape(Path(sheet).name)}"><img loading="lazy" src="{html.escape(Path(sheet).name)}" '
                 f'alt="{html.escape(name)}"></a>' if sheet else '<p>no sheet</p>')
        rows.append(f'<section id="{html.escape(name)}" class="{cls}"><h2>{html.escape(name)} '
                    f'<span>{problems}</span></h2><p>{html.escape(result["description"])}</p>'
                    f'<p class="metrics">from consensus: {html.escape(consensus)}; outlier: '
                    f'{html.escape(result.get("outlier", ""))}</p>{image}</section>')
    links = ' '.join(f'<a class="{"ok" if r["verdict"] == "ok" else "fail"}" href="#{html.escape(n)}">{html.escape(n)}</a>'
                     for n, r in results.items())
    failed = sum(r['verdict'] != 'ok' for r in results.values())
    page = f'''<!doctype html><meta charset="utf-8"><title>Renderer parity {html.escape(args.label)}</title>
<style>body{{background:#16161c;color:#ddd;font:14px sans-serif;margin:16px}} img{{max-width:100%}}
a{{color:#9cf}} a.fail,.fail h2 span{{color:#f88}} a.ok{{color:#8d8}} section{{border-top:1px solid #444;padding:8px 0}}
h2 span{{font-size:13px;font-weight:normal;margin-left:12px}} .metrics{{color:#aaa}} nav a{{margin-right:8px;line-height:1.8}}</style>
<h1>Renderer parity: {html.escape(args.label)}</h1>
<p>{len(results)} scenes, {failed} failing; bar {args.max_over_8}% of pixels more than 8 levels from the GPU consensus
or between two GPU renderers; software is information only. {html.escape(platform.platform())}; wall time {wall:.0f} s.</p>
<nav>{links}</nav>
{''.join(rows)}'''
    (output / 'index.html').write_text(page)


def generate_shaders(glslang, directory):
    command = [sys.executable, str(ROOT / 'tools/shaders/generate.py'), '--output', str(directory),
               '--glslang', str(glslang)]
    subprocess.run(command, cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    return directory


def prepare(sources):
    """Make the standard saves: the memory scenario's, then the same game played on."""
    sys.path.insert(0, str(ROOT / 'tools/rendering'))
    import memory_scenario
    glob2 = memory_scenario.newest('src/glob2')
    if 'honeycomb' in sources or 'battle' in sources:
        if not SAVES['honeycomb'].exists():
            memory_scenario.prepare(glob2)
    if 'battle' in sources and not SAVES['battle'].exists():
        work = SAVE_DIR / 'prepare-battle'
        shutil.rmtree(work, ignore_errors=True)
        subprocess.run([glob2, 'game', 'run', '--load-game', SAVES['honeycomb'], '--ticks', '40000', '--save', 'final',
                        '--output-dir', work], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
        (work / 'final.game.gz').replace(SAVES['battle'])
        shutil.rmtree(work, ignore_errors=True)


def select_scenes(args):
    if args.scenes:
        names = [name for name in args.scenes.split(',') if name]
        unknown = [name for name in names if name not in SCENES]
        if unknown:
            raise SystemExit(f'Unknown scenes {unknown}; see --list-scenes')
    else:
        names = [name for name, spec in SCENES.items() if spec['quick'] or not args.quick]
    if args.filter:
        names = [name for name in names if re.search(args.filter, name)]
    if not names:
        raise SystemExit('No scenes selected')
    return names


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--label', default='latest', help='Result name under artifacts/renderer-parity/')
    parser.add_argument('--renderers', default=','.join(RENDERERS), help='Comma-separated renderers to draw')
    parser.add_argument('--scenes', help='Comma-separated scenes (default: the whole matrix; see --list-scenes)')
    parser.add_argument('--quick', action='store_true', help='Only the everyday subset (about ten scenes)')
    parser.add_argument('--filter', help='Regular expression the scene names must match')
    parser.add_argument('--list-scenes', action='store_true')
    parser.add_argument('--reference', help='Also measure every renderer against this one (a pairwise column)')
    parser.add_argument('--prepare', action='store_true', help='Generate the standard saves first if they are missing')
    parser.add_argument('--binary', help='torus-render-benchmark to run (default: newest perf build)')
    parser.add_argument('--window-size', default='1280x800', help='Logical window size (2x scenes double it)')
    parser.add_argument('--hidpi', choices=('emulate', 'native'),
                        default='native' if platform.system() == 'Darwin' else 'emulate',
                        help='2x scenes: emulate a 2x display (a doubled window with GLOB2_UI_SCALE=2, the default '
                             'except on macOS) or draw them like the others on a real HiDPI display (native)')
    parser.add_argument('--settle-frames', type=int, default=600,
                        help='Frames the camera is held before the capture, so streaming finishes')
    parser.add_argument('--interval-ms', type=int, default=16, help='Pause between settle frames, as a display paces them')
    parser.add_argument('--device-tier', default='high', choices=('low', 'mid', 'high'))
    parser.add_argument('--hd', choices=('scene', 'on', 'off'), default='scene',
                        help='HD artwork as each scene says (default), or pinned on or off for all')
    parser.add_argument('--shift', type=int, default=1,
                        help='Pixels a difference may move and still match (default 1)')
    parser.add_argument('--max-over-8', type=float, default=5.0,
                        help='Bar with HD block compression on: percent of pixels more than 8 levels from the '
                             'consensus or between two GPU renderers (default 5.0)')
    parser.add_argument('--max-over-8-uncompressed', type=float, default=2.0,
                        help='Bar with HD block compression off (default 2.0): what the renderers must agree on '
                             'before compression differences are allowed for')
    parser.add_argument('--compression', choices=('both', 'on', 'off'), default='both',
                        help='HD block compression for the drawn scenes: both (default) draws every scene with '
                             'it off (bar --max-over-8-uncompressed) and on (bar --max-over-8) under '
                             '<label>/off and <label>/on, and measures each renderer against itself across '
                             'the two, so any difference beyond compression shows; on or off draws one pass')
    parser.add_argument('--shader-dir', default=os.environ.get('GLOB2_RENDERER_SHADER_DIR'),
                        help='SDL GPU / programmable GL shader directory (GLOB2_RENDERER_SHADER_DIR)')
    parser.add_argument('--glslang', help='Pinned glslangValidator: generate shaders into the output first')
    parser.add_argument('--jobs', type=int, default=6,
                        help='Renderer processes at once, across scenes (default 6; captures do not depend on it)')
    parser.add_argument('--timeout', type=int, default=300, help='Seconds one renderer may take for one scene (default 300)')
    parser.add_argument('--crop', type=int, default=64, help='Crop size in logical pixels (default 64)')
    parser.add_argument('--crop-zoom', type=int, default=4, help='Nearest-neighbour magnification of crops')
    parser.add_argument('--worst-crops', type=int, default=3, help='Crops of the densest differences per scene')
    parser.add_argument('--sharpness-split', type=float, default=0.6,
                        help='Where a failing scene\'s renderers disagree, flag an HD residency mismatch when one '
                             'group holds less than this fraction of the detail of the next sharper one (default 0.6)')
    parser.add_argument('--reanalyse', action='store_true',
                        help='Compare the captures already under the label again instead of drawing them')
    args = parser.parse_args()
    if args.list_scenes:
        for name, spec in SCENES.items():
            print(f'{name:28} {"quick " if spec["quick"] else "      "}{spec["description"]}')
        return 0
    renderers = [name for name in args.renderers.split(',') if name]
    names = select_scenes(args)
    sources = {SCENES[name]['source'] for name in names}
    if args.prepare:
        prepare(sources)
    missing = [str(SAVES[source]) for source in sources if source in SAVES and not SAVES[source].exists()]
    if missing:
        raise SystemExit(f'{", ".join(missing)} missing; pass --prepare to generate the standard saves '
                         '(needs a perf build of glob2)')
    if args.compression != 'both':
        if args.compression == 'off':
            args.max_over_8 = args.max_over_8_uncompressed
        failures = run_pass(args, names, renderers, OUTPUT / args.label)
        return 1 if failures else 0
    passes = {}
    for mode, bar in (('off', args.max_over_8_uncompressed), ('on', args.max_over_8)):
        pass_args = argparse.Namespace(**vars(args))
        pass_args.compression, pass_args.max_over_8 = mode, bar
        print(f'== HD block compression {mode} (bar {bar}%)', flush=True)
        passes[mode] = run_pass(pass_args, names, renderers, OUTPUT / args.label / mode)
    changed = compression_report(args, names, renderers, OUTPUT / args.label)
    failed = bool(passes['off'] or passes['on'] or changed)
    print(f'\nHD block compression off: {"FAILED " + ", ".join(passes["off"]) if passes["off"] else "ok"}; '
          f'on: {"FAILED " + ", ".join(passes["on"]) if passes["on"] else "ok"}; '
          f'renderers changed beyond the bar by compression: {", ".join(changed) or "none"}')
    return 1 if failed else 0


def compression_report(args, names, renderers, root):
    """Each renderer's capture with HD block compression on, against its own capture without it.

    Renderers that never compress (portable SDL, software) must match themselves
    exactly; on the others the difference is what compression alone costs.
    Returns the renderer/scene pairs whose difference exceeds --max-over-8.
    """
    rows, changed = [], []
    for name in names:
        for renderer in renderers:
            off, on = root / 'off' / name / f'{renderer}.png', root / 'on' / name / f'{renderer}.png'
            if not (off.exists() and on.exists()):
                continue
            a = np.asarray(Image.open(off).convert('RGB'))
            b = np.asarray(Image.open(on).convert('RGB'))
            if a.shape != b.shape:
                rows.append((name, renderer, None)); changed.append(f'{name}/{renderer}'); continue
            value = metrics(difference(a, b, args.shift))
            rows.append((name, renderer, value))
            if value['over_8_percent'] > args.max_over_8:
                changed.append(f'{name}/{renderer}')
    lines = ['| scene | renderer | on vs off: % over 8 | % over 32 | mean |', '| --- | --- | --- | --- | --- |']
    for name, renderer, value in rows:
        lines.append(f'| {name} | {renderer} | ' + ('size differs | | |' if value is None else
                     f'{value["over_8_percent"]} | {value["over_32_percent"]} | {value["mean"]} |'))
    report = '\n'.join(lines)
    (root / 'compression.md').write_text(report + '\n')
    (root / 'compression.json').write_text(json.dumps(
        [{'scene': n, 'renderer': r, **(v or {'size_differs': True})} for n, r, v in rows], indent=1) + '\n')
    print('\nHD block compression on vs off, each renderer against itself:\n' + report)
    return changed


def run_pass(args, names, renderers, output):
    """Draws and compares every scene once; returns the failing scene names."""
    previous, drawn = {}, {}
    if args.reanalyse:
        binary = '(reanalysed captures)'
        if (output / 'summary.json').exists():
            drawn = json.loads((output / 'summary.json').read_text())
            previous, binary = drawn['scenes'], drawn['binary']
    else:
        binary = Path(args.binary) if args.binary else newest('test/torus-render-benchmark')
    output.mkdir(parents=True, exist_ok=True)
    if args.glslang:
        args.shader_dir = str(generate_shaders(args.glslang, output / 'shaders'))
    print(f'binary {binary}\noutput {output.relative_to(ROOT)}\n{len(names)} scenes x {len(renderers)} renderers, '
          f'{args.jobs} at once\n', flush=True)
    started = time.monotonic()
    results = {}
    pending = {name: [] for name in names}
    lock = threading.Lock()

    def finish(name):
        directory = output / name
        records = sorted(pending[name], key=lambda r: renderers.index(r['renderer']))
        result, images, consensus, deltas = compare_scene(args, name, directory, records)
        if args.reference and args.reference in images:
            base = images[args.reference]
            for renderer, image in images.items():
                if renderer != args.reference and image.shape == base.shape:
                    next(r for r in records if r['renderer'] == renderer)['reference'] = metrics(
                        difference(base, image, args.shift))
        for record in records:
            if record['status'] in ('failed', 'setup') and record['renderer'] not in INFORMATIONAL:
                result['problems'].append(f'{record["renderer"]} {record["status"]}: {record.get("reason", "")[:200]}')
                result['verdict'] = 'fail'
        sheet = contact_sheet(args, name, directory, records, result, images, consensus, deltas)
        result.update(description=SCENES[name]['description'], bar=args.max_over_8, renderers=records,
                      sheet=str(sheet.relative_to(ROOT)) if sheet else None,
                      renderer_seconds=round(sum(r['seconds'] for r in records), 1),
                      spec={key: value for key, value in SCENES[name].items() if key != 'description'})
        return result

    def job(name, renderer):
        directory = output / name
        directory.mkdir(parents=True, exist_ok=True)
        record = reuse_capture(previous, name, directory, renderer) if args.reanalyse else \
            run_renderer(args, binary, name, directory, renderer)
        with lock:
            pending[name].append(record)
            complete = len(pending[name]) == len(renderers)
        if complete:
            result = finish(name)
            with lock:
                results[name] = result
                print(f'[{len(results)}/{len(names)} {time.monotonic() - started:5.0f}s] '
                      + table({name: result}).split('\n', 2)[2], flush=True)

    with concurrent.futures.ThreadPoolExecutor(max(1, args.jobs)) as pool:
        futures = [pool.submit(job, name, renderer) for name in names for renderer in renderers]
        for future in concurrent.futures.as_completed(futures):
            future.result()
    wall = drawn.get('wall_seconds', 0) if args.reanalyse else time.monotonic() - started
    results = {name: results[name] for name in names}
    report = table(results)
    failures = [name for name, result in results.items() if result['verdict'] != 'ok']
    unavailable = sorted({record['renderer'] for result in results.values() for record in result['renderers']
                          if record['status'] == 'unavailable'})
    summary = {'binary': str(binary), 'platform': platform.platform(), 'settle_frames': args.settle_frames,
               'interval_ms': args.interval_ms, 'window_size': args.window_size, 'hidpi': args.hidpi,
               'device_tier': args.device_tier, 'hd': args.hd, 'compression': args.compression, 'shift': args.shift,
               'max_over_8': args.max_over_8, 'jobs': args.jobs, 'wall_seconds': round(wall, 1), 'scenes': results, 'unavailable': unavailable,
               'failures': failures}
    (output / 'summary.json').write_text(json.dumps(summary, indent=1) + '\n')
    (output / 'report.md').write_text(report + '\n')
    write_index(output, results, args, wall)
    print('\n' + report)
    if unavailable:
        print(f'\nSkipped (unavailable here): {", ".join(unavailable)}')
    print(f'\n{len(names)} scenes in {wall:.0f} s; contact sheets: {output.relative_to(ROOT)}/index.html')
    if failures:
        print(f'\nFAILED ({len(failures)}): ' + ', '.join(
            f'{name} ({results[name].get("outlier", "?")})' for name in failures))
    return failures


if __name__ == '__main__':
    sys.exit(main())
