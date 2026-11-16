#!/usr/bin/env python3
"""Draw the same standard scenes on every renderer and compare the settled images.

Each renderer may take its own path (direct terrain, pages, device composition,
software rasterisation), but after the view has been held still long enough for
streaming and background composition to finish, they should all show the same
picture to within a few pixels. This runner draws each scene with every
available renderer in a fresh process and profile, holds a fixed camera for
--settle-frames paced frames, captures the last one, and compares every capture
against a reference renderer.

    python3 tools/rendering/memory_scenario.py --prepare --label prepare   # once: the save
    python3 tools/dev_build.py perf=1 torus-render-benchmark
    python3 tools/rendering/renderer_parity.py --label master

Results go to artifacts/renderer-parity/<label>/: one contact sheet per scene
(<scene>.png: every renderer's capture above its difference heat map against
the reference), the captures and logs, summary.json and report.md. The exit
status is 1 when any renderer exceeds the tolerance or fails to run, 0
otherwise; unavailable renderers are skipped and listed.
"""
import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import time

try:
    import numpy as np
    from PIL import Image, ImageDraw
except ImportError as error:  # pragma: no cover - environment guidance
    raise SystemExit(f'{error}; install the development requirements: '
                     'uv pip install -r requirements-dev.txt')

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / 'artifacts/renderer-parity'
SAVE = ROOT / 'artifacts/render-memory/honeycomb-256-tick-30000.game.gz'
RENDERERS = ('legacy-gl', 'portable-sdl', 'sdl-gpu', 'programmable-gl', 'software')
REFERENCES = ('portable-sdl', 'legacy-gl')

# Every scene holds one camera. `gui` scenes draw the whole in-game screen
# (GameGUI::drawAll: map, HUD panel, minimap, top bar, health bars and the
# selection) at the local colony; `map` scenes draw the bare map at the top-left
# tile. Zoom 0 is the minimum (overview) zoom; 1 is the game's default.
SCENES = {
    'map-far': dict(description='Bare map at the overview zoom, health bars on', zoom=0, gui=False,
                    env={'GLOB2_BENCH_BARS': '1'}),
    'gui-far': dict(description='Game screen at the overview zoom', zoom=0, gui=True),
    'gui-default': dict(description='Game screen at the default zoom, a unit selected', zoom=1, gui=True,
                        env={'GLOB2_BENCH_SELECT': 'unit'}),
    'gui-zoomed': dict(description='Game screen zoomed in: HD terrain, units, resources, health bars', zoom=3,
                       gui=True),
    'gui-panned': dict(description='Game screen at the default zoom, panned 24 by 16 tiles', zoom=1, gui=True,
                       env={'GLOB2_BENCH_PAN_X': '24', 'GLOB2_BENCH_PAN_Y': '16'}),
    'small-map': dict(description='Game screen on a small map (SmallForTwo) at the default zoom', zoom=1,
                      gui=True, map='maps/SmallForTwo.map.gz'),
}


def newest(relative):
    builds = [path for path in (ROOT / 'build').glob('*/client/perf*/' + relative) if path.is_file()]
    builds += [path for path in (ROOT / 'build').glob('*/client/perf/o3/' + relative) if path.is_file()]
    builds += [path for path in (ROOT / 'build').glob('*/client/perf/' + relative + '.exe') if path.is_file()]
    if not builds:
        raise SystemExit(f'No perf build of {relative}; run: python3 tools/dev_build.py perf=1 torus-render-benchmark')
    return max(builds, key=lambda path: path.stat().st_mtime)


def scene_environment(args, scene, directory, renderer):
    """The pinned inputs: device tier, HD artwork, desktop HUD, a fresh profile."""
    spec = SCENES[scene]
    profile = directory / f'profile-{renderer}'
    shutil.rmtree(profile, ignore_errors=True)
    env = dict(os.environ)
    for key in [key for key in env if key.startswith('GLOB2_BENCH_')]:
        del env[key]
    env.update({
        'GLOB2_USER_DATA_DIR': str(profile),
        'GLOB2_DEVICE_TIER': args.device_tier,
        'GLOB2_BENCH_HD': '1' if args.hd else '0',
        'GLOB2_MOBILE_UI': '0',
        'GLOB2_BENCH_FLAT': '1',
        'GLOB2_BENCH_MODE': '2D no clouds',
        'GLOB2_BENCH_FRAMES': '1',
        'GLOB2_BENCH_WARMUP_FRAMES': str(args.settle_frames),
        'GLOB2_BENCH_FRAME_INTERVAL_MS': str(args.interval_ms),
        'GLOB2_BENCH_PAUSE_PRESENTATION': '1',
        'GLOB2_BENCH_CAPTURE': str(directory / f'{renderer}.ppm'),
    })
    if spec.get('map'):
        env['GLOB2_BENCH_MAP'] = spec['map']
        env['GLOB2_BENCH_UNITS'] = '0'
    else:
        env['GLOB2_BENCH_GAME'] = str(Path(args.save).resolve())
    if spec['zoom']:
        env['GLOB2_BENCH_ZOOM'] = str(spec['zoom'])
    if spec['gui']:
        env['GLOB2_BENCH_GUI'] = '1'
    env.update(spec.get('env', {}))
    if args.shader_dir:
        env['GLOB2_RENDERER_SHADER_DIR'] = str(Path(args.shader_dir).resolve())
    if platform.system() == 'Linux' and 'SDL_VIDEO_DRIVER' not in env and env.get('DISPLAY'):
        env['SDL_VIDEO_DRIVER'] = 'x11'
    return env


def run_renderer(args, binary, scene, directory, renderer):
    """Draw one scene on one renderer; returns its status record."""
    env = scene_environment(args, scene, directory, renderer)
    capture = directory / f'{renderer}.ppm'
    capture.unlink(missing_ok=True)
    log = directory / f'{renderer}.log'
    command = [str(binary), '--renderer', renderer, '--no-fullscreen', '--mute', '--window-size', args.window_size]
    started = time.monotonic()
    try:
        with log.open('w') as output:
            code = subprocess.run(command, cwd=ROOT, env=env, stdout=output, stderr=subprocess.STDOUT,
                                  timeout=args.timeout).returncode
    except subprocess.TimeoutExpired:
        code = 'timeout'
    text = log.read_text(errors='replace')
    record = {'renderer': renderer, 'log': str(log.relative_to(ROOT)), 'seconds': round(time.monotonic() - started, 1)}
    backend = re.search(r'^BACKEND requested=(\S+) active=(\S+) fallback_reason=(.*)$', text, re.M)
    settings = re.search(r'^BENCH_SETTINGS (.*)$', text, re.M)
    if settings:
        record['settings'] = dict(field.split('=', 1) for field in settings.group(1).split())
    if backend and backend.group(2) != renderer:
        record.update(status='unavailable', reason=f'fell back to {backend.group(2)}: {backend.group(3).strip()}')
        return record
    if code != 0 or not capture.exists():
        tail = '; '.join(line for line in text.strip().splitlines()[-3:])
        if not backend and code != 0 and re.search(r'unavailable|not available|cannot create|no shader', text, re.I):
            record.update(status='unavailable', reason=tail[-300:])
        else:
            record.update(status='failed', reason=f'exit {code}: {tail[-300:]}')
        return record
    image = Image.open(capture).convert('RGB')
    image.save(directory / f'{renderer}.png')
    capture.unlink()
    record.update(status='ok', size=list(image.size))
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


def heat_map(reference, delta):
    """A dimmed grey reference with differences over 8 levels in yellow to red."""
    grey = reference.astype(np.float32).mean(axis=2, keepdims=True) * 0.35
    out = np.repeat(grey, 3, axis=2)
    strength = np.clip(delta.astype(np.float32) / 64.0, 0, 1)
    mask = delta > 8
    heat = np.stack([np.full_like(strength, 255), 255 * (1 - strength), np.zeros_like(strength)], axis=2)
    out[mask] = heat[mask]
    return Image.fromarray(out.astype(np.uint8))


def contact_sheet(scene, directory, records, reference, deltas, scale):
    shown = [record for record in records if record['status'] == 'ok']
    if not shown:
        return None
    width, height = shown[0]['size']
    cell_w, cell_h = int(width * scale), int(height * scale)
    label_h = 22
    sheet = Image.new('RGB', (cell_w * len(shown), (cell_h + label_h) * 2), (24, 24, 32))
    draw = ImageDraw.Draw(sheet)
    for column, record in enumerate(shown):
        renderer = record['renderer']
        x = column * cell_w
        image = Image.open(directory / f'{renderer}.png')
        sheet.paste(image.resize((cell_w, cell_h), Image.LANCZOS), (x, label_h))
        draw.text((x + 6, 4), f'{scene}: {renderer}' + (' (reference)' if renderer == reference else ''),
                  fill=(255, 255, 255))
        if renderer in deltas:
            heat = Image.open(directory / f'diff-{renderer}.png')
            sheet.paste(heat.resize((cell_w, cell_h), Image.BOX), (x, label_h * 2 + cell_h))
            metrics = record['metrics']
            draw.text((x + 6, label_h + cell_h + 4),
                      f'vs {reference}: >8 {metrics["over_8_percent"]:.2f}%  >32 {metrics["over_32_percent"]:.2f}%  '
                      f'mean {metrics["mean"]:.2f}', fill=(255, 220, 120))
    path = directory.parent / f'{scene}.png'
    sheet.save(path)
    return path


def compare_scene(args, scene, directory, records):
    available = {record['renderer']: record for record in records if record['status'] == 'ok'}
    reference = next((name for name in args.reference if name in available), None)
    if reference is None:
        reference = next(iter(available), None)
    deltas = {}
    if reference:
        base = np.asarray(Image.open(directory / f'{reference}.png').convert('RGB'))
        for name, record in available.items():
            if name == reference:
                continue
            image = np.asarray(Image.open(directory / f'{name}.png').convert('RGB'))
            if image.shape != base.shape:
                record.update(status='failed', reason=f'size {image.shape[1]}x{image.shape[0]} differs from '
                                                      f'{reference} {base.shape[1]}x{base.shape[0]}')
                continue
            delta = difference(base, image, args.shift)
            exact = np.abs(base.astype(np.int16) - image.astype(np.int16)).max(axis=2)
            pixels = delta.size
            record['metrics'] = {
                'over_8_percent': 100.0 * np.count_nonzero(delta > 8) / pixels,
                'over_32_percent': 100.0 * np.count_nonzero(delta > 32) / pixels,
                'mean': float(delta.mean()),
                'exact_over_8_percent': 100.0 * np.count_nonzero(exact > 8) / pixels,
            }
            over = record['metrics']['over_8_percent'] > args.max_over_8 or (
                args.max_over_32 is not None and record['metrics']['over_32_percent'] > args.max_over_32)
            record['within_tolerance'] = not over
            heat_map(base, delta).save(directory / f'diff-{name}.png')
            deltas[name] = delta
    sheet = contact_sheet(scene, directory, records, reference, deltas, args.sheet_scale)
    return reference, sheet


def table(results):
    lines = ['| Scene | Renderer | > 8 levels | > 32 levels | Mean | Result |',
             '| --- | --- | ---: | ---: | ---: | --- |']
    for scene, result in results.items():
        for record in result['renderers']:
            name = record['renderer']
            if record['status'] != 'ok':
                lines.append(f'| {scene} | {name} | | | | {record["status"]}: {record.get("reason", "")[:120]} |')
            elif name == result['reference']:
                lines.append(f'| {scene} | {name} | | | | reference |')
            elif 'metrics' in record:
                metrics = record['metrics']
                verdict = 'ok' if record['within_tolerance'] else '**over tolerance**'
                lines.append(f'| {scene} | {name} | {metrics["over_8_percent"]:.2f}% | '
                             f'{metrics["over_32_percent"]:.2f}% | {metrics["mean"]:.2f} | {verdict} |')
    return '\n'.join(lines)


def generate_shaders(glslang, directory):
    command = [sys.executable, str(ROOT / 'tools/shaders/generate.py'), '--output', str(directory),
               '--glslang', str(glslang)]
    subprocess.run(command, cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    return directory


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--label', default='latest', help='Result name under artifacts/renderer-parity/')
    parser.add_argument('--renderers', default=','.join(RENDERERS), help='Comma-separated renderers to draw')
    parser.add_argument('--reference', default=','.join(REFERENCES),
                        help='Comma-separated reference preference; the first available is used')
    parser.add_argument('--scenes', default=','.join(SCENES), help='Comma-separated scenes (see --list-scenes)')
    parser.add_argument('--list-scenes', action='store_true')
    parser.add_argument('--save', default=str(SAVE), help='Saved game the save-based scenes load')
    parser.add_argument('--prepare', action='store_true', help='Generate the standard save first if it is missing')
    parser.add_argument('--binary', help='torus-render-benchmark to run (default: newest perf build)')
    parser.add_argument('--window-size', default='1280x800')
    parser.add_argument('--settle-frames', type=int, default=600,
                        help='Frames the camera is held before the capture, so streaming finishes')
    parser.add_argument('--interval-ms', type=int, default=16, help='Pause between settle frames, as a display paces them')
    parser.add_argument('--device-tier', default='high', choices=('low', 'mid', 'high'))
    parser.add_argument('--no-hd', dest='hd', action='store_false', help='Pin HD artwork off instead of on')
    parser.add_argument('--shift', type=int, default=1,
                        help='Pixels a difference may move and still match (default 1)')
    parser.add_argument('--max-over-8', type=float, default=2.0,
                        help='Tolerance: percent of pixels more than 8 levels from the reference (default 2.0)')
    parser.add_argument('--max-over-32', type=float, help='Optional tolerance on the share over 32 levels')
    parser.add_argument('--shader-dir', default=os.environ.get('GLOB2_RENDERER_SHADER_DIR'),
                        help='SDL GPU / programmable GL shader directory (GLOB2_RENDERER_SHADER_DIR)')
    parser.add_argument('--glslang', help='Pinned glslangValidator: generate shaders into the output first')
    parser.add_argument('--jobs', type=int, default=1, help='Renderers drawn at once (1 keeps GPU timing quiet)')
    parser.add_argument('--timeout', type=int, default=600, help='Seconds one renderer may take for one scene')
    parser.add_argument('--sheet-scale', type=float, default=0.5, help='Contact sheet image scale')
    args = parser.parse_args()
    if args.list_scenes:
        for name, spec in SCENES.items():
            print(f'{name:12} {spec["description"]}')
        return 0
    args.reference = [name for name in args.reference.split(',') if name]
    renderers = [name for name in args.renderers.split(',') if name]
    scenes = [name for name in args.scenes.split(',') if name]
    unknown = [name for name in scenes if name not in SCENES]
    if unknown:
        raise SystemExit(f'Unknown scenes {unknown}; see --list-scenes')
    if args.prepare and not Path(args.save).exists():
        sys.path.insert(0, str(ROOT / 'tools/rendering'))
        import memory_scenario
        memory_scenario.prepare(memory_scenario.newest('src/glob2'))
    if any(not SCENES[name].get('map') for name in scenes) and not Path(args.save).exists():
        raise SystemExit(f'{args.save} is missing; pass --prepare to generate the standard save')
    binary = Path(args.binary) if args.binary else newest('test/torus-render-benchmark')
    output = OUTPUT / args.label
    output.mkdir(parents=True, exist_ok=True)
    if args.glslang:
        args.shader_dir = str(generate_shaders(args.glslang, output / 'shaders'))
    print(f'binary {binary}\noutput {output.relative_to(ROOT)}\n', flush=True)
    results = {}
    for scene in scenes:
        directory = output / scene
        directory.mkdir(parents=True, exist_ok=True)
        with concurrent.futures.ThreadPoolExecutor(max(1, args.jobs)) as pool:
            records = list(pool.map(lambda name: run_renderer(args, binary, scene, directory, name), renderers))
        reference, sheet = compare_scene(args, scene, directory, records)
        for record in records:
            shutil.rmtree(directory / f'profile-{record["renderer"]}', ignore_errors=True)
        results[scene] = {'description': SCENES[scene]['description'], 'reference': reference,
                          'sheet': str(sheet.relative_to(ROOT)) if sheet else None, 'renderers': records}
        print(table({scene: results[scene]}).split('\n', 2)[2], flush=True)
    report = table(results)
    failures = [(scene, record['renderer']) for scene, result in results.items() for record in result['renderers']
                if record['status'] == 'failed' or record.get('within_tolerance') is False]
    unavailable = sorted({record['renderer'] for result in results.values() for record in result['renderers']
                          if record['status'] == 'unavailable'})
    summary = {'binary': str(binary), 'platform': platform.platform(), 'settle_frames': args.settle_frames,
               'interval_ms': args.interval_ms, 'window_size': args.window_size, 'device_tier': args.device_tier,
               'hd': args.hd, 'shift': args.shift, 'max_over_8': args.max_over_8, 'max_over_32': args.max_over_32,
               'scenes': results, 'unavailable': unavailable, 'failures': failures}
    (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    (output / 'report.md').write_text(report + '\n')
    print('\n' + report)
    if unavailable:
        print(f'\nSkipped (unavailable here): {", ".join(unavailable)}')
    print(f'\nContact sheets: {output.relative_to(ROOT)}/<scene>.png; details in summary.json')
    if failures:
        print(f'\nFAILED: {", ".join(f"{scene}/{name}" for scene, name in failures)}')
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
