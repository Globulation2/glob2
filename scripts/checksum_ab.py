#!/usr/bin/env python3
"""Play identical headless games with two glob2 builds, with and without the
team-timeline telemetry switch, and require byte-identical per-tick checksum
sidecars across all four runs of every scenario.

usage: checksum_ab.py BASE_GLOB2 PR_GLOB2 OUT_DIR [--ticks N] [--jobs N]
       [--load SAVE ...]   (continuations of saved games instead of new games)
"""
import argparse, concurrent.futures as cf, hashlib, itertools, json, os, re, shutil, subprocess, sys
from pathlib import Path

AIS = ['numbi', 'castor', 'warrush', 'econo', 'nicowar', 'maxima', 'cortex', 'cabino']
MAPS = ['FourSquares1', 'Archipelago', 'Garden_3', 'Muka', 'Triangle', 'Oazis']
SEEDS = [11, 4242]


def run(binary, out, args, telemetry):
    if out.exists():
        shutil.rmtree(out)
    cmd = [binary, '--run-game', *args, '--output-dir', str(out)]
    for t in telemetry:
        cmd += ['--telemetry', t]
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace')
    (out.parent / (out.name + '.log')).write_text(' '.join(cmd) + '\n' + p.stdout)
    sidecar = out / 'game.replay.checksums'
    if p.returncode != 0 or not sidecar.exists():
        return None, p.returncode, p.stdout[-2000:]
    digest, size = hashlib.sha256(sidecar.read_bytes()).hexdigest(), sidecar.stat().st_size
    shutil.rmtree(out)  # sidecars are large; keep the digest and the log
    return digest, p.returncode, size


def team_count(binary, map_file, scratch):
    # The engine names no count when a lineup has the wrong size, so try each size.
    for n in range(1, 17):
        out = scratch / ('probe-' + map_file.stem)
        shutil.rmtree(out, ignore_errors=True)
        cmd = [binary, '--run-game', '--map-file', str(map_file), '--game-seed', '1', '--ticks', '2',
               '--output-dir', str(out)] + ['--player', 'numbi'] * n
        if subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0:
            shutil.rmtree(out, ignore_errors=True)
            return n
    raise SystemExit(f'cannot find the team count of {map_file}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('base'); ap.add_argument('pr'); ap.add_argument('out')
    ap.add_argument('--maps-dir', default='maps')
    ap.add_argument('--only', nargs='*', default=None)
    ap.add_argument('--ticks', type=int, default=6000)
    ap.add_argument('--jobs', type=int, default=8)
    ap.add_argument('--load', nargs='*', default=[])
    a = ap.parse_args()
    out = Path(a.out); out.mkdir(parents=True, exist_ok=True)
    scenarios = []
    if a.load:
        for save in a.load:
            scenarios.append((Path(save).name, ['--load-game', save, '--ticks', str(a.ticks)]))
    else:
        rotation = itertools.cycle(AIS)
        for name in (a.only or MAPS):
            m = Path(a.maps_dir) / f'{name}.map.gz'
            teams = team_count(a.base, m, out)
            for seed in SEEDS:
                lineup = [next(rotation) for _ in range(teams)]
                args = ['--map-file', str(m), '--game-seed', str(seed), '--ticks', str(a.ticks)]
                for ai in lineup:
                    args += ['--player', ai]
                scenarios.append((f'{name}-s{seed}', args))
    variants = [('base', a.base, ['checksums']), ('base-timeline', a.base, ['checksums', 'team-timeline']),
                ('pr', a.pr, ['checksums']), ('pr-timeline', a.pr, ['checksums', 'team-timeline'])]
    jobs = {}
    with cf.ThreadPoolExecutor(a.jobs) as pool:
        for name, args in scenarios:
            for vname, binary, tel in variants:
                jobs[(name, vname)] = pool.submit(run, binary, out / name / vname, args, tel)
    summary, ok = [], True
    for name, args in scenarios:
        results = {v: jobs[(name, v)].result() for v, _, _ in variants}
        digests = {v: r[0] for v, r in results.items()}
        same = None not in digests.values() and len(set(digests.values())) == 1
        ok &= same
        size = results['base'][2] if results['base'][0] else None
        summary.append({'scenario': name, 'args': args, 'identical': same, 'sidecar_bytes': size,
                        'sha256': digests, 'failures': {v: r[2] for v, r in results.items() if r[0] is None}})
        print(f"{'OK  ' if same else 'DIFF'} {name}: {digests['base']}")
    (out / 'summary.json').write_text(json.dumps(summary, indent=1))
    print('ALL IDENTICAL' if ok else 'MISMATCH')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
