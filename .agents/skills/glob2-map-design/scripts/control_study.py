#!/usr/bin/env python3
"""One-machine control study for any generator: every control on its own, then random rolls of everything.

Reads the generator's controls from `glob2 --list-map-generators ID`, runs the native binary in parallel
and keeps one compact JSON line per map. Run from the repository root:

  python3 .agents/skills/glob2-map-design/scripts/control_study.py hidden-oasis ablation --out DIR
  python3 .agents/skills/glob2-map-design/scripts/control_study.py hidden-oasis random --count 2000 --out DIR
  python3 .agents/skills/glob2-map-design/scripts/control_study.py hidden-oasis report --out DIR

`ablation` generates every value of every control alone (default 8 seeds at 256x256 with 4 colonies) and
each control's extremes on a small, a large and a rectangular map. `random` rolls every control, seven
shapes and the colony counts given by --teams. `report` prints, per control, the mean of every metric
at every value, flags DEAD STEPS (adjacent values whose maps measure the same), groups refusals by
message, and gives each control's correlation with every metric over the random rolls. About 1,000
maps a minute on eight cores. Copy the binary first (`--binary`) if you will rebuild while it runs.

Metrics: terrain shares, resource tiles, 4x4 building sites, mean fertility, generation seconds, and the
mean of every numeric telemetry key the generator records (docs/map-generators/TELEMETRY.md): give a
control a telemetry measure of what it places and the report shows whether it moved.
"""
import argparse, collections, json, math, os, random, re, statistics as st, subprocess, tempfile, time
from concurrent.futures import ThreadPoolExecutor

def controls(binary, generator):
    out = subprocess.run([binary, '--list-map-generators', generator], capture_output=True, text=True).stdout
    found, defaults = {}, {}
    for line in out.splitlines():
        m = re.match(r'\s+([\w-]+)=(-?\d+)\s+values: (.*)', line)
        if m and m.group(1) not in ('width', 'height', 'teams', 'workers'):
            # A choice lists its values as `0(Brief) 1(Normal)`: the stored value is the number, the
            # name is only what the lobby shows. Without this a generator with any choice control
            # cannot be studied at all.
            found[m.group(1)] = [int(v) for v in re.findall(
                r'(?:^|\s)(-?\d+)(?:\([^)]*\))?(?=\s|$)', m.group(3))]
            defaults[m.group(1)] = int(m.group(2))
    return found, defaults

def run(job):
    fd, path = tempfile.mkstemp(suffix='.json'); os.close(fd)
    cmd = [job['binary'], '--generate-map', job['generator'], '--seed', str(job['seed']), '--width', str(job['w']),
           '--height', str(job['h']), '--teams', str(job['teams']), '--json', path]
    for k, v in job['set'].items():
        cmd += ['--set', f'{k}={v}']
    start = time.time()
    p = subprocess.run(cmd, capture_output=True, text=True)
    row = {k: job[k] for k in ('study', 'control', 'value', 'seed', 'w', 'h', 'teams', 'set')}
    row.update(seconds=round(time.time() - start, 3), ok=False, detail='no result line')
    for line in (p.stdout + p.stderr).splitlines():
        if ' revision ' in line and ']:' in line:
            row['ok'] = '[complete]' in line
            row['detail'] = '' if row['ok'] else line.split(']: ', 1)[-1]
    try:
        d = json.load(open(path))
        tel = collections.defaultdict(list)
        for e in (d['generation'].get('telemetry') or {}).get('records', []):
            if isinstance(e.get('value'), (int, float)) and not isinstance(e.get('value'), bool):
                tel[e['key']].append(e['value'])
        m = {'tel:' + k: sum(v) / len(v) for k, v in tel.items()}
        if row['ok']:
            for k, v in d['terrain'].items():
                if isinstance(v, dict):
                    m['terrain%:' + k] = v['percent']
            for k, v in d['resources']['types'].items():
                m['tiles:' + k] = v['coverage']['tiles']
            m['sites4x4'] = d['space']['build_sites_4x4']
            m['fertility'] = d['fertility']['all_tiles']['mean']
        row['m'] = m
    except Exception as ex:
        row['parse'] = str(ex)[:80]
    os.unlink(path)
    return row

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('generator'); ap.add_argument('what', choices=['ablation', 'random', 'report'])
    ap.add_argument('--out', required=True); ap.add_argument('--binary', default='build/src/glob2')
    ap.add_argument('--jobs', type=int, default=8); ap.add_argument('--seeds', type=int, default=8)
    ap.add_argument('--count', type=int, default=2000); ap.add_argument('--teams', default='2-8')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    found, defaults = controls(a.binary, a.generator)
    if a.what == 'report':
        return report(a, found, defaults)
    lo, hi = (int(x) for x in a.teams.split('-'))
    base = dict(binary=a.binary, generator=a.generator)
    jobs = []
    if a.what == 'ablation':
        for seed in range(1, a.seeds + 1):
            jobs.append(dict(base, study='baseline', control='', value=0, seed=seed, w=256, h=256, teams=4, set={}))
            for c, values in found.items():
                jobs += [dict(base, study='ablation', control=c, value=v, seed=seed, w=256, h=256, teams=4, set={c: v})
                         for v in values if v != defaults[c]]
        for w, h, teams in ((128, 128, min(3, hi)), (512, 512, min(6, hi)), (512, 256, min(5, hi))):
            for seed in range(1, 4):
                jobs.append(dict(base, study='baseline', control='', value=0, seed=seed, w=w, h=h, teams=teams, set={}))
                for c, values in found.items():
                    jobs += [dict(base, study='extremes', control=c, value=v, seed=seed, w=w, h=h, teams=teams, set={c: v})
                             for v in (values[0], values[-1])]
    else:
        rng = random.Random(20260917)
        shapes = [(128, 128), (256, 256), (256, 256), (512, 512), (512, 256), (256, 512), (128, 256), (256, 128)]
        for n in range(a.count):
            w, h = rng.choice(shapes)
            jobs.append(dict(base, study='random', control='', value=0, seed=100000 + n, w=w, h=h,
                             teams=rng.randint(lo, hi), set={c: rng.choice(v) for c, v in found.items()}))
    start = time.time()
    with ThreadPoolExecutor(a.jobs) as pool, open(os.path.join(a.out, a.what + '.jsonl'), 'w') as f:
        for row in pool.map(run, jobs):
            f.write(json.dumps(row) + '\n')
    print(len(jobs), 'maps in', round(time.time() - start), 's')

def corr(xs, ys):
    mx, my = st.mean(xs), st.mean(ys)
    sx = math.sqrt(sum((x - mx) ** 2 for x in xs)); sy = math.sqrt(sum((y - my) ** 2 for y in ys))
    return sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / (sx * sy) if sx and sy else 0.0

def report(a, found, defaults):
    def load(name):
        path = os.path.join(a.out, name + '.jsonl')
        return [json.loads(l) for l in open(path)] if os.path.exists(path) else []
    for name in ('ablation', 'random'):
        rows = load(name)
        if rows:
            bad = collections.Counter(re.sub(r'\d+', 'N', r['detail'])[:100] for r in rows if not r['ok'])
            print(f"# {name}: {sum(r['ok'] for r in rows)} of {len(rows)} generated and validated")
            for k, v in bad.most_common():
                print(f'  {v:5d}  {k}')
    rows = [r for r in load('ablation') if r['ok'] and r['w'] == 256 and r['h'] == 256]
    base = [r for r in rows if r['study'] == 'baseline']
    for c, values in found.items():
        by = collections.defaultdict(list)
        for r in rows:
            if r['study'] == 'ablation' and r['control'] == c:
                by[r['value']].append(r)
        by[defaults[c]] = base
        keys = sorted({k for v in by.values() for r in v for k in r['m']})
        means = {v: {k: st.mean(r['m'].get(k, 0) for r in by[v]) for k in keys} for v in by if by[v]}
        moved = [k for k in keys if len({round(means[v][k], 3) for v in means}) > 1 and not k.startswith('tel:settlement')]
        # The metrics that follow the control most steadily first: noise moves too, but not in step.
        steady = lambda k: abs(corr(sorted(means), [means[v][k] for v in sorted(means)])) if len(means) > 2 else 1.0
        moved = sorted(moved, key=steady, reverse=True)[:6]
        print(f'\n## {c} (256x256, 4 colonies, mean of seeds; * default)')
        print('| value | ' + ' | '.join(moved) + ' |'); print('|---|' + '---|' * len(moved))
        order = sorted(means)
        for i, v in enumerate(order):
            dead = i > 0 and all(round(means[v][k], 3) == round(means[order[i - 1]][k], 3) for k in keys if not k.startswith('tel:settlement'))
            print(f"| {v}{'*' if v == defaults[c] else ''} | " + ' | '.join(f'{means[v][k]:.1f}' for k in moved) + ' |' + ('  <- DEAD STEP: same as the value above' if dead else ''))
        if not moved:
            print('(no metric moved: a visual-only control, or a dead one; look at a pair of previews)')
    rnd = [r for r in load('random') if r['ok'] and r['w'] == 256 and r['h'] == 256]
    if len(rnd) > 30:
        print(f'\n## Strongest correlations over random 256x256 rolls (n={len(rnd)})')
        keys = sorted({k for r in rnd for k in r['m'] if not k.startswith('tel:settlement')})
        for c in found:
            xs = [r['set'][c] for r in rnd]
            best = sorted(((abs(corr(xs, [r['m'].get(k, 0) for r in rnd])), k) for k in keys), reverse=True)[:3]
            print(f'  {c:18s} ' + ', '.join(f'{k} r={corr(xs, [r["m"].get(k, 0) for r in rnd]):+.2f}' for _, k in best))

if __name__ == '__main__':
    main()
