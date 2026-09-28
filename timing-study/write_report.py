import json, statistics
from pathlib import Path
p=Path(__file__).resolve().parent
plan=json.loads((p/'plan.json').read_text());a=json.loads((p/'analysis.json').read_text())
rows=[json.loads(s) for s in (p/'measurements.jsonl').read_text().splitlines()]
assert len(rows)==162 and sum(s['blocks'] for s in a)==54
names=['Arena 128², 2 players','Arena 256², 2 players','Arena 512², 4 players','Lakes 256², 4 players','Arena 256², additional seed, 4 players','Lakes 256², additional seed, 4 players','Lakes 256², four checkpoints']
def effect(s,k):
 d=s[k];l,h=d['ci95_percent'];return f"{d['saving_percent']:+.2f}% [{l:+.2f}, {h:+.2f}]"
text='''# Lazy building gradients: controlled timing study

The simple lazy implementation is a repeatable CPU improvement on every tested workload. It saves about 2–8% of total headless process CPU versus the original baseline. The larger building-heavy games benefit most. Keeping the PR's lazy mode disabled has a small cost in some cases, so lazy-versus-eager alone slightly overstates the net benefit.

These results supersede the earlier two-run timing estimates, including the apparent 256² slowdown. They do not establish the cause of that earlier variability. The original measurements remain available in the parent evidence directory.

Implementation: [d50968d06](https://github.com/Globulation2/glob2/commit/d50968d06125dc10fbf4b247e35d0526fe69af0b). Original baseline: [1c49596f5](https://github.com/Globulation2/glob2/commit/1c49596f5195188e7f078768f610454f7068d7c6). No game code changed for this study.

## Total CPU effect

Positive percentages mean less CPU time. Brackets are approximate pointwise 95% confidence intervals. Each repetition is a matched block containing all three variants.

| Workload | Repeats per variant | PR disabled vs baseline | Lazy vs PR disabled | Net lazy vs baseline |
| --- | ---: | ---: | ---: | ---: |
'''
for name,s in zip(names,a):text+=f"| {name} | {s['blocks']} | {effect(s,'refactor_vs_base')} | {effect(s,'lazy_vs_eager')} | **{effect(s,'lazy_vs_base')}** |\n"
text+='''
![CPU comparisons](cpu-comparison.png)

“PR disabled” includes all integration/refactoring overhead with lazy mode off; it does not isolate the shared expansion kernel. The within-PR comparison uses exactly the same executable for both modes. There is no single representative aggregate percentage: the cases have different sizes, durations and building workloads.

## Absolute costs and memory

CPU values below are arithmetic means in seconds. Memory is the difference between means of per-process peak RSS, not a precise measurement of allocated cache memory. The effect estimates above use paired geometric ratios, so they need not equal ratios of these rounded arithmetic means.

| Workload | Baseline CPU s | PR disabled CPU s | Lazy CPU s | Extra lazy RSS vs PR disabled MiB | Extra lazy RSS vs baseline MiB |
| --- | ---: | ---: | ---: | ---: | ---: |
'''
for name,s in zip(names,a):
 b,e,l=[s['variants'][v] for v in ['base','eager','lazy']]
 text+=f"| {name} | {b['cpu_mean_s']:.3f} | {e['cpu_mean_s']:.3f} | {l['cpu_mean_s']:.3f} | {l['peak_rss_mean_mib']-e['peak_rss_mean_mib']:+.2f} | {l['peak_rss_mean_mib']-b['peak_rss_mean_mib']:+.2f} |\n"
text+='''
## Where the time goes

These are elapsed-time instrumentation scopes, not process CPU counters. Building time sums `gradient.building` and `gradient.building_resume`; this includes initialization, seed scanning and resumed expansion. Initialization and seed scanning remain eager. The study does not separately measure their costs or estimate hypothetical designs that eliminate them. Resource gradients are unchanged and shown as context. Inclusive scopes must not be summed into a total CPU budget.

| Workload | Building seconds: PR disabled → lazy | Reduction in building scope | Resource seconds: PR disabled → lazy |
| --- | ---: | ---: | ---: |
'''
for name,s in zip(names,a):
 e,l=[s['variants'][v] for v in ['eager','lazy']];eb=e['building_scope_mean_s'];lb=l['building_scope_mean_s']
 text+=f"| {name} | {eb:.3f} → {lb:.3f} | {100*(1-lb/eb):.1f}% | {e['resource_scope_mean_s']:.3f} → {l['resource_scope_mean_s']:.3f} |\n"
s=a[-1];e,l=[s['variants'][v] for v in ['eager','lazy']];d=s['save_mean_extra_ms'];low,high=d['ci95']
text+=f'''
## Saving and latency

The checkpoint workload performs four synchronous headless saves, one every 4,096 ticks. Across six repeats per variant, mean `save.serialize` elapsed time rises from **{e['save_scope_mean_ms']:.1f} ms to {l['save_scope_mean_ms']:.1f} ms**. The paired increase is **{d['mean_difference']:.1f} ms per save**, with a 95% interval of **{low:.1f}–{high:.1f} ms**. The largest observed serialization scope is {e['save_scope_max_worst_ms']:.1f} ms with lazy disabled and {l['save_scope_max_worst_ms']:.1f} ms with lazy enabled. Despite this cost, the whole checkpoint run still saves **{s['lazy_vs_base']['saving_percent']:.1f}%** CPU versus baseline.

Serialization finishes pending fields. These headless saves include synchronous hashing/writing work; GUI asynchronous autosave has a different I/O path. This measures a real save-related cost, but is not a prediction of GUI autosave frame stalls. Storage/cache effects remain in these measurements.

Per-run maximum `loop.work` scopes were also retained in `analysis.json`. A handful of maxima is not a reliable frame-latency distribution, and no interactive FPS or p95/p99 frame claims are made.

## Fixed protocol and uncertainty

- **162 measured runs in 54 matched blocks**, plus three excluded warmups. Twelve repeats per variant for each two-player arena; six for each other scenario.
- Each block runs original baseline, PR disabled and PR lazy back-to-back. All six variant permutations occur exactly once per six repeats. Case order is shuffled each round with fixed scheduling seed 20260928. The full plan was fixed before measurements; no runs were dropped or added based on their performance.
- Apple M3, 24 GiB, macOS arm64, release builds with Apple clang 21.0.0. The exact binaries and fixtures are SHA-256 identified in `plan.json`; source/compiler metadata is in the parent evidence directory.
- Primary metric is child-process user + system CPU from `wait4`, including startup and shutdown. Wall time, peak RSS, faults, context switches, final performance scopes and game results are retained for every run.
- Every variant uses team-timeline telemetry and normal performance scopes. Renderer and checksum telemetry are off. Saves are off except in the checkpoint case. No compilation or compression ran concurrently with measurements.
- Every trial checks final ticks, termination and team results against its baseline. These coarse outcomes agree in all runs; they do not replace per-tick correctness validation. The parent evidence retains the separate 65,442-tick-per-mode checksum comparisons and the existing Maxima diagnostic save caveat.
- Confidence intervals use a Student-t interval on paired log CPU ratios across blocks, transformed back to percent savings. They estimate repeatability for these fixed games, not variation across all maps, machines or future games. Intervals are pointwise and not adjusted for multiple comparisons; with six or twelve blocks, distributional assumptions remain relevant.
'''
ratios=[r['wall_s']/r['cpu_s'] for r in rows]
power={r[k]['power_source'] for r in rows for k in ['before','after']}
assert power=={'ac'}
text+=f'''- All measured runs began and ended on AC power. Low-power mode was disabled. Wall/CPU ratios ranged from {min(ratios):.3f} to {max(ratios):.3f}; median {statistics.median(ratios):.3f}. `pmset` reported no recorded thermal/performance warnings. These checks do not establish fixed CPU frequency, core affinity or an otherwise perfectly idle desktop. Earlier battery warmup conditions are retained separately.

## Workloads

| Case / fixture | Generator | Map seed | Game seed | Players | Tick target | Save interval |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
'''
for c in plan['cases']:text+=f"| {c['name']} | {c['generator']} | {c['map_seed']} | {c['game_seed']} | {len(c['players'])} | {c['ticks']} | {c['save_every'] or 'none'} |\n"
text+='''
Two-player games use Nicowar/Warrush. Four-player games add Cortex/Maxima. The arena128 game terminates at tick 16,290. The two additional seeded cases run to 8,192 ticks; they sample different game states as well as different maps. The checkpoint case uses the same map and game seed as lakes256-4. Map generation was outside measured time.

## Assessment

Keep the simple lazy design: the measured total-CPU benefit is modest but repeatable, and building-gradient scopes improve materially. Account for retained memory, extra work at serialization, and the small disabled-mode overhead. There is no evidence here to justify more complex initialization/seed-scanning designs, a universal speedup claim, or default enablement without further review.

The remaining practical coverage gaps are interactive play, GUI autosave/frame latency, longer and more varied games, other CPUs, and full-game cross-platform checksum equivalence. The feature remains opt-in; this study does not change defaults or merge the PR.

## Evidence and reproduction

- `plan.json`: preselected cases, execution order, seeds and hashes.
- `measurements.jsonl`, `warmups.jsonl`: all commands, counters, scopes, environment snapshots and results.
- `analysis.json`, `analyze.py`: derived paired effects, intervals, phase costs, memory and save measurements.
- `study.py`: exact original local driver; its absolute paths describe the original run.
- `reproduce.py`: portable fixture-based runner using explicit executable paths; `fixtures/` contains all seven maps.
- `raw-*.tar.gz`: original logs and result JSON, grouped by case. Redundant checkpoint game files stay local; prior correctness saves are retained in the parent evidence archives.
- `manifest.json`: SHA-256 hashes for published evidence files. `completed.json` and `driver.log` record completion.

Build baseline and implementation in separate source checkouts using the same compiler and release settings. From the implementation source checkout, with repository assets available:

```sh
CCACHE=1 scons -j6 release=1 server=0 build/darwin/client/release/src/glob2
python3 /path/to/evidence/timing-study/reproduce.py --base-binary /path/to/baseline/glob2 --pr-binary /path/to/implementation/glob2 --output artifacts/timing-reproduction
```

Use the corresponding native build target on another platform. Reproduction requires a POSIX host (`wait4`); the runner accounts for macOS/Linux RSS units. It does not set CPU affinity or power policy. Use a fresh output directory, stable power and no competing heavy work. Copy `analyze.py` beside the new `plan.json`/`measurements.jsonl` to analyze a reproduction. `--case` selects a single recorded scenario. The portable runner is syntax/help checked; the original `study.py` is the driver used for all reported measurements.
'''
(p/'report.md').write_text(text)
