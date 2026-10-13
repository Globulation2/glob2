#!/usr/bin/env python3
"""Read-only derive compact historical/scaling evidence; never launch builds/games."""
import csv, hashlib, json, collections
from pathlib import Path
HERE = Path(__file__).resolve().parent
HISTORY = Path('/home/bradley/.codex/worktrees/a630/glob2/artifacts/engine-history')
OAZIS = HISTORY.with_name('engine-history-oazis')
OPT = Path('/home/bradley/.codex/worktrees/engine-optimization/glob2/artifacts/engine-optimization')
SCALING = OPT / 'thread-scaling-v2'
PERF = OPT / 'goal-audit/mature-perf-release-relink'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
write=lambda p,d:p.write_text(json.dumps(d,indent=2)+'\n')
files=[]
def add(p,target,reason):
 if not p.is_file():raise FileNotFoundError(p)
 files.append(dict(source=str(p),archive_path=target,bytes=p.stat().st_size,sha256=sha(p),purpose=reason))
observations={}
for name,root in [('historical-playground',HISTORY),('historical-oazis',OAZIS)]:
 config=json.loads((root/'experiment.json').read_text())
 rows=list(csv.DictReader((root/'runs.csv').open()))
 assert len(set((r['commit'],r['seed']) for r in rows))==len(rows)
 statuses=dict(collections.Counter(r['status'] for r in rows))
 observations[name]=dict(attempts=len(rows),status_counts=statuses,revisions_by_sample_date=dict(collections.Counter(r['sample_date'] for r in rows)),planned_attempts=len(config['snapshots'])*len(config['seeds']),remaining_attempts=len(config['snapshots'])*len(config['seeds'])-len(rows),map_sha256=config['map_sha256'],map_sha256_verified=sha(root/('Playground.map' if root==HISTORY else 'Oazis.map'))==config['map_sha256'],seeds=config['seeds'],revision_list=config['snapshots'],execution=config['execution'],accounting=config['accounting'],observer=config['observer'])
 for n in ['experiment.json','runs.csv','summary.csv','summary.json','build-times.csv','build-times.json','plot_build_times.py','plot_results.py','run_experiment.py','report.html']:
  add(root/n,f'{name}/{n}','Aggregate inputs, snapshot/seed identity, accounting definition, plotting/execution protocol, or scoped report')
 add(root/('Playground.map' if root==HISTORY else 'Oazis.map'),f'{name}/map/'+('Playground.map' if root==HISTORY else 'Oazis.map'),'Fixed workload input; not a raw simulation trace')
 for p in sorted(root.glob('snapshots/*/build.json')):
  add(p,f'{name}/'+str(p.relative_to(root)),'Per-revision compiler flags, dependency/binary hashes, build elapsed provenance')
 for p in sorted(root.glob('snapshots/*/adapter.patch')):
  add(p,f'{name}/'+str(p.relative_to(root)),'Exact historical benchmark compatibility adapter; original code remains in Git')
assert observations['historical-playground']['attempts']==448 and observations['historical-playground']['status_counts']=={'completed':448}
assert observations['historical-oazis']['attempts']==192 and observations['historical-oazis']['status_counts']=={'completed':190,'timeout':2}
assert (OAZIS/'compile-dispatch-hold.flag').exists()
observations['historical-oazis']['current_hold_flag_sha256']=sha(OAZIS/'compile-dispatch-hold.flag')
observations['historical-oazis']['qualification']='Campaign held and incomplete: six revisions observed, eight unobserved. Existing progress/report state can say running, but hold prevents dispatch; no completion of remaining 256 claimed. Timeout CPU/wall remain consumption measurements, not completed-game E2E; missing ticks/TPS remain unknown. Same legacy runner has 11 AIs plus idle local observer; distinct from exact eleven-controller later fixtures.'
observations['historical-playground']['qualification']='14x32 natural-winner games with one-hour cap, all winners observed. Historical rules/random streams/AI/replay defaults differ; trajectory changes prevent causal attribution from E2E alone. Earlier binaries can be serial despite six-CPU mask; CPU allocation is not proof of six compute participants. Frozen historical tip is not current master.'
for n in ['history-summary.html','history-detailed.html','cpu-affinity-evidence.json','serial-baseline-experiment.json','serial-baseline-runs.csv','serial-baseline-summary.json','run_build_first.py','prebuild.py','plot-requirements.lock']:
 add(HISTORY/n,f'historical-playground/{n}','Historical narrative/control/protocol; control cohort is excluded from primary 448')
for n in ['map-hash-by-revision.json','map-inspection.json','inherited-compute-environment.json','compile-dispatch-hold.flag']:
 add(OAZIS/n,f'historical-oazis/{n}','Historical map/controller/runtime lineage or explicit unfinished campaign hold')
for n in ['commit-inventory.json','accounting.json','interval-statistics.json','analyze_metrics.py','write_narrative.py','early.txt','middle.txt','late.txt','middle-links.json','mature-cpu-namespace-correction.json','mature-cpu-namespace-correction.html']:
 add(HISTORY/'analysis'/n,f'historical-playground/analysis/{n}','Chronological feature/change/evidence inventory, statistical or qualified attribution notes')
for p in sorted((HISTORY/'analysis').glob('pr*.json')):
 add(p,'historical-playground/analysis/'+p.name,'Captured public PR evidence referenced by the historical narrative')
d=json.loads((SCALING/'analysis/analysis.json').read_text());plan=json.loads((SCALING/'plan.json').read_text())
assert d['complete'] and d['planned_native']==d['verified_native']==480 and len(d['groups'])==60
metrics=list(next(iter(d['groups'].values()))['metrics'])
scalar_rows=[]
def export(context,seed,row,warmup):
 ge=row['health']['gradient_execution']
 o=row['outcome'];rp=row['replay']
 x=dict(budget=context['budget'],mask=','.join(map(str,context['mask'])),layout=context['layout'],gpu=context['gpu'],window=context['window'],seed=seed,repeat=row['repeat'],warmup=int(warmup),requested_count=row['requested_count'],actual_count=row['actual_count'],**row['metrics'],ticks=o['ticks'],termination=o['termination'],initialChecksum=o['initialChecksum'],finalChecksum=o['finalChecksum'],gpu_requested_fields=row['health']['requested_fields'],gpu_complete_fields=row['health']['gpu_completed_fields'],device_committed_fields=row['health']['device_committed_fields'],gpu_device=row['health']['gpu_device'],gpu_error=row['health']['gpu_error'],host_waits=row['health']['host_waits'],replay_sha256=rp['sha256'],ait1_sha256=rp['ait1']['sha256'],ait1_until=rp['ait1']['until'],ait1_truncated=int(rp['ait1']['truncated']),minimum_rss_floor_bytes=row['floor_bytes'],source_receipts_sha256=';'.join(v['sha256'] for v in row['sources']))
 scalar_rows.append(x)
for group in d['groups'].values():
 for seed,s in group['seeds'].items():
  for row in s['raw_rows']:export(group['context'],seed,row,False)
for job in d['warmups']:
 parts=job['job'].split('-')
 matches=[(g['context'],seed) for g in d['groups'].values() for seed in g['seeds'] if job['job']==f"{g['context']['budget']}-{g['context']['layout']}-{seed}-gpu{g['context']['gpu']}"]
 assert matches
 for row in job['rows']:
  context,seed=next((c,s) for c,s in matches if c['requested_count']==row['requested_count'])
  export(context,seed,row,True)
assert len(scalar_rows)==480 and sum(r['warmup'] for r in scalar_rows)==120
with (HERE/'scaling-scalars.csv').open('w',newline='') as f:
 w=csv.DictWriter(f,fieldnames=list(scalar_rows[0]));w.writeheader();w.writerows(scalar_rows)
compact=dict(source_analysis_sha256=sha(SCALING/'analysis/analysis.json'),source_plan_sha256=sha(SCALING/'plan.json'),generated_from='Read-only export of all 360 measured and 120 warmup native scalar records; raw traces omitted',complete=True,accepted_for_production_optimum=False,source_commit=plan['source_commit'],binary_sha256=plan['binary_sha256'],verified_native=480,measured=360,warmups=120,cells=60,scope=d['scope'],metrics=metrics,fixtures=plan['design']['fixtures'],design=plan['design'],groups={k:{kk:vv for kk,vv in g.items() if kk not in ['seeds','health']} for k,g in d['groups'].items()},provenance=d['provenance'],qualification='Tick-limited initial-state windows; no full-game completion/time optimum, no whole-host throughput claim. 36 preceding diagnostic children have per-tick GCS/world equivalence; measured windows have endpoint CRC/replay+AIT1 proof, not full every-tick trace. GPU counters are observed end counters, not GPU time.' )
write(HERE/'scaling-summary.json',compact)
observations['thread-scaling']=dict(verified_native=480,measured=360,warmups=120,paired_cells=60,diagnostic_children=36,source_commit=plan['source_commit'],scope=d['scope'],compact_export_source_sha256=sha(SCALING/'analysis/analysis.json'))
for n in ['README.txt','design.json','dispatch.json','performance-complete.json','conformance-complete.json','analyze.py','analysis/completed-independent-review.json','analysis/root-descriptive-acceptance.json','analysis/scaling-narrative-addendum.json','analysis/report.html']:
 add(SCALING/n,'thread-scaling/'+n,'Original method/completion/review/qualified report; standalone scalar reanalysis does not need raw traces')
add(OPT/'analyze_performance.py','thread-scaling/analyze_performance.py','Original deterministic paired-seed bootstrap implementation used for aggregate calculations')
add(HERE/'scaling-scalars.csv','thread-scaling/scaling-scalars.csv','All 480 native scalar observations, warmups labelled; compact raw timing input, no trace payload')
add(HERE/'scaling-summary.json','thread-scaling/scaling-summary.json','60-cell aggregate and source/fixture/method provenance; all source hashes retained')
for n in ['followup.html','leading-namespace-supplement-v1.json','independent-sample-accounting.json','qualification.json','revised-independent-review.json','independent-review.json','analysis-v4/analysis.json','analysis-v4/metadata.json','analysis-v4/header.txt','analysis-v4/buildids.txt','analysis-v4/elf-notes.txt','profile/sampling-boundaries.json','profile/attached-tasks.json','profile/command.json','profile/sampler-start.json','profile/sampler-stop.json','profile/complete.json','profile/validated.json','continuous-sealed/command.json','continuous-sealed/validated.json','continuous-symbolic/command.json','continuous-symbolic/validated.json','bounded-sealed/command.json','bounded-sealed/validated.json','bounded-symbolic/command.json','bounded-symbolic/validated.json','building-search-empty-layer-review/assessment.json','scalar-relax-store-candidate.json']:
 add(PERF/n,'mature-perf/'+n,'Qualified mature sample/namespace attribution and separate native continuity proofs; no raw perf, trace, replay, save or binary')
sample=json.loads((PERF/'independent-sample-accounting.json').read_text());leading=json.loads((PERF/'leading-namespace-supplement-v1.json').read_text())
observations['mature-perf']=dict(exported_reconciled_samples=sample['samples'],observed_period=sample['total_observed_period'],empty_callchains=sample['empty_callchains'],empty_callchain_fraction=sample['empty_callchain_fraction'],source_commit='9e370bcb83209cf99f43c346750a6d4bd440f4f2',qualification='One continuously evolved late Oazis window near120064-128000; CPU task-clock sample ranking, not whole-game CPU fractions or prediction of speed savings. 56.8662% empty callchains: full caller/all-active-thread coverage unproven. Original substring category table in analysis-v4/analysis.json is explicitly superseded by leading-namespace-supplement-v1.json: snapshot handle type appearing in a gradient template does not make gradient work snapshot work. Exact binary text/outcome/replay proof is separate from loaded512tick checksum continuity; continuous every-tick late-window equivalence not claimed.',leading_namespace_observation=leading['shares'])
write(HERE/'observed-counts.json',observations)
add(HERE/'observed-counts.json','history-and-scaling-observed-counts.json','Fresh read-only observed row counts, state/coverage caveats, no pending/completed substitution')
add(Path(__file__),'curation/curate-history.py','Exact read-only export recipe; original absolute source paths documented, no native launches')
write(HERE.parent/'history-curation.json',dict(schema_version=1,scope='Historical Playground/Oazis, final9e scaling, qualified mature CPU profile only',files=files,total_files=len(files),total_bytes=sum(f['bytes'] for f in files),observations=observations,excluded=['Full source copies and executables','Detailed simulation/world checksums and full replays/saves','perf.data, structured samples and raw record/callchain exports','Full build stdout logs and generated plot duplicates','Unexecuted Oazis revisions, unexecuted profile policy calibration'],limits='Compact evidence preserves per-run scalar inputs, tested workload identities, provenance and scoped conclusions; not a bit-for-bit complete reproduction of all raw diagnostics. Original scripts with absolute paths are historical records; use bundled standalone reanalysis for compact CSVs.'))
print(json.dumps(dict(files=len(files),bytes=sum(f['bytes'] for f in files),output=str(HERE.parent/'history-curation.json'),counts={k:{kk:vv for kk,vv in v.items() if kk in ['attempts','status_counts','remaining_attempts','verified_native','measured','warmups','paired_cells','exported_reconciled_samples','empty_callchain_fraction']} for k,v in observations.items()}),indent=2))
