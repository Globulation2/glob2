#!/usr/bin/env python3
"""Run the renderer memory scenario and report peak memory by owner.

The scenario is torus-render-benchmark loading a saved game and cycling the
camera through pan and zoom, paced like a display so asset streaming keeps up.
The benchmark prints the renderer's tracked bytes each frame
(FRAME_RENDER_MEMORY); this runner adds what the renderer cannot see: the
process's resident memory and, on Linux, the GPU driver's per-process memory.

    python3 tools/rendering/memory_scenario.py --prepare --label master
    python3 tools/rendering/memory_scenario.py --label change --compare master

Results go to artifacts/render-memory/<label>/ (log, samples, summary.json).
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / 'artifacts/render-memory'
SAVE = OUTPUT / 'honeycomb-256-tick-30000.game.gz'
MIB = 1024 * 1024
PLAYERS = ('castor', 'cortex', 'castor', 'cortex')
# Reported as peaks; everything else in FRAME_RENDER_MEMORY is cumulative.
GAUGES = (
    ('cpu_surface_bytes', 'Decoded pixels held by surfaces'),
    ('cpu_high_resolution_bytes', '  of which HD artwork'),
    ('cpu_compositor_bytes', 'Terrain compositor copies'),
    ('cpu_prepared_bytes', 'Upload staging'),
    ('gpu_surface_bytes', 'GPU sprite and atlas textures'),
    ('gpu_array_bytes', 'GPU texture-array pages'),
    ('gpu_direct_terrain_bytes', 'GPU direct terrain'),
    ('gpu_terrain_page_bytes', 'GPU terrain pages'),
    ('gpu_frame_cache_bytes', 'GPU frame cache'),
    ('gpu_skin_atlas_bytes', 'GPU skin atlas'),
    ('gpu_view_tile_bytes', 'GPU view tiles'),
)


def newest(relative):
    builds = [path for path in (ROOT / 'build').glob('*/client/perf*/' + relative) if path.is_file()]
    builds += [path for path in (ROOT / 'build').glob('*/client/perf/o3/' + relative) if path.is_file()]
    if not builds:
        raise SystemExit(f'No perf build of {relative}; run: python3 tools/dev_build.py perf=1 glob2 torus-render-benchmark')
    return max(builds, key=lambda path: path.stat().st_mtime)


def prepare(glob2):
    """Generate the standard map and play it to a mid-game save."""
    OUTPUT.mkdir(parents=True, exist_ok=True)
    work = OUTPUT / 'prepare'
    map_file = OUTPUT / 'honeycomb-256.map.gz'
    subprocess.run([glob2, 'map', 'generate', 'honeycomb-isle', '--width', '256', '--height', '256',
                    '--teams', '4', '--seed', '7', '--output', map_file], cwd=ROOT, check=True,
                   stdout=subprocess.DEVNULL)
    command = [glob2, 'game', 'run', '--map-file', map_file, '--game-seed', '11', '--ticks', '30000',
               '--save', 'final', '--output-dir', work]
    for player in PLAYERS:
        command += ['--player', player]
    subprocess.run(command, cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    (work / 'final.game.gz').replace(SAVE)


def process_memory(pid):
    """Resident bytes and, where the kernel exposes it, GPU driver bytes of one process."""
    sample = {}
    try:
        for line in Path(f'/proc/{pid}/status').read_text().splitlines():
            key, _, value = line.partition(':')
            if key in ('VmRSS', 'VmHWM'):
                sample[key] = int(value.split()[0]) * 1024
        clients = {}
        for entry in Path(f'/proc/{pid}/fdinfo').iterdir():
            text = entry.read_text()
            client = re.search(r'^drm-client-id:\s*(\d+)', text, re.M)
            if not client:
                continue
            units = {'': 1, 'KiB': 1024, 'MiB': MIB}
            clients[client.group(1)] = sum(
                int(value) * units.get(unit, 1)
                for value, unit in re.findall(r'^drm-memory-\w+:\s*(\d+)\s*(\w*)', text, re.M))
        if clients:
            sample['gpu_driver'] = sum(clients.values())
    except OSError:
        pass
    return sample


def run(args, directory):
    binary = Path(args.binary) if args.binary else newest('test/torus-render-benchmark')
    environment = dict(os.environ)
    environment.update({
        'GLOB2_USER_DATA_DIR': str(directory / 'profile'),
        'GLOB2_BENCH_GAME': str(Path(args.save).resolve()),
        'GLOB2_BENCH_FRAMES': str(args.frames),
        'GLOB2_BENCH_FRAME_INTERVAL_MS': str(args.interval_ms),
        'GLOB2_BENCH_FLAT': '1',
        'GLOB2_BENCH_CAMERA_MOTION': '1',
        'GLOB2_BENCH_FRAME_TIMES': '1',
        'GLOB2_BENCH_MODE': '2D no clouds',
    })
    if args.zoom:
        environment['GLOB2_BENCH_ZOOM'] = str(args.zoom)
    if args.capture:
        environment['GLOB2_BENCH_CAPTURE'] = str(directory / 'final.ppm')
    command = [str(binary), '--renderer', args.renderer, '--no-fullscreen', '--mute', '--window-size', args.window_size]
    samples = []
    log = directory / 'benchmark.log'
    with log.open('w') as output:
        process = subprocess.Popen(command, cwd=ROOT, env=environment, stdout=output, stderr=subprocess.STDOUT)

        def sample():
            while process.poll() is None:
                value = process_memory(process.pid)
                if value:
                    samples.append(value)
                time.sleep(0.25)
        sampler = threading.Thread(target=sample, daemon=True)
        sampler.start()
        code = process.wait()
        sampler.join()
    if code:
        raise SystemExit(f'{binary} exited with {code}; see {log}')
    return binary, command, log, samples


def summarize(log, samples):
    peaks, last, frames = {}, {}, 0
    summary = {}
    for line in log.read_text(errors='replace').splitlines():
        if line.startswith('FRAME_RENDER_MEMORY '):
            frames += 1
            last = {key: int(value) for key, value in re.findall(r'(\w+)=(\d+)', line)}
            for key, value in last.items():
                peaks[key] = max(peaks.get(key, 0), value)
        elif match := re.match(r'2D no clouds (median|p99)=([\d.]+) ms (p95|max)=([\d.]+) ms', line):
            summary[f'frame_{match.group(1)}_ms'] = float(match.group(2))
            summary[f'frame_{match.group(3)}_ms'] = float(match.group(4))
        elif line.startswith('BACKEND '):
            summary['backend'] = line
        elif line.startswith('RENDER_MEMORY_SPRITE '):
            summary.setdefault('sprites', []).append(line.split(' ', 1)[1])
    if not frames:
        raise SystemExit(f'No FRAME_RENDER_MEMORY lines in {log}: the benchmark predates the memory report')
    summary['frames'] = frames
    summary['peaks'] = {key: peaks.get(key, 0) for key, _ in GAUGES}
    summary['image_decodes'] = last.get('image_decodes', 0)
    summary['image_decoded_bytes'] = last.get('image_decoded_bytes', 0)
    summary['peak_rss_bytes'] = max([last.get('peak_rss_bytes', 0)] + [s.get('VmHWM', 0) for s in samples])
    drivers = [s['gpu_driver'] for s in samples if 'gpu_driver' in s]
    summary['peak_gpu_driver_bytes'] = max(drivers) if drivers else None
    return summary


def table(summary, baseline=None):
    def mib(value):
        return 'n/a' if value is None else f'{value / MIB:.0f}'

    def row(name, value, before, text=mib):
        cells = [name] + ([text(before)] if baseline else []) + [text(value)]
        return '| ' + ' | '.join(cells) + ' |'
    header = ['Peak, MiB'] + (['before'] if baseline else []) + ['after' if baseline else 'value']
    lines = ['| ' + ' | '.join(header) + ' |', '|' + '---|' * len(header)]
    before = baseline or {}
    lines.append(row('Process resident memory', summary['peak_rss_bytes'], before.get('peak_rss_bytes')))
    lines.append(row('GPU driver memory', summary['peak_gpu_driver_bytes'], before.get('peak_gpu_driver_bytes')))
    for key, name in GAUGES:
        lines.append(row(name, summary['peaks'][key], before.get('peaks', {}).get(key)))
    lines.append(row('Image decodes (count)', summary['image_decodes'], before.get('image_decodes'), str))
    lines.append(row('Image decode output', summary['image_decoded_bytes'], before.get('image_decoded_bytes')))
    for key in ('frame_median_ms', 'frame_p95_ms', 'frame_p99_ms', 'frame_max_ms'):
        lines.append(row(key.replace('_', ' '), summary.get(key), before.get(key), lambda v: 'n/a' if v is None else f'{v:.2f}'))
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--label', required=True, help='Result name under artifacts/render-memory/')
    parser.add_argument('--compare', help='Label of an earlier result to print beside this one')
    parser.add_argument('--prepare', action='store_true', help='Generate the standard save first if it is missing')
    parser.add_argument('--save', default=str(SAVE), help='Saved game to load')
    parser.add_argument('--binary', help='torus-render-benchmark to run (default: newest perf build)')
    parser.add_argument('--renderer', default='legacy-gl')
    parser.add_argument('--window-size', default='1920x1080')
    parser.add_argument('--frames', type=int, default=3600, help='Measured frames; 120 make one zoom cycle')
    parser.add_argument('--zoom', type=float,
                        help='Start of the zoom cycle, which reaches four times this (default: the minimum zoom)')
    parser.add_argument('--capture', action='store_true', help='Save the last frame as final.ppm')
    parser.add_argument('--interval-ms', type=int, default=16, help='Pause between frames so streaming keeps pace')
    args = parser.parse_args()
    if args.prepare and not Path(args.save).exists():
        prepare(newest('src/glob2'))
    if not Path(args.save).exists():
        raise SystemExit(f'{args.save} is missing; pass --prepare to generate the standard save')
    directory = OUTPUT / args.label
    directory.mkdir(parents=True, exist_ok=True)
    binary, command, log, samples = run(args, directory)
    summary = summarize(log, samples)
    summary.update(binary=str(binary), command=command, save=str(args.save), frames_requested=args.frames,
                   interval_ms=args.interval_ms, zoom=args.zoom)
    (directory / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    (directory / 'samples.json').write_text(json.dumps(samples) + '\n')
    baseline = None
    if args.compare:
        baseline = json.loads((OUTPUT / args.compare / 'summary.json').read_text())
    print(table(summary, baseline))
    print(f'\n{summary.get("backend", "")}\nDetails: {directory.relative_to(ROOT)}/summary.json')
    return 0


if __name__ == '__main__':
    sys.exit(main())
