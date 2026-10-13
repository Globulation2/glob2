#!/usr/bin/env python3
"""Plan-only telemetry replication runner. Explicit --run and sealed gates required.
Fresh one-fixture/factor controllers,3/6 repeats, immutable original campaign.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import threading

ART=Path(__file__).resolve().parent
ROOT=ART.parents[1]
OUT=ART/'telemetry-replication'

def load(p):return json.loads(Path(p).read_text())
def sha(p):
 h=hashlib.sha256()
 with Path(p).open('rb') as f:
  for part in iter(lambda:f.read(1024*1024),b''):h.update(part)
 return h.hexdigest()
def write(p,v):
 p.parent.mkdir(parents=True,exist_ok=True);temp=p.with_suffix(p.suffix+'.tmp')
 temp.write_text(json.dumps(v,indent=2)+'\n');temp.replace(p)
def require(condition,message):
 if not condition:raise ValueError(message)
def environment():
 env={k:v for k,v in os.environ.items() if not k.startswith('GLOB2_')}
 env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
 return env

def idle_gate(gates):
 require(gates.get('build_phase_complete') is True,'root build phase gate pending')
 require(gates.get('compilers_idle') is True,'root idle-compiler checkpoint pending')
 # Refuse an actual compiler/linker even when an old gate claims idle.
 active=[]
 for path in Path('/proc').glob('[0-9]*/comm'):
  try:name=path.read_text().strip()
  except OSError:continue
  if name in {'cc1','cc1plus','clang','clang++','ld','ld.lld','lto1','collect2'}:active.append(name)
 require(not active,'active compilers/linkers detected: '+str(active))

def job_gate(job,gates):
 idle_gate(gates)
 require(gates.get('historical_slots_held') is True,'historical slot hold gate pending')
 require(gates.get('baseline_sha256')==job['baseline']['binary_sha256'],'baseline gate hash mismatch')
 record=gates.get('factors',{}).get(job['factor'],{})
 require(record.get('correctness_passed') is True,'factor correctness gate pending: '+job['factor'])
 require(record.get('binary_sha256')==job['candidate'].get('binary_sha256'),'factor correctness binary mismatch')
 require(job['candidate']['state']=='complete','factor build not complete')
 require(sha(job['candidate']['binary'])==job['candidate']['binary_sha256'],'candidate changed')
 require(sha(job['baseline']['binary'])==job['baseline']['binary_sha256'],'baseline changed')
 manifest=load(job['manifest'])
 for scenario in manifest['scenarios']:
  for path,expected in scenario['fixture_sha256'].items():
   require(expected is not None and sha(path)==expected,'fixture not sealed/changed')
   require(record.get('fixture_sha256',{}).get(path)==expected,'fixture correctness coverage missing')
 evidence=record.get('evidence',[])
 require(bool(evidence),'correctness evidence not attached')
 for source in evidence:require(sha(source['path'])==source['sha256'],'correctness evidence changed')


def holdout(plan,gates):
 idle_gate(gates)
 require(gates.get('baseline_sha256')==plan['baseline']['binary_sha256'],'baseline gate mismatch')
 require(sha(plan['baseline']['binary'])==plan['baseline']['binary_sha256'],'baseline changed')
 spec=importlib.util.spec_from_file_location('correctness_helpers',ART/'correctness_campaign.py')
 c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
 for case in plan['fixture_preparation']:
  cmd=case['command'];directory=Path(cmd[cmd.index('--output-dir')+1]);map_path=Path(cmd[cmd.index('--map-file')+1])
  require(sha(map_path)==case['frozen_map_sha256'],'holdout frozen map changed')
  require(not directory.exists(),'holdout directory exists: preserve/diagnose; no overwrite')
  directory.mkdir(parents=True);env=environment();env.update(GLOB2_USER_DIR=str(directory/'profile'),GLOB2_USER_DATA_DIR=str(directory/'profile'))
  write(directory/'invocation.json',dict(command=cmd,binary_sha256=sha(plan['baseline']['binary']),wrapper_sha256=sha(Path(__file__)),scope='one-tick diagnostic fixture preparation; no timing conclusions',environment_overrides={k:v for k,v in env.items() if k.startswith('GLOB2_') or k.startswith('SDL_')}))
  with (directory/'stdout.log').open('wb') as stdout,(directory/'stderr.log').open('wb') as stderr:
   rc=subprocess.run(cmd,cwd=Path(plan['baseline']['root']),env=env,stdout=stdout,stderr=stderr,timeout=3600).returncode
  require(rc==0,'holdout preparation failed; logs preserved')
  result=load(directory/'result.json');require(result['status']=='completed' and result['ticks']==1 and result['termination']=='tick_cap','invalid holdout initial run')
  scenario=dict(width=case['expected_width'],height=case['expected_height'],ais=case['expected_ais'])
  c.validate_result(result,scenario,case['seed']);header=c.save_header(directory/'initial.game.gz')
  require((header['width'],header['height'],header['teams'])==(case['expected_width'],case['expected_height'],case['expected_teams']),'holdout geometry changed')
  trace=c.trace(directory/'game.replay.checksums');require(trace['first_tick']==0 and trace['last_tick']==0 and trace['count']==1,'holdout initialtick not0')
  c.world_trace(directory/'world.checksums',0,1)
  write(directory/'fixture.json',dict(path=str(directory/'initial.game.gz'),header=header,start_tick=0,preparation_trace=trace,scenario=scenario,seed=case['seed'],wrapper_sha256=sha(Path(__file__))))
  print('Prepared sealed holdout',case['id'],flush=True)
 print('Rerun telemetry_replication_plan.py with exact metadata arguments to read the sealed holdout; timing correctness gates remain pending.')


def worker(jobpath):
 job=load(jobpath);os.sched_setaffinity(0,set(job['slot']))
 for key in list(os.environ):
  if key.startswith('GLOB2_'):del os.environ[key]
 os.environ.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
 gates=load(job['gates_path']);job_gate(job,gates)
 sys.path.insert(0,str(ROOT/'test'));import benchmark_resource_refactor as bench
 original=bench.execute
 def isolated(binary,args,output,*,cwd=ROOT):
  previous={k:os.environ.get(k) for k in ('GLOB2_USER_DIR','GLOB2_USER_DATA_DIR')}
  os.environ.update({k:str(output/'profile') for k in previous})
  try:
   row=original(binary,args,output,cwd=cwd)
   write(output/'isolation.json',dict(profile=str(output/'profile'),wrapper_sha256=sha(Path(__file__)),affinity=sorted(os.sched_getaffinity(0))))
   write(output/'native-measurement.json',row)
   return row
  finally:
   for key,value in previous.items():
    if value is None:os.environ.pop(key,None)
    else:os.environ[key]=value
 bench.execute=isolated
 bench.RUNNER_INPUTS=tuple(bench.RUNNER_INPUTS)+(Path(__file__).resolve(),Path(jobpath).resolve(),Path(job['gates_path']).resolve(),OUT/'plan.json')
 identity=job['identity'];require(sha(Path(__file__))==identity['wrapper_sha256'],'wrapper changed')
 require(sha(OUT/'plan.json')==identity['plan_sha256'] and sha(job['gates_path'])==identity['gates_sha256'],'plan/gates changed')
 require(sha(ROOT/'test/benchmark_resource_refactor.py')==identity['benchmark_sha256'] and sha(ROOT/'test/benchmark_parallel_compute.py')==identity['execute_sha256'],'benchmark source changed')
 require(sha(job['manifest'])==job['manifest_sha256'],'manifest changed')
 binaries={'baseline':Path(job['baseline']['binary']),'candidate':Path(job['candidate']['binary'])}
 roots={'baseline':Path(job['baseline']['root']),'candidate':Path(job['candidate']['root'])}
 if job.get('resume_check'):
  audit=load(Path(job['output'])/'input-verification.json');manifest=load(job['manifest'])
  fixtures={Path(p) for s in manifest['scenarios'] for p in s['fixture_sha256']}
  # Previous captured job path contains resume_check=false; compare the immutable
  # original job digest separately, and omit only this control-file hash here.
  current=bench.capture_inputs(binaries,roots,Path(job['manifest']),fixtures)
  before=audit['after'];before['runner_inputs'].pop(str(Path(jobpath).resolve()),None)
  current['runner_inputs'].pop(str(Path(jobpath).resolve()),None)
  require(not bench.changed_inputs(before,current),'resume inputs changed')
  write(Path(job['output'])/'resume-verification.json',dict(inputs_unchanged=True,identity=identity));return
 sys.argv=[str(ROOT/'test/benchmark_resource_refactor.py'),str(binaries['baseline']),str(binaries['candidate']),job['manifest'],'--output',job['output'],'--before-root',str(roots['baseline']),'--after-root',str(roots['candidate']),'--repeats',str(job['repeats']),'--report-only']
 require(bench.main()==0,'paired benchmark execution/integrity failed')
 audit=load(Path(job['output'])/'input-verification.json');require(audit['state']=='verified' and audit['inputs_unchanged'],'input audit failed')
 require(sha(Path(__file__))==identity['wrapper_sha256'],'wrapper changed during measurements')
 write(Path(job['output'])/'campaign-passed.json',dict(passed=True,identity=identity))


def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--run',action='store_true');p.add_argument('--prepare-holdout',action='store_true');p.add_argument('--resume',action='store_true');p.add_argument('--factor',action='append',choices=['telemetry-v1','telemetry-v2','combined-later']);p.add_argument('--gates',type=Path);p.add_argument('--worker',type=Path,help=argparse.SUPPRESS);args=p.parse_args()
 if args.worker:worker(args.worker);return
 plan=load(OUT/'plan.json')
 if not args.run and not args.prepare_holdout:
  print(json.dumps(dict(plan_only=True,jobs=len(plan['jobs']),ready_inputs=sum(j['ready'] for j in plan['jobs']),required_gates='build phase complete; compilers actually idle; baseline hash; per-factor exactbinary/fixture correctness evidence; historicalslots held')));return
 require(args.gates is not None,'--gates FILE required; no implied build/correctness acceptance')
 gates=load(args.gates);idle_gate(gates)
 if args.prepare_holdout:holdout(plan,gates);return
 selected=[dict(j) for j in plan['jobs'] if not args.factor or j['factor'] in args.factor]
 require(bool(selected),'no selected jobs')
 for job in selected:
  job_gate(job,gates);job['gates_path']=str(args.gates.resolve())
  job['identity']=dict(wrapper_sha256=sha(Path(__file__)),plan_sha256=sha(OUT/'plan.json'),manifest_sha256=sha(job['manifest']),baseline_sha256=job['baseline']['binary_sha256'],candidate_sha256=job['candidate']['binary_sha256'],repeats=job['repeats'],slot=job['slot'],gates_sha256=sha(args.gates),benchmark_sha256=sha(ROOT/'test/benchmark_resource_refactor.py'),execute_sha256=sha(ROOT/'test/benchmark_parallel_compute.py'))
  passed=Path(job['output'])/'campaign-passed.json'
  if args.resume and passed.exists():require(load(passed)['identity']==job['identity'],'resume source/input/args identity changed');job['resume_check']=True
  else:require(not Path(job['output']).exists(),'partial output exists; preserve and diagnose')
 write(OUT/'plans'/f"{sha(OUT/'plan.json')}.json",plan)
 write(OUT/'selected-worklist.json',selected);failed=threading.Event();errors=[]
 def lane(slot):
  for job in selected:
   if job['slot']!=slot or failed.is_set():continue
   path=OUT/'jobs'/f"{job['id']}.json";write(path,job)
   with path.with_suffix('.log').open('w') as log:
    rc=subprocess.run([sys.executable,str(Path(__file__).resolve()),'--worker',str(path)],cwd=ROOT,env=environment(),stdout=log,stderr=subprocess.STDOUT).returncode
   if rc:errors.append(dict(job=job['id'],returncode=rc));failed.set()
 # Tuples converted to lists so equality matches JSON plan slots.
 threads=[threading.Thread(target=lane,args=(list(slot),)) for slot in {tuple(j['slot']) for j in selected}]
 for t in threads:t.start()
 for t in threads:t.join()
 write(OUT/'status.json',dict(selected_factors=args.factor,selected_jobs=len(selected),complete=not errors,errors=errors,accepted=False))
 require(not errors,'replication failure; native outcomes/logs retained; root decides early-winner window')
if __name__=='__main__':main()
