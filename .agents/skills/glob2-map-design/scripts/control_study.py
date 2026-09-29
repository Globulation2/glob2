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
import argparse, collections, hashlib, json, math, os, random, re, statistics as st, sys, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[4]))
from tools.map_generation_study import load_catalog, generator_definition, generate_map, report_metrics
from concurrent.futures import ThreadPoolExecutor

def controls(binary, generator, catalog=None):
    definition = generator_definition(catalog if catalog is not None else load_catalog(binary), generator)
    selected = [c for c in definition['controls'] if c['id'] not in ('width', 'height', 'teams', 'workers')]
    return ({c['id']: c['values'] for c in selected}, {c['id']: c['default'] for c in selected})


def run(job):
    result = generate_map(job['binary'], job['method'], job['seed'], job['w'], job['h'],
                          job['teams'], job['set'], job.get('timeout', 120))
    row = {k: job[k] for k in ('study', 'control', 'value', 'seed', 'w', 'h', 'teams', 'set')}
    row.update(schema_version=2, generator=job['generator'], seconds=result['seconds'],
               ok=result['category'] == 'completed', category=result['category'],
               detail=result['detail'], returncode=result['returncode'])
    native = result['native'] or {}
    row['native_status'] = native.get('status')
    report = native.get('map_report') or {}
    try:
        row['m'], row['telemetry'] = report_metrics(report)
        row['generation'] = {k: v for k, v in report.get('generation', {}).items() if k != 'telemetry'}
    except (KeyError, TypeError, ValueError, AttributeError) as error:
        row.update(ok=False, category='execution_error', detail=f'Malformed map report: {error}', m={})
    return row


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('generator'); ap.add_argument('what', choices=['ablation', 'random', 'report'])
    ap.add_argument('--out', required=True); ap.add_argument('--binary', default='build/src/glob2')
    ap.add_argument('--jobs', type=int, default=8); ap.add_argument('--seeds', type=int, default=8)
    ap.add_argument('--timeout', type=float, default=120)
    ap.add_argument('--count', type=int, default=2000); ap.add_argument('--teams', default='2-8')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    catalog = load_catalog(a.binary)
    definition = generator_definition(catalog, a.generator)
    found, defaults = controls(a.binary, a.generator, catalog)
    if a.what == 'report':
        return report(a, found, defaults)
    lo, hi = (int(x) for x in a.teams.split('-'))
    if a.count < 0 or a.seeds < 1 or a.jobs < 1 or a.timeout <= 0 or not 1 <= lo <= hi:
        ap.error('Use positive seeds/jobs/timeout, a nonnegative count, and an ordered colony range')
    base = dict(binary=a.binary, generator=definition['id'], method=definition['method'], timeout=a.timeout)
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
    metadata = dict(schema_version=2, generator=definition, argv=sys.argv,
                    binary=str(Path(a.binary).resolve()),
                    binary_sha256=hashlib.sha256(Path(a.binary).read_bytes()).hexdigest())
    Path(a.out, a.what + '-metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
    start = time.monotonic()
    with ThreadPoolExecutor(a.jobs) as pool, open(os.path.join(a.out, a.what + '.jsonl'), 'w') as f:
        for row in pool.map(run, jobs):
            f.write(json.dumps(row) + '\n')
            f.flush()
    print(len(jobs), 'maps in', round(time.monotonic() - start), 's')

def corr(xs, ys):
    mx, my = st.mean(xs), st.mean(ys)
    sx = math.sqrt(sum((x - mx) ** 2 for x in xs)); sy = math.sqrt(sum((y - my) ** 2 for y in ys))
    return sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / (sx * sy) if sx and sy else 0.0

def report(a, found, defaults):
    def load(name):
        path = os.path.join(a.out, name + '.jsonl')
        if not os.path.exists(path):
            return []
        with open(path) as source:
            return [json.loads(line) for line in source]
    for name in ('ablation', 'random'):
        rows = load(name)
        if rows:
            bad = collections.Counter(re.sub(r'\d+', 'N', r['detail'])[:100] for r in rows if not r['ok'])
            print(f"# {name}: {sum(r['ok'] for r in rows)} of {len(rows)} generated and validated")
            print('  outcomes:', dict(collections.Counter(r.get('category', 'legacy') for r in rows)))
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
        means = {v: {k: st.mean(r['m'][k] for r in by[v] if k in r['m'])
                     for k in keys if all(k in r['m'] for r in by[v])} for v in by if by[v]}
        # Compare complete measurements only; an absent observation is not a zero.
        keys = [k for k in keys if all(k in means[v] for v in means)]
        moved = [k for k in keys if len({round(means[v][k], 3) for v in means}) > 1 and not k.startswith(('tel:settlement', 'tel-min:settlement', 'tel-max:settlement'))]
        # The metrics that follow the control most steadily first: noise moves too, but not in step.
        steady = lambda k: abs(corr(sorted(means), [means[v][k] for v in sorted(means)])) if len(means) > 2 else 1.0
        moved = sorted(moved, key=steady, reverse=True)[:6]
        print(f'\n## {c} (256x256, 4 colonies, mean of seeds; * default)')
        print('| value | ' + ' | '.join(moved) + ' |'); print('|---|' + '---|' * len(moved))
        order = sorted(means)
        for i, v in enumerate(order):
            dead = bool(keys) and i > 0 and all(round(means[v][k], 3) == round(means[order[i - 1]][k], 3) for k in keys if not k.startswith(('tel:settlement', 'tel-min:settlement', 'tel-max:settlement')))
            print(f"| {v}{'*' if v == defaults[c] else ''} | " + ' | '.join(f'{means[v][k]:.1f}' for k in moved) + ' |' + ('  <- DEAD STEP: same as the value above' if dead else ''))
        if not moved:
            print('(no consistently observed metric moved; inspect missing observations and paired previews)')
    rnd = [r for r in load('random') if r['ok'] and r['w'] == 256 and r['h'] == 256]
    if len(rnd) > 30:
        print(f'\n## Strongest correlations over random 256x256 rolls (n={len(rnd)})')
        keys = sorted({k for r in rnd for k in r['m'] if not k.startswith(('tel:settlement', 'tel-min:settlement', 'tel-max:settlement'))})
        keys = [k for k in keys if all(k in r['m'] for r in rnd)]
        for c in found:
            xs = [r['set'][c] for r in rnd]
            best = sorted(((abs(corr(xs, [r['m'][k] for r in rnd])), k) for k in keys), reverse=True)[:3]
            print(f'  {c:18s} ' + ', '.join(f'{k} r={corr(xs, [r["m"][k] for r in rnd]):+.2f}' for _, k in best))

if __name__ == '__main__':
    main()
