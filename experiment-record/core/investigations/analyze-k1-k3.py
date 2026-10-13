#!/usr/bin/env python3
"""Standalone K1/K3 analyzer. Read inputs only; write focused local reports."""
from pathlib import Path
import json,statistics,random,datetime,os,argparse
ROOT=Path(__file__).resolve().parent

def ci(ratios):
 if len(ratios)<2:return None
 r=random.Random(310312);medians=sorted(statistics.median(r.choices(ratios,k=len(ratios))) for _ in range(5000));return [medians[124],medians[4874]]
def summarize(values):return dict(n=len(values),median=statistics.median(values),minimum=min(values),maximum=max(values),bootstrap_median_ci95=ci(values)) if values else dict(n=0)
def analyze(name):
 h=ROOT/name;b=json.loads((h/'build.json').read_text());plan=json.loads((h/'timing-plan.json').read_text());records={};errors=[]
 for c in plan:
  p=h/'timing-runs'/(c['id']+'.json')
  if not p.exists():continue
  r=json.loads(p.read_text());m=r.get('measured',{})
  checks=[r.get('binary_sha256')==c['binary_sha256'],r.get('fixture')==c['fixture'],r.get('variant')==c['variant'],r.get('repetition')==c['repetition'],r.get('command')==c['command'],m.get('fixture')==c['fixture'],m.get('input')==c['input_hash'],m.get('output')==c['output_hash'],int(m.get('iterations',0))==c['iterations'],int(m.get('wall_ns',0))>0,int(m.get('cpu_ns',0))>0,c['cpu_affinity']==[12],not r.get('instrumented')]
  if not all(checks):errors.append(dict(id=c['id'],reason='metadata/output/timer validation failed',checks=checks));continue
  records[c['id']]=r
 groups={}
 for c in plan:groups.setdefault(c['fixture'],{}).setdefault(c['repetition'],{})[c['variant']]=c
 baseline=plan[0]['reference_variant'];variants=sorted({c['variant'] for c in plan}-{baseline});fixtures=[];allpairs=[]
 for fixture,blocks in groups.items():
  comparisons=[]
  for variant in variants:
   pairs=[]
   for rep,block in sorted(blocks.items()):
    base=block[baseline];alt=block[variant]
    if base['id'] not in records or alt['id'] not in records:continue
    x=records[base['id']]['measured'];y=records[alt['id']]['measured'];pair=dict(fixture=fixture,repetition=rep,variant=variant,baseline_id=base['id'],candidate_id=alt['id'],baseline_position=base['order_position'],candidate_position=alt['order_position'],baseline_wall_ns_per_kernel=int(x['wall_ns'])/base['iterations'],candidate_wall_ns_per_kernel=int(y['wall_ns'])/alt['iterations'],baseline_cpu_ns_per_kernel=int(x['cpu_ns'])/base['iterations'],candidate_cpu_ns_per_kernel=int(y['cpu_ns'])/alt['iterations'],wall_ratio=(int(y['wall_ns'])/alt['iterations'])/(int(x['wall_ns'])/base['iterations']),cpu_ratio=(int(y['cpu_ns'])/alt['iterations'])/(int(x['cpu_ns'])/base['iterations']))
    pairs.append(pair);allpairs.append(pair)
   comparisons.append(dict(variant=variant,complete_pairs=len(pairs),expected_pairs=len(blocks),wall_ratio=summarize([p['wall_ratio'] for p in pairs]),cpu_ratio=summarize([p['cpu_ratio'] for p in pairs]),baseline_wall_ns_per_kernel=summarize([p['baseline_wall_ns_per_kernel'] for p in pairs]),candidate_wall_ns_per_kernel=summarize([p['candidate_wall_ns_per_kernel'] for p in pairs]),baseline_cpu_ns_per_kernel=summarize([p['baseline_cpu_ns_per_kernel'] for p in pairs]),candidate_cpu_ns_per_kernel=summarize([p['candidate_cpu_ns_per_kernel'] for p in pairs]),pairs=pairs))
  sample=next(iter(blocks.values()))[baseline];fixtures.append(dict(fixture=fixture,input_hash=sample['input_hash'],output_hash=sample['output_hash'],cells=sample['cells'],comparisons=comparisons))
 work=json.loads((h/'allocation-work.json').read_text());allocation=[]
 workgroups={}
 for c in work['cases']:workgroups.setdefault((c['fixture'],int(c['warm'])),{})[c['variant']]=c
 suffix='-metrics' if name.startswith('k1') else '-counters'
 for (fixture,warm),rows in sorted(workgroups.items()):
  base=rows[baseline+suffix]
  for variant in variants:
   alt=rows[variant+suffix];ratios={};deltas={}
   for key in base.keys()&alt.keys()-{'fixture','variant','visitation','warm'}:
    try:x=int(base[key]);y=int(alt[key])
    except ValueError:continue
    ratios[key]=y/x if x else None;deltas[key]=y-x
   allocation.append(dict(fixture=fixture,state='warm' if warm else 'cold',variant=variant,baseline=base,candidate=alt,ratios=ratios,deltas=deltas))
 oracle_pass=b.get('oracle_inventories_identical',b.get('all_cell_and_visit_order_inventories_identical',False));record=dict(generated_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),state='complete' if len(records)==len(plan) and not errors else 'pending',planned_cases=len(plan),valid_cases=len(records),invalid_cases=errors,baseline=baseline,fixture_count=len(fixtures),variant_comparison_pair_count=len(allpairs),fixtures=fixtures,allocation_comparisons=allocation,oracle_evidence=dict(passed=oracle_pass,source_build_manifest=str(h/'build.json'),summaries=[r['oracle_summary'] for r in b['variants']],actual_visit_order_verified=name.startswith('k3'),note='K3 actual visitation digests verified in separate counter binaries, not added to timed release path. All-cell outputs validated before each timed invocation and after every kernel iteration.'),statistics='Candidate/baseline ratios paired by exact fixture and repetition block. Median of within-block ratios; fixed-seed5000-resample paired bootstrap95 interval. Exploratory timing uncertainty across repeats, not a population-of-games interval. No pooled aggregate across different fixture sizes/weights/source/frontier workloads.',timing_scope='Warm kernel wall and std::clock processCPU only; initialization/inputcopy/oracle/validation/output excluded. Cold/warm memory work separate and not timed. No mature native game, generic partial/resume/exception or platform equivalence conclusion.')
 (h/'timing-summary.json').write_text(json.dumps(record,indent=2)+'\n')
 lines=[f'{name}: {record["state"]}; {len(records)}/{len(plan)} timing records; {len(allpairs)} complete fixture/block variant pairs.',f'Baseline={baseline}; every workload stays separate. Ratios below1 favor candidate; above1 are regressions.','All-cell oracle preparation: '+str(oracle_pass),record['statistics'],record['timing_scope'],'','Fixture | candidate | pairs | wall ratio median [95 interval] | CPU ratio median [95 interval]']
 fmt=lambda d:'pending' if not d['n'] else f'{d["median"]:.4f} '+(f'[{d["bootstrap_median_ci95"][0]:.4f},{d["bootstrap_median_ci95"][1]:.4f}]' if d['bootstrap_median_ci95'] else '[insufficient repeats]')
 for f in fixtures:
  for c in f['comparisons']:lines.append(f'{f["fixture"]} | {c["variant"]} | {c["complete_pairs"]}/{c["expected_pairs"]} | {fmt(c["wall_ratio"])} | {fmt(c["cpu_ratio"])}')
 lines+=['','Separate allocation comparisons: '+str(len(allocation))+'; full per-fixture cold/warm rows and zero-baseline-qualified ratios in timing-summary.json.','Allocation requested bytes are not live memory peaks; K3 pending queue excludes fixed stack and seed peak. No blind pooled memory or speed summary.']
 (h/'timing-report.txt').write_text('\n'.join(lines)+'\n');print(lines[0]);return record
if __name__=='__main__':
 os.sched_setaffinity(0,{15,31});ap=argparse.ArgumentParser();ap.add_argument('folders',nargs='*');a=ap.parse_args()
 for n in a.folders or ['k1-current-chunks','k3-current-bfs']:analyze(n)
