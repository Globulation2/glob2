#!/usr/bin/env python3
"""Finite68-job V2 timing follow-up. Default plan-only; explicit --run after gates.
Fresh adapters reuse immutable telemetry_replication.worker and versioned benchmark.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import threading

ART=Path(__file__).resolve().parent
ROOT=ART.parents[1]
OUT=ART/'v2-followup'
SCRIPT=Path(__file__).resolve()

def module(name,path):
 spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
R=module('followup_replication',ART/'telemetry_replication.py')
load,sha,write,require=R.load,R.sha,R.write,R.require
MASKS=[[0,1,2,16,17,18],[3,4,5,19,20,21],[6,7,8,22,23,24],[9,10,11,25,26,27]]

def prepare():
 acceptance=load(ART/'acceptance-plan.json');campaign=load(ART/'campaign.json');variants=load(ART/'variant-builds.json');mature=load(ART/'mature/plan.json')
 baseline=dict(binary=campaign['baseline_binary'],root=campaign['baseline_source'],binary_sha256=campaign['baseline_binary_sha256'])
 candidates={name:load(ART/'variants'/name/'manifest.json') for name in ('snapshot-v2','telemetry-v2')}
 v1={name:dict(binary=variants['variants'][name]['binary'],root=variants['variants'][name]['root'],binary_sha256=variants['variants'][name]['binary_sha256']) for name in ('snapshot','telemetry')}
 for item in [baseline,*candidates.values(),*v1.values()]:require(sha(item['binary'])==item['binary_sha256'],'built binary changed')
 jobs=[];group_index={};fixtures={}
 def add(group,scenario,seed,path,digest,start,window,gpu,factor,before,before_label,cohort):
  if group not in group_index:group_index[group]=len(group_index)
  identifier=f'{group}-{factor}-vs-{before_label}-gpu{gpu}-w{window}'
  manifest=dict(scenarios=[dict(id=identifier,start_tick=start,fixture_sha256={str(path):digest},args=['--load-game',str(path),'--ticks',str(start+window),'--compute-threads','3','--gpu',gpu,'--write-replay'])])
  mp=OUT/'manifests'/f'{identifier}.json';write(mp,manifest)
  jobs.append(dict(id=identifier,scenario=scenario['id'],seed=seed,factor=factor,repeats=3,discarded_warmup_pairs=1,manifest=str(mp),manifest_sha256=sha(mp),output=str(OUT/'runs'/identifier),baseline=before,candidate=candidates[factor],slot=MASKS[group_index[group]%4],context=dict(cohort=cohort,fixture_group=group,comparator=before_label,candidate=factor,checkpoint_tick=start,window=window,gpu=gpu,scope='loadedcheckpoint cache/allocator reconstruction' if start else 'frozeninitial window; canonical replay enabled')))
  fixtures[str(path)]=digest
 for scenario in acceptance['scenario_design']:
  for seed in acceptance['new_game_seeds']:
   key=f"{scenario['id']}-{seed}";f=load(ART/'correctness'/key/'fixture.json');path=Path(f['path']);require(sha(path)==f['header']['gzip_sha256'] and f['start_tick']==0,'initial fixture changed')
   add('initial-'+key,scenario,seed,path,f['header']['gzip_sha256'],0,scenario['ticks'],'off','snapshot-v2',baseline,'baseline','initial')
 for case in mature['checkpoints']:
  f=load(ART/'mature/checkpoints'/case['id']/'checkpoint.json');path=Path(f['path']);digest=f['header']['gzip_sha256'];require(sha(path)==digest and f['start_tick']==case['target_tick'],'checkpoint changed')
  for factor in candidates:
   for gpu in ('off','on'):
    for window in (4096,8192):add('mature-'+case['id'],case['scenario'],case['seed'],path,digest,f['start_tick'],window,gpu,factor,baseline,'baseline','mature-baseline')
    name=factor.removesuffix('-v2');add('mature-'+case['id'],case['scenario'],case['seed'],path,digest,f['start_tick'],8192,gpu,factor,v1[name],name+'-v1','mature-direct')
 require(len(jobs)==68,'finite job count must68')
 source_paths=[SCRIPT,ART/'telemetry_replication.py',ROOT/'test/benchmark_resource_refactor.py',ROOT/'test/benchmark_parallel_compute.py',ART/'acceptance-plan.json',ART/'campaign.json',ART/'variant-builds.json',ART/'mature/plan.json',ART/'v2-conformance/plan.json',ART/'v2-mature/plan.json']+[ART/'variants'/n/'manifest.json' for n in candidates]+[ART/'tests'/f'{n}-focused.json' for n in candidates]
 seals={str(p):sha(p) for p in source_paths}
 plan=dict(plan_only=True,jobs=jobs,total_jobs=68,total_native_children=544,cohorts={'initial':20,'mature-baseline':32,'mature-direct':16},source_seals=seals,fixtures=fixtures,baseline=baseline,candidates=candidates,v1=v1,accepted=False,gate_receipts=dict(initial=str(ART/'v2-conformance/complete.json'),mature_v1=str(ART/'mature/parallel-correctness/complete.json'),mature_v2=str(ART/'v2-mature/complete.json')),qualifications=['Freshloaded checkpoints do not preserve prior allocator/cache lifetime.','Matchedbaseline/V1/V2 pairs use same6SMTmask for onefixturegroup; all3reps+warmuppair fixedmask.','TelemetryV2early24 and V1mature96 are separate rootphases; no original sealed runner modified.','3measured repeats alternate2/1order; discardwarmuppair; no automatic threshold acceptance.'])
 write(OUT/'plan.json',plan)
 template=dict(build_phase_complete=False,compilers_idle=False,historical_slots_held=False,baseline_sha256=baseline['binary_sha256'],comparators={item['binary_sha256']:False for item in [baseline,*v1.values()]},factors={})
 for factor,item in candidates.items():template['factors'][factor]=dict(binary_sha256=item['binary_sha256'],correctness_passed=False,fixture_sha256=fixtures,evidence=[])
 if not (OUT/'gates-template.json').exists():write(OUT/'gates-template.json',template)
 return plan


def assert_sources(plan):
 for path,digest in plan['source_seals'].items():require(sha(path)==digest,'followup source/build/focused receipt changed: '+path)
 for path,digest in plan['fixtures'].items():require(sha(path)==digest,'sealed input changed')


def proof_gate(plan):
 initial=load(plan['gate_receipts']['initial']);require(initial['passed'] and initial['selected']==initial['planned_total']==21,'initial21PASS gate pending')
 old=load(plan['gate_receipts']['mature_v1']);require(old['passed'] and old['completed_workers']==old['planned_workers']==4,'V1mature4PASS gate pending')
 initial_plan=load(ART/'v2-conformance/plan.json');mature_plan=load(ART/'v2-mature/plan.json')
 new=load(plan['gate_receipts']['mature_v2']);require(new['passed'] and new['completed_workers']==new['planned_workers']==4 and new['planned_children']==48,'V2mature48/4PASS gate pending')
 require(new['plan_sha256']==sha(ART/'v2-mature/plan.json'),'V2mature completion seal changed')
 for factor,candidate in plan['candidates'].items():
  require(initial_plan['seals'][candidate['binary']]==candidate['binary_sha256'] and mature_plan['variants'][factor]['binary_sha256']==candidate['binary_sha256'],'diagnostic proofs used a different candidate binary')
  receipt=load(ART/'tests'/f'{factor}-focused.json')
  require(receipt['passed'] and receipt['failures']==receipt['errors']==0 and receipt['application_binary_sha256']==candidate['binary_sha256'],'focused build/test receipt mismatch')
  require(receipt['source_manifest_sha256']==sha(ART/'variants'/factor/'manifest.json'),'focused source receipt mismatch')
  require(sha(receipt['junit'])==receipt['junit_sha256'],'focused JUnit changed')
  for path,digest in candidate.get('source_sha256',{}).items():
   source=Path(path) if Path(path).is_absolute() else Path(candidate['root'])/path
   require(sha(source)==digest,'candidate frozen source changed')


def generic_gate(job,gates):
 plan=load(OUT/'plan.json');assert_sources(plan);proof_gate(plan)
 require(gates.get('baseline_sha256')==plan['baseline']['binary_sha256'],'untouched baseline gate mismatch')
 require(gates.get('comparators',{}).get(job['baseline']['binary_sha256']) is True,'comparator build/correctness gate missing')
 # R gate checks an exact comparator hash for this job; prove it against the
 # independent comparator allowlist before adapting its baseline-specific field.
 adapted=dict(gates,baseline_sha256=job['baseline']['binary_sha256'])
 ORIGINAL_GATE(job,adapted)
ORIGINAL_GATE=R.job_gate


def worker(path):
 job=load(path);require(sha(SCRIPT)==job['identity']['adapter_sha256'],'followup adapter changed')
 os.sched_setaffinity(0,set(job['slot']));R.OUT=OUT;R.job_gate=generic_gate
 sys.path.insert(0,str(ROOT/'test'));import benchmark_resource_refactor as bench
 bench.RUNNER_INPUTS=tuple(bench.RUNNER_INPUTS)+(SCRIPT,)
 R.worker(path)
 require(sha(SCRIPT)==job['identity']['adapter_sha256'],'followup adapter changed during job')


def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--run',action='store_true');p.add_argument('--gates',type=Path);p.add_argument('--cohort',action='append',choices=['initial','mature-baseline','mature-direct']);p.add_argument('--factor',action='append',choices=['snapshot-v2','telemetry-v2']);p.add_argument('--worker',type=Path,help=argparse.SUPPRESS);args=p.parse_args()
 if args.worker:worker(args.worker);return
 if not args.run:
  plan=prepare();print(json.dumps(dict(plan_only=True,jobs=68,native_children=544,cohorts=plan['cohorts'],launches=0)));return
 require(args.gates is not None,'explicit root checkpoint gates required')
 plan=load(OUT/'plan.json');assert_sources(plan);proof_gate(plan);gates=load(args.gates)
 selected=[dict(j) for j in plan['jobs'] if (not args.cohort or j['context']['cohort'] in args.cohort) and (not args.factor or j['factor'] in args.factor)]
 require(selected,'no selected jobs')
 for job in selected:
  generic_gate(job,gates);require(not Path(job['output']).exists(),'fresh output required; preserve/diagnose old evidence')
  job['gates_path']=str(args.gates.resolve());job['identity']=dict(wrapper_sha256=sha(ART/'telemetry_replication.py'),adapter_sha256=sha(SCRIPT),plan_sha256=sha(OUT/'plan.json'),manifest_sha256=sha(job['manifest']),baseline_sha256=job['baseline']['binary_sha256'],candidate_sha256=job['candidate']['binary_sha256'],repeats=3,slot=job['slot'],gates_sha256=sha(args.gates),benchmark_sha256=sha(ROOT/'test/benchmark_resource_refactor.py'),execute_sha256=sha(ROOT/'test/benchmark_parallel_compute.py'))
 write(OUT/'selected-worklist.json',selected);failed=threading.Event();errors=[]
 def lane(mask):
  for job in selected:
   if job['slot']!=mask or failed.is_set():continue
   path=OUT/'jobs'/f"{job['id']}.json";write(path,job)
   with path.with_suffix('.log').open('w') as log:
    rc=subprocess.run([sys.executable,str(SCRIPT),'--worker',str(path)],cwd=ROOT,env=R.environment(),stdout=log,stderr=subprocess.STDOUT).returncode
   if rc:errors.append(dict(job=job['id'],returncode=rc));failed.set()
 threads=[threading.Thread(target=lane,args=(mask,)) for mask in MASKS]
 for t in threads:t.start()
 for t in threads:t.join()
 write(OUT/'status.json',dict(complete=not errors,selected_jobs=len(selected),selected_cohorts=args.cohort,selected_factors=args.factor,errors=errors,accepted=False))
 require(not errors,'followup execution/input/window failure; preserve native outcomes, no auto-shortening')
if __name__=='__main__':main()
