#!/usr/bin/env python3
"""Measure how much terrain the view draws from fallbacks while a player plays.

Runs torus-render-benchmark on a saved game with the scripted camera
(GLOB2_BENCH_CAMERA_SCRIPT=fallback): from the first frame of the game, cycles
of a jump to a new place, a hold, a pan, wheel steps out to the overview and
back in. Each frame reports its visible terrain cells by source
(FRAME_FALLBACK, see src/render/TerrainFallbackMetrics.h): detailed pages
drawn from another density (stand-ins) or missing (overview backdrop), and
overview cells drawn from the other density, the whole-map base or nothing
(transparent). This runner sums them and times how long after each camera
event the view settles with no fallback at all.

    python3 tools/rendering/fallback_scenario.py --label master --renderer portable-sdl
    python3 tools/rendering/fallback_scenario.py --label change --renderer portable-sdl --compare master

The default save is the memory scenario's (tools/rendering/memory_scenario.py
--prepare makes it). Results go to artifacts/render-fallback/<label>/.
"""
import argparse
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / 'artifacts/render-fallback'
SAVE = ROOT / 'artifacts/render-memory/honeycomb-256-tick-30000.game.gz'
KINDS = (
    ('stand_in_cells', 'Pages from another density'),
    ('backdrop_cells', 'Pages missing (overview beneath)'),
    ('fading_cells', 'Pages fading in'),
    ('overview_other_cells', 'Overview from the other density'),
    ('overview_base_cells', 'Overview from the whole-map base'),
    ('overview_hole_cells', 'Overview transparent (nothing)'),
    ('overview_fading_cells', 'Overview fading in'),
)
EVENTS = ('start', 'jump', 'pan', 'zoom_out', 'zoom_in')


def newest(relative):
    builds = [path for path in (ROOT / 'build').glob('*/client/*/*/' + relative) if path.is_file()]
    builds += [path for path in (ROOT / 'build').glob('*/client/*/*/*/' + relative) if path.is_file()]
    if not builds:
        raise SystemExit(f'No build of {relative}; run: python3 tools/dev_build.py perf=1 torus-render-benchmark')
    return max(builds, key=lambda path: path.stat().st_mtime)


def run(args, directory):
    binary = Path(args.binary) if args.binary else newest('test/torus-render-benchmark')
    environment = dict(os.environ)
    environment.update({
        'GLOB2_USER_DATA_DIR': str(directory / 'profile'),
        'GLOB2_BENCH_FRAMES': str(args.frames),
        'GLOB2_BENCH_WARMUP_FRAMES': '0',
        'GLOB2_BENCH_FRAME_INTERVAL_MS': str(args.interval_ms),
        'GLOB2_BENCH_FLAT': '1',
        'GLOB2_BENCH_CAMERA_SCRIPT': 'fallback',
        'GLOB2_BENCH_FRAME_TIMES': '1',
        'GLOB2_BENCH_MODE': '2D no clouds',
    })
    environment['GLOB2_BENCH_GAME'] = str(Path(args.save).resolve())
    if args.workers:
        environment['GLOB2_BACKGROUND_RENDER_WORKERS'] = str(args.workers)
    if args.capture_frames:
        environment['GLOB2_BENCH_CAPTURE_FRAMES'] = args.capture_frames
        environment['GLOB2_BENCH_CAPTURE_PREFIX'] = str(directory / 'frame')
    if args.hd:
        environment['GLOB2_BENCH_TERRAIN_SWITCH'] = 'hd'
    if args.reset_at is not None:
        environment['GLOB2_BENCH_DEVICE_RESET_AT'] = str(args.reset_at)
    command = [str(binary), '--renderer', args.renderer, '--no-fullscreen', '--mute', '--window-size', args.window_size]
    log = directory / 'benchmark.log'
    with log.open('w') as output:
        code = subprocess.run(command, cwd=ROOT, env=environment, stdout=output, stderr=subprocess.STDOUT).returncode
    if code:
        raise SystemExit(f'{binary} exited with {code}; see {log}')
    return binary, command, log


def summarize(log):
    frames = []
    direct, hd = {}, {}
    for line in log.read_text(errors='replace').splitlines():
        if line.startswith('FRAME_HD '):
            fields = dict(re.findall(r'(\w+)=(\S+)', line))
            if 'frame' in fields and 'pending' in fields:
                hd[int(fields['frame'])] = int(fields['pending'])
        if line.startswith('FRAME_DIRECT_TERRAIN '):
            fields = dict(re.findall(r'(\w+)=(\S+)', line))
            if 'frame' in fields:
                direct[int(fields['frame'])] = fields
        if line.startswith('FRAME_FALLBACK '):
            fields = dict(re.findall(r'(\w+)=(\S+)', line))
            # Stderr from other threads can split a line: skip a torn one.
            if any(key not in fields for key, _ in KINDS):
                continue
            frame = {key: (value if key == 'event' else float(value)) for key, value in fields.items()}
            frame['fallback'] = sum(frame[key] for key, _ in KINDS)
            frames.append(frame)
    if not frames:
        raise SystemExit(f'No FRAME_FALLBACK lines in {log}')
    summary = {'frames': len(frames)}
    # Direct terrain (Legacy GL, SDL GPU): visible mixed cells drawn as a
    # corner split, with no mask at the view's density, the previous one or the
    # whole-map base, and the times a device lost its streamed textures.
    split = [int(direct.get(int(frame['frame']), {}).get('split_cells', 0)) for frame in frames]
    summary['split_frames'] = sum(1 for value in split if value)
    summary['split_cell_frames'] = sum(split)
    summary['restarts'] = max((int(fields.get('restarts', 0)) for fields in direct.values()), default=0)
    summary['page_frames'] = sum(1 for frame in frames if frame['page_cells'])
    # Direct terrain: from each camera event, how long until no visible mixed
    # cell waits for its mask at the view's density (FRAME_HD pending).
    settle = []
    marks = [i for i, frame in enumerate(frames) if frame['event'] != '-']
    for n, start in enumerate(marks):
        end = marks[n + 1] if n + 1 < len(marks) else len(frames)
        done = next((i for i in range(start, end) if not hd.get(int(frames[i]['frame']), 0)), None)
        if done is not None:
            settle.append(frames[done]['time_ms'] - frames[start]['time_ms'])
    summary['hd_settled_events'] = len(settle)
    summary['hd_unsettled_events'] = len(marks) - len(settle)
    summary['hd_settle_median_ms'] = statistics.median(settle) if settle else None
    summary['hd_settle_max_ms'] = max(settle) if settle else None
    summary['hd_pending_max'] = max(hd.values(), default=0)
    summary['fallback_frames'] = sum(1 for frame in frames if frame['fallback'])
    shown = sum(frame['page_cells'] for frame in frames) + sum(
        frame['overview_cells'] for frame in frames if not frame['page_cells'])
    summary['visible_cell_frames'] = shown
    summary['kinds'] = {}
    for key, _ in KINDS:
        total = sum(frame[key] for frame in frames)
        summary['kinds'][key] = {
            'frames': sum(1 for frame in frames if frame[key]),
            'cell_frames': total,
            'percent_of_visible': 100 * total / shown if shown else 0,
        }
    # Time from each camera event to the first frame without any fallback;
    # an event followed by another before settling counts as unsettled.
    events = {name: {'settle_ms': [], 'settle_frames': [], 'unsettled': 0, 'fallback_frames': 0} for name in EVENTS}
    marks = [i for i, frame in enumerate(frames) if frame['event'] != '-']
    for n, start in enumerate(marks):
        end = marks[n + 1] if n + 1 < len(marks) else len(frames)
        name = frames[start]['event']
        record = events.setdefault(name, {'settle_ms': [], 'settle_frames': [], 'unsettled': 0, 'fallback_frames': 0})
        record['fallback_frames'] += sum(1 for frame in frames[start:end] if frame['fallback'])
        settled = next((i for i in range(start, end) if not frames[i]['fallback']), None)
        if settled is None:
            record['unsettled'] += 1
            continue
        record['settle_frames'].append(settled - start)
        record['settle_ms'].append(frames[settled]['time_ms'] - frames[start]['time_ms'])
    for record in events.values():
        values = sorted(record['settle_ms'])
        record['count'] = len(values) + record['unsettled']
        record['median_ms'] = statistics.median(values) if values else None
        record['max_ms'] = values[-1] if values else None
        record['max_frames'] = max(record['settle_frames']) if record['settle_frames'] else None
        del record['settle_ms'], record['settle_frames']
    summary['events'] = events
    return summary


def table(summary, baseline=None):
    def cell(value, digits=1):
        if value is None:
            return 'n/a'
        return f'{value:.{digits}f}' if isinstance(value, float) else str(value)

    def row(name, value, before):
        cells = [name] + ([cell(before)] if baseline else []) + [cell(value)]
        return '| ' + ' | '.join(cells) + ' |'
    header = ['Measure'] + (['before'] if baseline else []) + ['after' if baseline else 'value']
    lines = ['| ' + ' | '.join(header) + ' |', '|' + '---|' * len(header)]
    before = baseline or {'kinds': {}, 'events': {}}
    lines.append(row('Frames', summary['frames'], before.get('frames')))
    lines.append(row('Frames showing any fallback', summary['fallback_frames'], before.get('fallback_frames')))
    lines.append(row('Frames drawing terrain pages', summary.get('page_frames'), before.get('page_frames')))
    lines.append(row('Direct terrain: frames with corner-split cells', summary.get('split_frames'), before.get('split_frames')))
    lines.append(row('Direct terrain: corner-split cell-frames', summary.get('split_cell_frames'), before.get('split_cell_frames')))
    lines.append(row('Direct terrain: streamed texture restarts', summary.get('restarts'), before.get('restarts')))
    lines.append(row('Direct terrain: most masks pending', summary.get('hd_pending_max'), before.get('hd_pending_max')))
    lines.append(row('Direct terrain: events unsettled (masks pending)', summary.get('hd_unsettled_events'), before.get('hd_unsettled_events')))
    lines.append(row('Direct terrain: median ms until no mask pending', summary.get('hd_settle_median_ms'), before.get('hd_settle_median_ms')))
    lines.append(row('Direct terrain: max ms until no mask pending', summary.get('hd_settle_max_ms'), before.get('hd_settle_max_ms')))
    for key, name in KINDS:
        now, then = summary['kinds'][key], before['kinds'].get(key, {})
        lines.append(row(f'{name}: frames', now['frames'], then.get('frames')))
        lines.append(row(f'{name}: % of visible cells', now['percent_of_visible'], then.get('percent_of_visible')))
    for name in EVENTS:
        now, then = summary['events'].get(name, {}), before['events'].get(name, {})
        if not now.get('count'):
            continue
        lines.append(row(f'{name}: events', now['count'], then.get('count')))
        lines.append(row(f'{name}: frames with fallback', now['fallback_frames'], then.get('fallback_frames')))
        lines.append(row(f'{name}: unsettled before the next event', now['unsettled'], then.get('unsettled')))
        lines.append(row(f'{name}: median ms to settle', now['median_ms'], then.get('median_ms')))
        lines.append(row(f'{name}: max ms to settle', now['max_ms'], then.get('max_ms')))
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--label', required=True, help='Result name under artifacts/render-fallback/')
    parser.add_argument('--compare', help='Label of an earlier result to print beside this one')
    parser.add_argument('--save', default=str(SAVE), help='Saved game to load')
    parser.add_argument('--binary', help='torus-render-benchmark to run (default: the newest build)')
    parser.add_argument('--renderer', default='portable-sdl')
    parser.add_argument('--window-size', default='1920x1080')
    parser.add_argument('--frames', type=int, default=1920, help='Measured frames; 480 make one cycle')
    parser.add_argument('--interval-ms', type=int, default=16, help='Pause between frames, as a display paces them')
    parser.add_argument('--workers', type=int, help='Presentation workers (GLOB2_BACKGROUND_RENDER_WORKERS)')
    parser.add_argument('--capture-frames', help='Comma-separated frames to save as frame-<n>.ppm')
    parser.add_argument('--hd', action='store_true', help='Switch HD terrain artwork on after about a second (frame 68)')
    parser.add_argument('--reset-at', type=int, help='Reset the render device at this frame (streamed textures lost)')
    args = parser.parse_args()
    if not Path(args.save).exists():
        raise SystemExit(f'{args.save} is missing; run tools/rendering/memory_scenario.py --prepare first')
    directory = OUTPUT / args.label
    directory.mkdir(parents=True, exist_ok=True)
    binary, command, log = run(args, directory)
    summary = summarize(log)
    summary.update(binary=str(binary), command=command, save=str(args.save), renderer=args.renderer,
                   interval_ms=args.interval_ms, workers=args.workers)
    (directory / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    baseline = json.loads((OUTPUT / args.compare / 'summary.json').read_text()) if args.compare else None
    print(table(summary, baseline))
    print(f'\nDetails: {directory.relative_to(ROOT)}/summary.json')
    return 0


if __name__ == '__main__':
    sys.exit(main())
