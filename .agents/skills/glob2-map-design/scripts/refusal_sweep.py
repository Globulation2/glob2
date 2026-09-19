#!/usr/bin/env python3
"""How often does one control value refuse? Many seeds at a few values of one control.

  python3 refusal_sweep.py tug lakes 0,10,20,25,30,35,40 --seeds 40
"""
import argparse, json, os, subprocess, tempfile
from concurrent.futures import ThreadPoolExecutor


def run(job):
    fd, path = tempfile.mkstemp(suffix='.json')
    os.close(fd)
    cmd = [job['binary'], '--generate-map', job['gen'], '--seed', str(job['seed']),
           '--width', str(job['w']), '--height', str(job['h']), '--teams', str(job['teams']),
           '--json', path, '--set', f"{job['control']}={job['value']}"]
    p = subprocess.run(cmd, capture_output=True, text=True)
    text = p.stdout + p.stderr
    ok = '[complete]' in text
    detail = ''
    for line in text.splitlines():
        if ' revision ' in line and ']:' in line and not ok:
            detail = line.split(']: ', 1)[-1]
    extra = {}
    try:
        d = json.load(open(path))
        if ok:
            extra['water%'] = d['terrain']['water']['percent']
            extra['sites4x4'] = d['space']['build_sites_4x4']
    except Exception:
        pass
    os.unlink(path)
    return dict(job, ok=ok, detail=detail, **extra)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('gen'); ap.add_argument('control'); ap.add_argument('values')
    ap.add_argument('--seeds', type=int, default=40)
    ap.add_argument('--binary', default='build/src/glob2')
    ap.add_argument('--jobs', type=int, default=16)
    ap.add_argument('--w', type=int, default=256)
    ap.add_argument('--h', type=int, default=256)
    ap.add_argument('--teams', type=int, default=4)
    a = ap.parse_args()
    values = [int(v) for v in a.values.split(',')]
    jobs = [dict(binary=a.binary, gen=a.gen, control=a.control, value=v, seed=s,
                 w=a.w, h=a.h, teams=a.teams)
            for v in values for s in range(1, a.seeds + 1)]
    with ThreadPoolExecutor(a.jobs) as pool:
        rows = list(pool.map(run, jobs))
    print(f'{a.gen} {a.control}, {a.seeds} seeds at {a.w}x{a.h} teams={a.teams}\n')
    print(f'{a.control:>8} | refused | rate  | mean water% | mean sites4x4 | commonest refusal')
    for v in values:
        got = [r for r in rows if r['value'] == v]
        bad = [r for r in got if not r['ok']]
        good = [r for r in got if r['ok']]
        water = sum(r.get('water%', 0) for r in good) / len(good) if good else 0
        sites = sum(r.get('sites4x4', 0) for r in good) / len(good) if good else 0
        why = max({r['detail'][:60] for r in bad}, key=lambda d: sum(
            1 for r in bad if r['detail'][:60] == d), default='')
        print(f'{v:>8} | {len(bad):>7} | {len(bad)/len(got):>5.0%} | {water:>11.1f} | '
              f'{sites:>13.0f} | {why}')


if __name__ == '__main__':
    main()
