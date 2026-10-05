#!/usr/bin/env python3
"""Refresh the evidence-only report from completed measurements; tolerate pending games."""
from pathlib import Path
import argparse, hashlib, json, random, statistics, xml.etree.ElementTree as ET

ap=argparse.ArgumentParser()
ap.add_argument('--root',type=Path,default=Path(__file__).resolve().parent.parent)
ap.add_argument('--output',type=Path,default=None)
a=ap.parse_args(); root=a.root.resolve(); out=a.output or root/'published';out.mkdir(parents=True,exist_ok=True)
def read(name,default=None):
    try:return json.loads((root/name).read_text())
    except (FileNotFoundError,json.JSONDecodeError):return default

def tests(name):
    r=ET.parse(root/name).getroot();v={k:int(r.attrib.get(k,0)) for k in ('tests','failures','errors','skipped')};v['passed']=v['tests']-v['failures']-v['errors']-v['skipped'];return v

def pct(r):return f'{(r-1)*100:+.1f}%'
def row(*values):return '| '+' | '.join(str(v) for v in values)+' |'

provenance=read('final-provenance.json',{})
bench=read('bench-confirmation/summary.json',[])
lazy=read('integration/paired-process/portable-offset-summary.json',[])
mem=read('memory-accounting.json',{})
continuation=read('review-continuation-final/summary.json',{})
arithmetic=read('review-arithmetic/summary.json',{})
games=read('final-games/samples.json',[])
resumption=read('final-games/resumption.json',{})
scenarios=('classic128','classic512','network128','mixed256','mixed512','ice128')
metric=[]
for mode in ('terrain','dispatch','strategic'):
    for layout in ('shared','separate'):
        cohort=[r for r in bench if r['case']['mode']==mode and r['layout']==layout and r['case'].get('registry')==7 and r['case'].get('size',0)>=128]
        if cohort:metric.append(dict(mode=mode,layout=layout,cases=len(cohort),ratio=sum(r['candidate_cpu_ms'] for r in cohort)/sum(r['baseline_cpu_ms'] for r in cohort),median=statistics.median(r['ratio'] for r in cohort)))
synthetic=[]
for n in (8,32,64):
    c=[r for r in bench if r['case']['mode']=='terrain' and r['case'].get('registry')==n and r['case'].get('costs')=='equivalent' and r['case'].get('size')==128]
    if c:synthetic.append(dict(identities=n,cases=len(c),ratio=sum(r['candidate_cpu_ms'] for r in c)/sum(r['baseline_cpu_ms'] for r in c)))
lazy_stats=[dict(width=w,groups=len(v),median=statistics.median(v),worst=max(v),best=min(v)) for w in (32,128,512) if (v:=[r['cpu_ratio'] for r in lazy if r['width']==w])]
game_stats=[];rng=random.Random(1427)
for scenario in scenarios:
    b={r['repeat']:r for r in games if r['scenario']==scenario and r['variant']=='baseline'}
    c={r['repeat']:r for r in games if r['scenario']==scenario and r['variant']=='candidate'}
    keys=sorted(b.keys()&c.keys())
    result=dict(scenario=scenario,pairs=len(keys),complete=len(keys)>=11,no_growth=scenario.endswith('512'))
    if keys:
        ratios=[c[k]['cpu_ns_per_tick']/b[k]['cpu_ns_per_tick'] for k in keys]
        boot=sorted(statistics.median(rng.choices(ratios,k=len(ratios))) for _ in range(10000))
        before=statistics.median(b[k]['cpu_ns_per_tick'] for k in keys);after=statistics.median(c[k]['cpu_ns_per_tick'] for k in keys)
        compared=('initialChecksum','finalChecksum','teams','ticks','gradient_jobs','gradient_published','hiring_popped_entries')
        result.update(baseline_ns_per_tick=before,candidate_ns_per_tick=after,ratio=after/before,paired_median=statistics.median(ratios),paired_bootstrap_95pct=[boot[249],boot[9749]],end_state_equal=all(b[k][field]==c[k][field] for k in keys for field in compared),peak_rss_kib={v:statistics.median(d[k]['peak_rss_kib'] for k in keys) for v,d in [('baseline',b),('candidate',c)]})
    if keys and not result['end_state_equal']:
        raise RuntimeError(f'End-state mismatch in timing scenario {scenario}')
    game_stats.append(result)
complete=all(r['complete'] for r in game_stats)
data=dict(revision=provenance.get('revision'),baseline=provenance.get('baseline'),whole_game_complete=complete,whole_games=game_stats,resumption=resumption,kernel_cohorts=metric,synthetic_equivalent=synthetic,lazy=lazy_stats,unit=tests('verified-unit-tests.xml'),engine=tests('verified-engine-tests.xml'))
(out/'report-data.json').write_text(json.dumps(data,indent=2)+'\n')
text=['# Varied-terrain gradient optimization: review evidence','',
'## Revision and conclusion','',
f"Candidate **`{data['revision']}`**, baseline **`{data['baseline']}`**. PR #791 is stacked on the ecology optimization PR #787; this comparison starts from that ecology-optimized baseline.",'',
'General engine-gradient CPU time improved approximately **6%** in the larger real-terrain microbenchmark cohort. Strategic AI travel improved approximately **66%**, measured separately. The **20% general-gradient research target was not met**. The accepted work preserves scalar/SSE2/NEON execution and exact integer results, while avoiding the cache and queue experiments that did not show sufficient benefit.','',
'Accepted changes: immutable movement-cost classes shared across calls; compact per-layer cost/cursor preparation and tighter queue reservation; bounded integer bucket traversal for strategic AI travel; a portable resumable-search seed-scan refinement that keeps the lazy object size unchanged. No new per-cell terrain-cost cache is retained.','',
f"Whole-game confirmation status: **{'complete' if complete else 'PENDING'}** ({sum(r['pairs'] for r in game_stats)}/66 completed baseline/candidate pairs). This report is generated from completed pairs only; rerun `published/generate-report.py` after remaining runs finish.",'',
'All evidence paths below are relative to `artifacts/gradient-optimization/` in the restored archive. These notes are review artifacts, not product documentation.','',
'## Kernel measurements','',
'`bench-confirmation/manifest.json` freezes compiler flags, source/adapter/binary hashes and copied headers. `samples.jsonl` contains each alternating timed sample; `summary.json` contains case medians. Timed builds disable allocation/work counters. Eleven alternating pairs use both shared buffers/workspaces and independent allocations; cold runs remain separately identified. Positive changes in the tables mean slower.','',
row('Cohort, maps ≥128²','Allocation layout','Cases','Aggregate CPU change','Median case change'),row('---','---','---:','---:','---:')]
labels={'terrain':'General engine kernel','dispatch':'Classic specialized dispatch','strategic':'Strategic AI travel'}
for r in metric:text.append(row(labels[r['mode']],r['layout'],r['cases'],pct(r['ratio']),pct(r['median'])))
text+=['','The aggregate is the ratio of sums of per-case median CPU times; the median-case column weights cases equally. This is neither a whole-game speedup nor a claim that every individual case improves. The general cohort includes classic input deliberately forced through the general kernel; the specialized-dispatch cohort checks actual classic routing separately.','',
'Coverage includes 32² through 512²; all seven swimming profiles; classic, uniform road/ice, sparse/connected roads, dense mixtures and enclosed modifiers; dense/deferred seeds and propagation limits; thin/rectangular wrapped grids; and 8/32/64 synthetic identities. Synthetic equivalent-cost cases preserve the same underlying cost map while increasing identities.','',
row('Synthetic equivalent identities, 128²','Cases across both layouts','Aggregate CPU change'),row('---:','---:','---:')]
for r in synthetic:text.append(row(r['identities'],r['cases'],pct(r['ratio'])))
text+=['','This demonstrates reduced dependence on identity count when actual movement costs are identical. Distinct-cost cases are separate rows in the raw data; increased distinct costs still require additional work. Profile preparation, seed copying, optional plane construction and snapshot copying are reported separately. Tiny single-construction preparation timings are illustrative, not robust microsecond-level speed claims.','',
'## Resumable searches','',
'`integration/paired-process/portable-offset-summary.json` records the accepted portable lazy variant. Four buffer offsets (0, 1, 8 and 31 elements), five layouts, seven swimming profiles, cold/shared snapshot settings and repeated searches expose alignment sensitivity. The independent comparisons completed **37,800 assertions**. Production benchmark coverage also includes nearby, distant and unreachable requests.','',
row('Map width','Grouped comparisons','Median CPU change','Best / worst grouped change'),row('---:','---:','---:','---:')]
for r in lazy_stats:text.append(row(r['width'],r['groups'],pct(r['median']),f"{pct(r['best'])} / {pct(r['worst'])}"))
text+=['','Each group is itself a median of paired runs. These summaries concern the near-query experiment and must not be presented as a universal lazy-search speedup. Raw logs, source, build/link commands and hashes are in `integration/paired-process/`; `final-manifest.json` inventories the final variants. Source review confirms a paused search retains its terrain snapshot. The accepted change does not add class planes or a new invalidation mechanism.','',
'## Whole-game confirmation','',
'Frozen binaries run eleven alternating pairs per scenario, 4,096 total ticks each, excluding the first 512 ticks from timing; Maxima/Nicowar, game seed 12345, identical maps/orders, serial execution pinned to CPU 0. Growth is disabled **only for the 512² timing scenarios**. The smaller scenarios retain ordinary growth. Source/binary/map hashes, exact commands, affinity, load averages and `/usr/bin/time` output accompany every run in `final-games/<scenario>-<repeat>-<variant>/`.','',
*( [f"Run interruption: the timing process received SIGTERM after {resumption.get('preserved_samples',0)//2} complete pairs. The resume retained {resumption.get('preserved_samples',0)} completed samples, discarded {resumption.get('discarded_incomplete_samples',0)} unmatched sample(s), and reran the interrupted pair in full before continuing. The tested revision and frozen binaries were unchanged; `final-games/resumption.json` records this recovery. Completed pairs were not rerun or selected by timing.",''] if resumption else [] ),
row('Scenario','Pairs','Growth','Baseline ms/tick','Candidate ms/tick','Change','Paired median; bootstrap 95% interval'),row('---','---:','---','---:','---:','---:','---')]
for r in game_stats:
    if not r['pairs']:text.append(row(r['scenario'],'0/11','off' if r['no_growth'] else 'on','pending','pending','pending','pending'));continue
    lo,hi=r['paired_bootstrap_95pct'];text.append(row(r['scenario'],f"{r['pairs']}/11",'off' if r['no_growth'] else 'on',f"{r['baseline_ns_per_tick']/1e6:.3f}",f"{r['candidate_ns_per_tick']/1e6:.3f}",pct(r['ratio']),f"{pct(r['paired_median'])}; [{pct(lo)}, {pct(hi)}]"))
text+=['','The confidence interval resamples paired ratios with fixed seed 1427 (10,000 bootstrap samples); it describes these observations and cannot remove systematic host noise. The median-of-times change and median paired ratio are intentionally both shown. End-state checksums, team state and gradient scheduling counters match for every completed timing pair. These timing runs do not collect per-tick checksums; the separate continuation suite below does.','']
if not complete:text+=['**Final performance acceptance remains pending until all six scenarios have eleven pairs.** Do not interpret incomplete rows as passing the classic ≤3% regression gate.','']
else:
    bad=[r['scenario'] for r in game_stats if r['scenario'].startswith('classic') and r['ratio']>1.03]
    text += [('Classic whole-game median changes exceed +3% in: '+', '.join(bad)+'. Investigate before acceptance.') if bad else 'Neither classic whole-game scenario has a median regression above 3%; raw uncertainty remains visible in the paired intervals.','']
text+=['## Memory and lifetime','',
'`memory-accounting.json` distinguishes game storage from narrower benchmark fixtures. Persistent prepared profiles total **1,064 bytes** (152 bytes per swimming profile), shared by all searches. Each executor workspace remains **2,072 bytes**; the lazy-search object remains **2,120 bytes**, identical to baseline. Game terrain snapshots remain **2N bytes** per live terrain generation, shared across profiles/searches; output fields remain **2N bytes** per worker field or active lazy field. No per-cell class plane is retained.','',
'For simultaneous work, let `N = width × height`, `G` be captured live terrain generations, `W` executor workspaces each holding one active eager field, and `L` active lazy fields/searches. A useful active-set accounting model in bytes is:', '',
'```text',
'M = 1,064 + 2N·G + W·(2N + 2,072) + L·(2N + 2,120) + Σ private queue capacities',
'```', '',
'Only the 1,064-byte immutable global profile term is newly retained. The snapshots are shared across all seven profiles and any number of readers; they do not multiply by profile or search count. Queue capacities remain independently owned. Add `2N` for each additional queued or published field retained outside this active-set model, and count an aliased field allocation only once. This formula is not total process memory and excludes unrelated map/AI state and temporary stack storage.', '',
row('Concurrent component','Scaling','512² example'),row('---','---','---:'),
row('All prepared profiles','One process-wide 1,064-byte table','1,064 B total'),
row('Terrain snapshots','2N per live generation; shared across profiles/searches','524,288 B per generation'),
row('Active eager field + executor object','2N + 2,072 + its variable queues','526,360 B + queues per worker'),
row('Active lazy field + search object','2N + 2,120 + its variable queues','526,408 B + queues per search'),
row('Per-profile cost planes','Rejected; no retained allocation','0 B'), '',
'The rejected plane approach could have added `7N` bytes per live generation for one-byte class planes across seven profiles (1.75 MiB at 512²), or `28N` for four-byte cardinal/diagonal planes (7 MiB). Multiple live generations would multiply those costs. Those potential worst-case profile-plane additions are avoided entirely by retaining no planes.', '',
row('512² dense cold example','Baseline','Candidate','Change'),row('---','---:','---:','---:')]
samples=mem.get('instrumented_cold_samples',[])
for mode,field,label in [('terrain','retained_queue_bytes','Engine retained queue capacity'),('terrain','peak_extra_bytes','Engine additional peak live heap'),('strategic','peak_extra_bytes','Strategic additional peak live heap')]:
    matching={r['candidate']:r[field] for r in samples if r['width']==512 and r['mode']==mode and r['layout']=='separate'}
    if len(matching)==2:text.append(row(label,f"{matching[False]:,} B",f"{matching[True]:,} B",f"{matching[True]-matching[False]:+,} B"))
text+=['','Strategic traversal uses a temporary 32-bucket object (1,024 bytes) and 28-byte cost table, plus temporary wide distances and allocated queue capacity; all are released on return. Its 512² example adds about **212 KiB** peak heap. Queue capacity is private to each worker/lazy search and varies with topology, seeds and profile; the table is a measured example, not a worst-case bound. Snapshot generations remain live while captured by outstanding jobs/searches. Compiler stack frames/spills and process RSS are separate from allocator counters; whole-game RSS is retained in `report-data.json`. Never use instrumented timing for performance claims.','',
'## Correctness and compatibility','',
f"- Final unit suite: **{data['unit']['passed']} passed**, {data['unit']['skipped']} skipped, {data['unit']['failures']+data['unit']['errors']} failures/errors (`verified-unit-tests.xml` and log).",
f"- Final engine suite: **{data['engine']['passed']} passed**, {data['engine']['skipped']} skipped, {data['engine']['failures']+data['engine']['errors']} failures/errors (`verified-engine-tests.xml` and log).",
f"- Final continuation comparison: **{continuation.get('paired_tick_records',0):,} paired tick records**, four full games and four continuations, checkpoints at tick 2,048 and completion at 8,192. Serial (1 compute / 0 gradient workers) and parallel (4 / 3) runs match per tick; final save bytes and replay bytes match, and save boundaries match uninterrupted execution. See `review-continuation-final/summary.json`, manifests, commands, saves, replays and checksums.",
'- The 701-tick match-verification golden matches exactly for both binaries. No simulation revision/save-format change is required for this output-preserving optimization. Per-tick checksums do not separately include RNG state; identical final save bytes include serialized RNG state, and modified algorithms make no random draws.',
f"- Independent arithmetic review: {arithmetic.get('common_cases',0)} common scalar/SSE2/NEON cases have identical hashes; ASan/UBSan, ARM64 QEMU NEON and randomized independent-oracle testing passed. Native/scalar/NEON fuzz logs include 950-case runs; expanded future-cost fixtures test a larger ring. See `review-arithmetic/summary.json`, `fuzz-manifest.json`, architecture logs and later lazy reviews.",
'- Oracle fixtures cover cost aliases/extrema, wrapped/thin geometry, deferred seeds, propagation caps, workspace reuse, queued snapshot lifetime, paused terrain edits and concurrent searches. The eager microbenchmarks independently check every field against heap Dijkstra.','',
'## Rejected experiments and limits','',
'Per-cell class/direct cost planes, empty-bucket bitmaps, additional palette dispatch and alternative lazy object/alignment changes were investigated. Their gains were inconsistent, limited to selected cases, or did not justify added construction/lifetime complexity. Rejected prototypes and raw measurements remain under `integration/`, `kernel/`, `core-ablation/` and earlier benchmark folders; they are not part of product history. The accepted strategic bucket change is a separate profiled AI improvement, not a way to count AI gains toward the 20% engine-kernel target.','',
'Master integration: fetched `e1634ecda` has a clean `git merge-tree` result with this branch (`master-merge-check.txt`); intervening seed-related changes were reviewed. The merged result against that newer master was **not built or run as a full engine**. Executable validation applies to the exact tested branch revision above.','',
'Full-engine verification and performance measurements are **Linux x86-64 only**. ARM64 QEMU establishes tested arithmetic agreement, not real ARM/NEON speed. Full-engine Windows/macOS/Android/browser determinism and performance were not measured here. No claim is made about those platforms. Hardware performance counters were unavailable; no host security configuration was changed.','',
'The host is shared. Final game pairs began after this task’s builds/tests ended, but unrelated jobs may still run; recorded load/affinity and paired samples expose some variability. Earlier contended/pilot results are exploratory and excluded from the final headline tables. No whole-game speedup should be inferred from the large strategic-field microbenchmark reduction. Human gameplay review remains useful for the terrain feature as a whole; this optimization is supported by unchanged tested simulation/save traces.','',
'## Reproduction and provenance index','',
'- `final-provenance.json`: final source revision/tree, compiler/platform, binaries, source SHA-256 values, linked-library hashes and build logs. `baseline-provenance.json` freezes the ecology-optimized baseline. `final-history.json` records the unchanged final tree after removing rejected experiments from product history.',
'- `bench-confirmation/manifest.json`, copied `source/`, adapters and binary hash: exact standalone compiler invocation and all hot-header hashes. `tools/gradient_benchmark.py` is the maintained opt-in runner in the PR. `bench-memory/` contains separately instrumented runs.',
'- `run-games.py`, `fixture-build-commands.json`, `fixture-spec.txt`, `make-fixtures.cpp`, maps and per-run `inputs.json`: map seeds, transformations, flags, input/library provenance and timing commands.',
'- `integration/paired-process/final-manifest.json`: lazy benchmark source/build/hash inventory and all offset comparisons. `review-continuation-final/manifest.json` and `environment.json`: exact full-game/save/replay verification commands and libraries.',
'- `review-arithmetic/*manifest.json`: sanitizer/cross-compiler/QEMU commands and hashes. `final-test-commands.md` records the native build/test commands; suite XML/log files enumerate retained tests and skips.',
'- `published/generate-report.py` regenerates this report and `report-data.json` using only recorded results; it performs no builds or measurements. Rerun it immediately before packaging to include all completed game pairs.','',
f"Compiler: `{provenance.get('compiler','').splitlines()[0]}`. Platform: `{provenance.get('platform','unknown')}`. Production flags: `{provenance.get('flags','unknown')}`. Standalone benchmark flags and target architecture are recorded separately in its manifest.",'']
(out/'REPORT.md').write_text('\n'.join(text))
print(f"Report refreshed: {sum(r['pairs'] for r in game_stats)}/66 pairs, complete={complete}")
