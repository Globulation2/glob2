#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Alternating baked/rig pairs with raw samples and per-run provenance.

Runs either the worker-only skin-rig-benchmark or the production Scene diagnostic;
"rig" runs use the default fitted clips and "baked" runs set GLOB2_SKIN_RIGS=0.
No production assets, user settings, or GPU clocks are modified.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import random
import statistics
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def capture(command):
    try:
        result = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    except FileNotFoundError:
        return {'command': command, 'returncode': 127, 'output': 'Command unavailable'}
    return {'command': command, 'returncode': result.returncode, 'output': result.stdout}


def quantile(values, fraction):
    return sorted(values)[int((len(values) - 1) * fraction)]


def bootstrap_median(values):
    rng = random.Random(821)
    medians = [statistics.median(rng.choices(values, k=len(values))) for _ in range(10000)]
    return [quantile(medians, .025), quantile(medians, .975)]


def summarize(records):
    groups = {}
    for record in records:
        case = (record['kind'], record['units'], record['cache'])
        groups.setdefault(case, {}).setdefault(record['pair'], {})[record['mode']] = record
    summary = []
    for (kind, units, cache), pairs in sorted(groups.items()):
        complete = [p for p in pairs.values() if set(p) == {'baked', 'rig'}]
        if not complete:
            continue
        row = {'kind': kind, 'units': units, 'cache': cache, 'pairs': len(complete), 'metrics': {}}
        for metric in ('p50Ms', 'p95Ms', 'p99Ms', 'gpuP95Ms', 'prepareP95Ms', 'geometryMeanMs',
                       'coldFirstDrawMs', 'meshLoadMs', 'maxRssKiB', 'meshBytes'):
            if not all(metric in p[m] for p in complete for m in ('baked', 'rig')):
                continue
            baked = [p['baked'][metric] for p in complete]
            rig = [p['rig'][metric] for p in complete]
            paired = [(b - a) / a * 100 for a, b in zip(baked, rig) if a > 0]
            row['metrics'][metric] = {'bakedMedian': statistics.median(baked), 'rigMedian': statistics.median(rig)}
            if paired:
                row['metrics'][metric].update(pairedChangePct=statistics.median(paired),
                    pairedChangePctBootstrap95=bootstrap_median(paired))
        summary.append(row)
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--kind', choices=('renderer', 'scene'), default='renderer')
    parser.add_argument('--save', type=Path)
    parser.add_argument('--scene-size', default='800x600', help='Scene window size; must fit the desktop')
    parser.add_argument('--pairs', type=int, default=10)
    parser.add_argument('--frames', type=int, default=240)
    parser.add_argument('--warmup', type=int, default=64)
    parser.add_argument('--units', type=int, nargs='+', default=[512, 2048])
    parser.add_argument('--cache', choices=('warm', 'miss'), nargs='+', default=['warm', 'miss'])
    parser.add_argument('--phases', type=int, default=32)
    args = parser.parse_args()
    if args.pairs < 1 or args.frames < 20 or args.warmup < 1:
        parser.error('Require positive pairs/warmup and at least 20 measured frames')
    if args.kind == 'scene' and not args.save:
        parser.error('--kind scene requires --save')
    try:
        scene_size = tuple(int(value) for value in args.scene_size.split('x'))
        if len(scene_size) != 2 or min(scene_size) < 1:
            raise ValueError()
    except ValueError:
        parser.error('--scene-size must be WIDTHxHEIGHT')
    args.binary = args.binary.resolve()
    args.assets = args.assets.resolve()
    args.output = args.output.resolve()
    if args.output.exists():
        parser.error('Choose a new output directory; existing evidence is never overwritten')
    args.output.mkdir(parents=True)
    sources = args.output / 'source-files'
    for relative in ('src/SConscript', 'tools/skins/rig_benchmark.cpp', 'tools/skins/game_preview.cpp', 'tools/skins/benchmark_rigs.py'):
        destination = sources / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / relative, destination)
    metadata = {'arguments': {k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()},
        'platform': platform.platform(), 'binarySha256': hashlib.sha256(args.binary.read_bytes()).hexdigest(),
        'source': capture(['git', 'rev-parse', 'HEAD']), 'base': capture(['git', 'rev-parse', 'origin/master']),
        'diff': capture(['git', 'diff']), 'cpu': capture(['lscpu']), 'gl': capture(['glxinfo', '-B']),
        'gpu': capture(['nvidia-smi', '--query-gpu=name,uuid,driver_version', '--format=csv']),
        'trackedAssets': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in args.assets.iterdir()
                          if p.suffix in ('.gsb', '.gsr', '.gsk', '.webp')}}
    if args.save:
        metadata['saveSha256'] = hashlib.sha256(args.save.read_bytes()).hexdigest()
    (args.output / 'metadata.json').write_text(json.dumps(metadata, indent=2)+'\n')
    records = []
    cases = [(units, cache) for units in (args.units if args.kind == 'renderer' else [0]) for cache in args.cache]
    for pair in range(args.pairs):
        for units, cache in (cases if pair % 2 == 0 else reversed(cases)):
            for mode in (('baked', 'rig') if pair % 2 == 0 else ('rig', 'baked')):
                stem = f'{pair:02d}-{units}-{cache}-{mode}'
                folder = args.output / stem
                folder.mkdir()
                env = dict(os.environ, SDL_VIDEODRIVER='x11', GLOB2_SKIN_RIGS='1' if mode == 'rig' else '0',
                    GLOB2_USER_DATA_DIR=str(folder / 'profile'), __GL_SYNC_TO_VBLANK='0')
                env.pop('LIBGL_ALWAYS_SOFTWARE', None)
                env.pop('GLOB2_SKIN_DEFORMATION', None)
                if args.kind == 'renderer':
                    command = [str(args.binary), str(args.assets), str(folder/'raw.json'), mode, cache,
                               str(units), str(args.frames), str(args.warmup), str(args.phases)]
                else:
                    env.update(SKIN_PREVIEW_SAVE=str(args.save.resolve()), SKIN_PREVIEW_CAPTURE='preview.bmp',
                        GLOB2_SKIN_PREVIEW_DIR=str(args.assets), SKIN_PREVIEW_BENCHMARK='benchmark',
                        SKIN_BENCH_FRAMES=str(args.frames+args.warmup), SKIN_BENCH_WARMUP=str(args.warmup),
                        SKIN_BENCH_UNCAPPED='1', SKIN_BENCH_FORCE_MISS='1' if cache == 'miss' else '0',
                        SKIN_PREVIEW_ADAPTIVE='0', SKIN_PREVIEW_ZOOM='1')
                    command = [str(args.binary), '-g', '-m', '-s', args.scene_size]
                (folder/'invocation.json').write_text(json.dumps({'command': command,
                    'environment': {k:v for k,v in env.items() if k.startswith(('SKIN_', 'GLOB2_', 'SDL_', '__GL_'))},
                    'loadBefore': os.getloadavg(), 'processesBefore': capture(['ps', '-eo', 'comm,pcpu', '--sort=-pcpu']), 'gpuBefore': capture(['nvidia-smi',
                        '--query-gpu=utilization.gpu,temperature.gpu,power.draw', '--format=csv'])}, indent=2)+'\n')
                start = time.monotonic()
                with (folder/'stdout.log').open('w') as log:
                    completed = subprocess.run(['/usr/bin/time', '-f', '%M', '-o', str(folder/'rss-kib.txt'),
                        *command], cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
                if completed.returncode:
                    raise RuntimeError(f'{stem} failed ({completed.returncode}); see {folder}/stdout.log')
                if args.kind == 'renderer':
                    raw = json.loads((folder/'raw.json').read_text())
                    samples = raw['samples']['wallMs']
                    row = {key: raw[key] for key in ('coldFirstDrawMs', 'meshLoadMs', 'meshBytes')}
                    row['prepareP95Ms'] = raw['prepareMs']['p95']
                    row['geometryMeanMs'] = statistics.mean(s['geometry']['ms'] for s in raw['samples']['scopes'])
                    if raw['gpuTimers']:
                        row['gpuP95Ms'] = raw['gpuMs']['p95']
                    if mode == 'rig' and raw['deformation'] == 'cpu-rig':
                        raise RuntimeError('Rig used CPU fallback')
                    renderer = raw['glRenderer']
                else:
                    decoded = [json.loads(line) for line in (folder/'stdout.log').read_text().splitlines() if line.startswith('{"adaptiveZoom"')]
                    raw = next(r for r in decoded if r['mode'] == 'skinned')
                    if (raw['width'], raw['height']) != scene_size:
                        raise RuntimeError('Unexpected Scene render resolution')
                    samples = raw['frameMs']
                    (folder/'raw.json').write_text(json.dumps(raw, indent=2)+'\n')
                    row = {'coldFirstDrawMs': raw['coldMs'], 'addedUnits': raw['addedUnits'],
                           'geometryMeanMs': raw['profile']['geometry']['totalMs']/raw['frames']}
                    renderer = raw['backend'].get('glRenderer', '')
                    previous = next((r for r in records if r['pair'] == pair and r['cache'] == cache), None)
                    if previous:
                        earlier = json.loads(Path(previous['rawPath']).read_text())
                        if earlier['stateChecksums'] != raw['stateChecksums'] or earlier['addedUnits'] != raw['addedUnits']:
                            raise RuntimeError('Paired Scene simulation states differ')
                    if raw['workerRig'] != (mode == 'rig') or raw['backend'].get('swapInterval') != 0:
                        raise RuntimeError('Scene backend mismatch')
                if not renderer:
                    raise RuntimeError('Missing hardware renderer identity')
                if any(s in renderer.lower() for s in ('llvmpipe', 'softpipe', 'swiftshader')):
                    raise RuntimeError(f'Software backend: {renderer}')
                if len(samples) != args.frames:
                    raise RuntimeError('Incomplete frame samples')
                row.update(kind=args.kind, units=units, cache=cache, mode=mode, pair=pair,
                    p50Ms=quantile(samples,.5), p95Ms=quantile(samples,.95), p99Ms=quantile(samples,.99),
                    maxRssKiB=int((folder/'rss-kib.txt').read_text().strip()), elapsedSeconds=time.monotonic()-start,
                    glRenderer=renderer, rawPath=str(folder/'raw.json'))
                records.append(row)
                (args.output/'runs.json').write_text(json.dumps(records, indent=2)+'\n')
                print(f'{stem}: p95={row["p95Ms"]:.3f} ms, RSS={row["maxRssKiB"]/1024:.1f} MiB', flush=True)
    (args.output/'summary.json').write_text(json.dumps(summarize(records), indent=2)+'\n')
    print(json.dumps(summarize(records), indent=2))


if __name__ == '__main__':
    main()
