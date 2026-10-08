"""Owner timing of building-field depth variants on busy Oazis checkpoints.

Each run loads a checkpoint, warms up 512 ticks, then measures 2048 ticks.
Perf samples whose window starts after the warmup are summed per scope.
Checksums of every variant must agree: the depth never changes results."""
import json, os, shlex, subprocess, sys, itertools, statistics
from pathlib import Path
out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
base, cand = sys.argv[2], sys.argv[3]
repeats = int(sys.argv[4]) if len(sys.argv) > 4 else 3
ckpts = [8192, 12288, 16384]
variants = {'master': (base, {}), **{p: (cand, {'GLOB2_BUILDING_DEPTH': p}) for p in ('l0100', 'l0200', 'l0300', 'l0500')},
            'lazy': (cand, {'GLOB2_BUILDING_DEPTH': 'lazy'}), 'full': (cand, {'GLOB2_BUILDING_DEPTH': 'full'})}
SCOPES = ('loop.work', 'simulation.tick', 'gradient.building', 'gradient.building_resume')
rows = []
for r in range(repeats):
    for c in ckpts:
        names = list(variants)
        names = names[r % len(names):] + names[:r % len(names)]  # rotate order per repeat
        for name in names:
            binary, env = variants[name]
            d = out / f'{name}-{c}-{r}'
            cmd = ['taskset', '-c', '24-31', binary, '--run-game', '--load-game',
                   f'/home/bradley/glob2-depth-model/artifacts/oazis-ckpt/checkpoint-{c}.game.gz', '--ticks', str(c + 4096),
                   '--benchmark-warmup', '2048', '--telemetry', 'team-timeline', '--telemetry', 'checksums',
                   '--compute-threads', '4', '--output-dir', str(d)]
            p = subprocess.run(cmd, env={**os.environ, **env}, capture_output=True, text=True,
                               cwd='/home/bradley/glob2-depth-model')
            totals = dict.fromkeys(SCOPES, 0)
            for line in p.stdout.splitlines():
                if not line.startswith('GLOB2_PERF_SAMPLE'):
                    continue
                f = dict(kv.split('=', 1) for kv in shlex.split(line)[1:] if '=' in kv)
                if int(f['tick_start']) >= c + 2048 and f.get('scope') in totals:
                    value = f.get('total_ns')
                    if value in (None, 'na'):
                        value = f.get('estimated_total_ns', '0')
                    totals[f['scope']] += int(float(value)) if value != 'na' else 0
            res = json.loads((d / 'result.json').read_text())
            row = {'variant': name, 'checkpoint': c, 'repeat': r, 'exit': p.returncode,
                   'run_cpu_ns': res.get('benchmark_run_cpu_ns'), 'join_wait_ns': res.get('compute_join_wait_ns'),
                   'building_gradient_wait_ns': res.get('building_gradient_wait_ns'), **totals}
            rows.append(row); print(json.dumps(row), flush=True)
            (out / 'rows.json').write_text(json.dumps(rows, indent=1))
