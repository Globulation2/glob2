from pathlib import Path
import json,csv,statistics
B=Path(__file__).parent;S=json.loads((B/'summary.json').read_text());W=json.loads((B/'workloads.json').read_text());R=list(csv.DictReader(open(B/'per-case.csv')))
def group(subset='movement',variant=1,dedup=False,key='all',value='all'):
 return next(g for g in S['groups'] if (g['subset'],g['variant'],g['dedup'],g['group'],g['value'])==(subset,variant,dedup,key,value))
def pct(x):return '—' if x is None else f'{x:.2f}%'
def rate(g,k):return pct(g['metrics'][k]['pct'])
def table_groups(key):
 vals=sorted({g['value'] for g in S['groups'] if g['group']==key},key=str)
 lines=['| Group | Cases with movement | Requests | Direct progress | Turning progress | Turning wrong way |','|---|---:|---:|---:|---:|---:|']
 for v in vals:
  a=group(variant=0,key=key,value=v);b=group(key=key,value=v)
  lines.append(f"| {v} | {b['cases_with_requests']} | {b['n']:,} | {rate(a,'progress')} | {rate(b,'progress')} | {rate(b,'worse')} |")
 return '\n'.join(lines)
def subset_table(dedup=False):
 lines=['| Route/request | Decisions | Direct progress | Turning progress | Turning wrong way |','|---|---:|---:|---:|---:|']
 for key in ['movement','footprint','combat','clearing','origin-open','origin-blocked','origin-unreachable','service-selection']:
  a=group(key,0,dedup);b=group(key,1,dedup)
  lines.append(f"| {key} | {b['n']:,} | {rate(a,'progress')} | {rate(b,'progress')} | {rate(b,'worse')} |")
 return '\n'.join(lines)
case_rows=[r for r in R if r['subset']=='movement' and r['variant']=='1' and r['dedup']=='False' and int(r['n'])>=100]
lowest=sorted(case_rows,key=lambda r:int(r['progress'])/int(r['n']))[:12]
weak=['| Setup | Size | Format | Requests | Turning progress | Turning wrong way | No route/exit |','|---|---|---|---:|---:|---:|---:|']
for r in lowest:weak.append(f"| {r['case']} | {r['width']}×{r['height']} | {r['format']} | {int(r['n']):,} | {100*int(r['progress'])/int(r['n']):.2f}% | {100*int(r['worse'])/int(r['n']):.2f}% | {100*(int(r['n'])-int(r['route_exists']))/int(r['n']):.2f}% |")
all_cases=['| Setup | Map size | Format | AI players | Pending movement decisions | Turning progress | Turning wrong way |','|---|---|---|---|---:|---:|---:|']
for w in W:
 r=next((r for r in R if r['case']==w['id'] and r['subset']=='movement' and r['variant']=='1' and r['dedup']=='False'),None)
 n=int(r['n']) if r else 0
 prog=pct(100*int(r['progress'])/n) if n else '—';wrong=pct(100*int(r['worse'])/n) if n else '—'
 all_cases.append(f"| {w['id']} | {w['width']}×{w['height']} | {w['format']} | {'/'.join(w['players'])} | {n:,} | {prog} | {wrong} |")
(B/'cases.md').write_text('\n'.join(all_cases)+'\n')
g=group();formats=sorted({w['format'] for w in W});ais=sorted({p for w in W for p in w['players']});all_movement=g['n']
report=f'''# Pending-direction heuristic: wider one-seed coverage

Accuracy-only experiment. Production PR 1197 remains unchanged at d1c0d4d50109b6f7d30f843368d30406f515b84f. The heuristic never moves a simulated unit; it is evaluated privately at actual pending requests.

## Coverage

{S['completed']}/{len(W)} setups completed, {S['ticks']:,} actual ticks, {S['pending_queries']:,} sampled pending route queries, including {all_movement:,} pending movement decisions. Each setup uses exactly one game seed (42). Each procedural setup uses exactly one map seed (713), with candidate selection disabled (`--candidates 0`); there are no seed sweeps or successful-map rerolls.

67 playable generator implementations, all 23 bundled map files, and one additional generated 16-team stress format: 91 setups. The extra setup uses shattered-coast at 512×256 with 8v8; its ordinary generator case is a different format/layout. Dimensions range 64–512, including rectangles. Formats: {', '.join(formats)}. AI types: {', '.join(ais)}. [Complete case table](cases.md), workloads.json and generators.json retain exact inputs and generator revisions.

The scheduling policy is unchanged from the earlier measurement: global four map-gradient admissions per tick, not the separate PR1220 per-team proposal.

This is deliberately heterogeneous coverage. One seed per setup cannot establish a generator's typical rate or rank formats causally: dimensions, AI composition, alliances and terrain all vary. No per-generator confidence intervals are claimed. Both request-weighted totals and equally weighted case means are reported to expose domination by busy matches.

## Results by route

Progress means the legal proposed step reduces remaining route cost in a freshly computed current-world gradient. It need not be an optimal step. Wrong way means remaining route cost increases. The denominator includes destinations with no reachable route or legal reachable exit; those cannot produce useful route progress. summary.json separately reports progress conditional on a route existing. Other outcomes include blocked/waiting, unchanged cost, or leaving the reachable route field; all remain in the denominators. Origins already at a route goal are excluded.

{subset_table()}

Overall flyer-turn progress: request-weighted {rate(g,'progress')}; mean case rate {pct(g['metrics']['progress']['mean_case_pct'])}; median case rate {pct(g['metrics']['progress']['median_case_pct'])}. {g['cases_with_requests']} cases have pending movement samples. Cases without any sampled pending queries: {', '.join(S['cases_without_pending_queries']) or 'none'}. The Bastion Keys setup had pending selection queries but no pending movement queries. A zero-count category is unobserved coverage, not proof of correctness.

## Spread across setups

![Accuracy spread](accuracy-spread.png)

The chart and weak-case table require at least 100 pending movement requests per setup to reduce unstable rankings from very small samples. All cases, including smaller samples, remain in cases.md and in the aggregate results. The grey comparison band is the earlier two-map request-weighted result, not a confidence interval.

{chr(10).join(weak)}

## By source

{table_groups('source')}

## By match format

{table_groups('format')}

Formats have unequal case counts and different generators/AIs; these are descriptive samples, not controlled estimates of an alliance policy's effect.

## Repeated locations counted once

Deduplication retains the first query per case, query subset, unit identity, building identity, route, swim class and coordinate.

{subset_table(True)}

## Measurement and reproducibility

Uses the frozen release diagnostic from the earlier 64-game experiment. experiment.json records both binary hashes; prior-source-reference.json identifies the archived source patch, private headers, compiler commands and original checks. The probe implements the ordinary unconstrained flyer heading: wrapped displacement toward building centre (flag position for flags), rounded to eight directions with the existing 2.41 threshold. Direct policy waits if the intended step is blocked. Turning policy tries +1, -1, +2, -2, +3, -3, +4, with normal ground terrain/collision checks; waits if none is legal.

The oracle is a private full field built using the production seeder and Dijkstra from the live current map. It neither publishes a field nor consumes game RNG. Short-lived caching requires matching tracked map generations, identity and slot epoch. A unit's origin can itself be excluded from the field, e.g. forbidden paint or a resource cell; these are scored against the best legal reachable neighbouring exit, counting the step to it. Such origins remain in all totals and are reported separately. All tied best gradient neighbour scores count as optimal; we do not compare against an arbitrary tie-broken direction.

Every completed setup is also run without the diagnostic. Probe/control initial checksum, final checksum and actual tick count match: {S['all_control_matches']}. These new paired checks compare endpoints. The earlier two 2,048-tick pilots also established per-tick world/entity equivalence against the current PR on those inputs. No broader per-tick or cross-platform equivalence claim is made.

Apple M3/macOS arm64, existing Apple clang 21 release builds, `PYTHONPATH=. scons release=1 CXXFLAGS=-g0 client_profile=full server=0 -j4`. Existing dependencies unchanged. Matches run concurrently (initially up to four across dispatchers, then a four-worker dispatcher); dispatch was restarted only after its active child runs completed, reusing saved results. No game seed, game state or completed run was discarded for its measurement result. This is unsuitable for CPU/jitter comparison; elapsed times are orchestration metadata only.

Exact game/generation commands, stdout/stderr, effective result records, generated maps and compressed per-request records are in request-data.tar.gz. The live benchmark source and production branch were not edited for this extension; it reuses frozen binaries. Analysis and plotting use repository-pinned NumPy 2.4.6 / Matplotlib 3.11.2 where needed. No runtime dependency or production code change.

## Setup corrections and limits

Initial generation calls supplied dimensions as tile counts; this legacy study interface expects log2 dimensions, as used by the repository tournament runner. Those pre-game failures were retained and corrected. Symmetric arena rejects sixteen colonies; its case was changed to supported 4v4, and the 8v8 stress moved to shattered-coast. Concrete islands failed six-team start-capacity validation at the chosen seed; its final case uses four teams with the same seed. Bastion keys also rejected its initial colony density, so its final setup uses two teams with the same seed. Braided delta requested more roomy islands at six teams; its rejoining-frequency control was raised from 2 to 3, keeping seed and dimensions. Savannah requires at least 128 tiles per side, so its requested 64×64 layout was raised to 128×128 with the same seed. These are documented input corrections, not seed rerolls. Any remaining generation failures are retained in summary.json; no successful generation is selected from a seed search.

This remains a one-step counterfactual. It does not enable heuristic travel, replay repeated heuristic steps, test oscillation/deadlocks, change service selection, or measure arrival time, starvation, outcomes, CPU, frame pacing or devices. Service-selection rows assess directions to candidate destinations, not which inn/hospital a replacement distance heuristic would choose. The oracle describes the current world, which may change before the missing background field publishes. No manual play or cross-platform performance claim.

Broader requests and sidecars can expose failures missed by the original two maps, but one successful seed per setup is a breadth audit rather than a statistical characterization of each generator. Production pending handling remains unchanged.
'''
(B/'README.md').write_text(report)
