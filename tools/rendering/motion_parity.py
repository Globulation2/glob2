#!/usr/bin/env python3
"""Check that every renderer draws each cell's own terrain while the view moves.

The renderer parity matrix (renderer_parity.py) compares settled frames. A view
in motion draws stand-ins while terrain refills at a new density: coarser art,
pages from another density, the overview. Those may be softer than the settled
frame but must show the same materials. This runner plays one scripted camera
twice per renderer in torus-render-benchmark (GLOB2_BENCH_CAMERA_SCRIPT=fallback
with GLOB2_BENCH_RAPID_ZOOM: a jump, a hold, a pan, then a wheel spun in and
out a step every few frames), once as players see it and once with every
frame's terrain completed first (GLOB2_BENCH_COMPLETE_TERRAIN), and compares
the captured frames in blocks of 1/80 of the width: a block whose mean colour
differs by more than --max-block-delta levels in any channel shows foreign art
(another variant's tile, an empty slot, a hole). It also reports the frame
times of a third, uncaptured run and the direct pass's art counters (FRAME_DIRECT_ART).

    python3 tools/dev_build.py perf=1 torus-render-benchmark
    python3 tools/rendering/motion_parity.py --label master
    python3 tools/rendering/motion_parity.py --label change --renderers legacy-gl,sdl-gpu

The default save is the memory scenario's (tools/rendering/memory_scenario.py
--prepare or renderer_parity.py --prepare makes it). Results, a contact sheet of
the worst frame per renderer and summary.json go to
artifacts/render-motion/<label>/. Exits 1 when a renderer shows foreign blocks,
a stale art record, or fails to run.
"""
from pathlib import Path
import argparse
import json
import os
import re
import shutil
import statistics
import subprocess
import sys

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / 'artifacts/render-motion'
SAVE = ROOT / 'artifacts/render-memory/honeycomb-256-tick-30000.game.gz'
RENDERERS = ('legacy-gl', 'portable-sdl', 'sdl-gpu', 'programmable-gl')
# One 480-frame cycle of the scripted camera: the jump at 0, the pan from 120,
# the wheel from 240 to 420.
CAPTURED = list(range(126, 240, 12)) + list(range(240, 420, 4))


def newest(relative):
    builds = [path for pattern in ('*/client/*/', '*/client/*/*/')
              for path in (ROOT / 'build').glob(pattern + relative) if path.is_file()]
    if not builds:
        raise SystemExit(f'No build of {relative}; run: python3 tools/dev_build.py perf=1 torus-render-benchmark')
    return max(builds, key=lambda path: path.stat().st_mtime)


def run(args, binary, renderer, directory, complete, capture=True):
    directory.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ)
    environment.update({
        'GLOB2_USER_DATA_DIR': str(directory / 'profile'),
        'GLOB2_BENCH_GAME': str(Path(args.save).resolve()),
        'GLOB2_BENCH_FRAMES': '480',
        'GLOB2_BENCH_WARMUP_FRAMES': '0',
        'GLOB2_BENCH_FRAME_INTERVAL_MS': str(args.interval_ms),
        'GLOB2_BENCH_FLAT': '1',
        'GLOB2_BENCH_MODE': '2D no clouds',
        'GLOB2_BENCH_HD': '1',
        'GLOB2_BENCH_PAUSE_PRESENTATION': '1',
        'GLOB2_BENCH_CAMERA_SCRIPT': 'fallback',
        'GLOB2_BENCH_RAPID_ZOOM': str(args.wheel_frames),
        'GLOB2_BENCH_RAPID_DEPTH': str(args.wheel_steps),
        'GLOB2_BENCH_FRAME_TIMES': '1',
    })
    if capture:
        environment['GLOB2_BENCH_CAPTURE_FRAMES'] = ','.join(map(str, CAPTURED))
        environment['GLOB2_BENCH_CAPTURE_PREFIX'] = str(directory / 'frame')
    if complete:
        environment['GLOB2_BENCH_COMPLETE_TERRAIN'] = '1'
    command = [str(binary), '--renderer', renderer, '--no-fullscreen', '--mute', '--window-size', args.window_size]
    log = directory / 'benchmark.log'
    with log.open('w') as output:
        code = subprocess.run(command, cwd=ROOT, env=environment, stdout=output, stderr=subprocess.STDOUT,
                              timeout=args.timeout).returncode
    shutil.rmtree(directory / 'profile', ignore_errors=True)
    text = log.read_text(errors='replace')
    backend = re.search(r'^BACKEND requested=\S+ active=(\S+)', text, re.M)
    if backend and backend.group(1) != renderer:
        return text, f'fell back to {backend.group(1)}'
    return text, None if code == 0 else f'exit {code}'


def frames(directory):
    found = {}
    for frame in CAPTURED:
        path = directory / f'frame-{frame}.ppm'
        if path.exists():
            found[frame] = path
    return found


def block_means(image, block):
    pixels = np.asarray(image.convert('RGB'), dtype=np.float32)
    rows, columns = pixels.shape[0] // block, pixels.shape[1] // block
    pixels = pixels[:rows * block, :columns * block]
    return pixels.reshape(rows, block, columns, block, 3).mean(axis=(1, 3))


def frame_times(text):
    times = [float(m.group(1)) for m in re.finditer(r'^FRAME mode=2D no clouds frame=\d+ elapsed_ms=([\d.]+)', text, re.M)]
    if not times:
        return {}
    ordered = sorted(times)
    return {'frames': len(times), 'median_ms': round(statistics.median(times), 3),
            'p99_ms': round(ordered[min(len(ordered) - 1, int(len(ordered) * .99))], 3),
            'max_ms': round(ordered[-1], 3), 'over_16_7': sum(t > 16.7 for t in times),
            'over_33_3': sum(t > 33.3 for t in times)}


def art_counters(text):
    # Lines can interleave with stderr: keep only complete records.
    rows = [m.groupdict() for m in re.finditer(
        r'FRAME_DIRECT_ART frame=-?\d+ visible=(?P<visible>\d+) absent=(?P<absent>\d+) stale=(?P<stale>\d+) '
        r'low_art_bytes=(?P<low_art_bytes>\d+)', text)]
    if not rows:
        return {}
    return {'frames': len(rows), 'stale_frames': sum(int(r['stale']) > 0 for r in rows),
            'absent_frames': sum(int(r['absent']) > 0 for r in rows),
            'most_absent': max(int(r['absent']) for r in rows),
            'low_art_bytes': int(rows[-1]['low_art_bytes'])}


def check(args, binary, renderer, directory):
    record = {'renderer': renderer}
    moving_text, problem = run(args, binary, renderer, directory / 'moving', False)
    if problem:
        return {**record, 'status': 'failed', 'reason': problem}
    complete_text, problem = run(args, binary, renderer, directory / 'complete', True)
    if problem:
        return {**record, 'status': 'failed', 'reason': f'complete run: {problem}'}
    # Frame times from a run without captures, whose readbacks would stall frames.
    timing_text, problem = run(args, binary, renderer, directory / 'timing', False, capture=False)
    if problem:
        return {**record, 'status': 'failed', 'reason': f'timing run: {problem}'}
    moving, complete = frames(directory / 'moving'), frames(directory / 'complete')
    shared = sorted(set(moving) & set(complete))
    if not shared:
        return {**record, 'status': 'failed', 'reason': 'no captured frames'}
    worst = None
    per_frame = []
    for frame in shared:
        a, b = Image.open(moving[frame]), Image.open(complete[frame])
        block = max(8, a.size[0] // 80)
        delta = np.abs(block_means(a, block) - block_means(b, block)).max(axis=2)
        foreign = int((delta > args.max_block_delta).sum())
        per_frame.append({'frame': frame, 'foreign_blocks': foreign, 'largest': round(float(delta.max()), 1)})
        if worst is None or foreign > worst[1] or (foreign == worst[1] and delta.max() > worst[2]):
            worst = (frame, foreign, float(delta.max()))
    frame, foreign, _ = worst
    sheet = Image.new('RGB', (2 * 960, 600))
    sheet.paste(Image.open(moving[frame]).convert('RGB').resize((960, 600)), (0, 0))
    sheet.paste(Image.open(complete[frame]).convert('RGB').resize((960, 600)), (960, 0))
    sheet.save(directory / f'worst-frame-{frame}.png')
    for path in list(moving.values()) + list(complete.values()):
        path.unlink()
    art = art_counters(moving_text)
    bad = sum(f['foreign_blocks'] > 0 for f in per_frame)
    status = 'ok' if not bad and not art.get('stale_frames') else 'foreign art'
    return {**record, 'status': status, 'frames_compared': len(per_frame), 'frames_with_foreign_blocks': bad,
            'worst_frame': frame, 'worst_foreign_blocks': foreign, 'art': art,
            'frame_times': frame_times(timing_text), 'per_frame': per_frame}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--label', required=True, help='Result name under artifacts/render-motion/')
    parser.add_argument('--renderers', default=','.join(RENDERERS))
    parser.add_argument('--save', default=str(SAVE), help='Saved game to load')
    parser.add_argument('--binary', help='torus-render-benchmark to run (default: the newest build)')
    parser.add_argument('--window-size', default='1280x800')
    parser.add_argument('--interval-ms', type=int, default=16, help='Pause between frames, as a display paces them')
    parser.add_argument('--wheel-frames', type=int, default=2, help='Frames between wheel steps')
    parser.add_argument('--wheel-steps', type=int, default=3, help='Wheel steps out before turning back')
    parser.add_argument('--max-block-delta', type=float, default=24,
                        help='Largest mean channel difference of a block before it counts as foreign')
    parser.add_argument('--timeout', type=int, default=600)
    args = parser.parse_args()
    if not Path(args.save).exists():
        raise SystemExit(f'{args.save} missing; run tools/rendering/renderer_parity.py --prepare')
    binary = Path(args.binary) if args.binary else newest('test/torus-render-benchmark')
    output = OUTPUT / args.label
    shutil.rmtree(output, ignore_errors=True)
    results = []
    print('| Renderer | Status | Frames with foreign blocks | Worst frame | Stale art frames | Absent art frames '
          '| Median ms | p99 ms | Max ms | Over 16.7 ms | Over 33.3 ms |')
    print('| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |')
    for renderer in args.renderers.split(','):
        result = check(args, binary, renderer, output / renderer)
        results.append(result)
        art, times = result.get('art', {}), result.get('frame_times', {})
        print(f"| {renderer} | {result['status']}{' (' + result['reason'] + ')' if 'reason' in result else ''} "
              f"| {result.get('frames_with_foreign_blocks', '-')}/{result.get('frames_compared', '-')} "
              f"| {result.get('worst_frame', '-')} | {art.get('stale_frames', '-')} | {art.get('absent_frames', '-')} "
              f"| {times.get('median_ms', '-')} | {times.get('p99_ms', '-')} | {times.get('max_ms', '-')} "
              f"| {times.get('over_16_7', '-')} | {times.get('over_33_3', '-')} |", flush=True)
    (output / 'summary.json').write_text(json.dumps({'binary': str(binary), 'save': args.save,
                                                     'max_block_delta': args.max_block_delta,
                                                     'results': results}, indent=1))
    print(f'Details: {output.relative_to(ROOT)}/summary.json; worst frames: {output.relative_to(ROOT)}/<renderer>/worst-frame-*.png')
    return 0 if all(r['status'] == 'ok' for r in results) else 1


if __name__ == '__main__':
    sys.exit(main())
