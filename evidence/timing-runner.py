#!/usr/bin/env python3
"""Prepare/execute predeclared native or scheduler-harness timing manifests.
Defaults to validate-only. --execute requires root-owned stable-boundary gate.
No build commands. Manifest provides variants with binary/cwd/sha256 and scenarios
with kind(game/harness), args, fixture hashes, budget, allocation and repeats.
Harness output may contain one COORDINATION_RESULT JSON object on stdout.
"""
import argparse,hashlib,json,os,signal,subprocess,time
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('manifest',type=Path);ap.add_argument('--execute',action='store_true');ap.add_argument('--gate',type=Path);ap.add_argument('--max-cases',type=int);args=ap.parse_args()
manifest=json.loads(args.manifest.read_text());out=args.manifest.resolve().parent/'timing';out.mkdir(exist_ok=True)
def digest(f):
 with Path(f).open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
def save(p,d):
 temp=p.with_suffix('.tmp');temp.write_text(json.dumps(d,indent=2)+'\n');temp.replace(p)
assert manifest['base_revision']=='48438f1f08a68b24ef0049834d371b9d00d84bae'
for v in manifest['variants']:assert digest(v['binary'])==v['sha256'],v['role']
for s in manifest['scenarios']:
 if s['kind']=='game':
  assert '--compute-threads' in s['args'] and s['args'][s['args'].index('--compute-threads')+1]==str(s['budget'])
  assert '--gradient-backend' in s['args'] and s['args'][s['args'].index('--gradient-backend')+1]=='cpu'
 assert s['kind'] in ['game','harness'] and s['budget'] in [1,2,4,8,'auto']
 for f,h in s.get('fixture_sha256',{}).items():assert digest(f)==h,f
 assert '--benchmark-warmup' not in s['args'] and '--telemetry' not in s['args'],'Timing requires ordinary mode and optional hot-path telemetry off'
cases=[]
for s in manifest['scenarios']:
 for repeat in range(s.get('repeats',2)):
  variants=manifest['variants'];shift=repeat%len(variants);order=variants[shift:]+variants[:shift]
  if repeat%2:order=list(reversed(order))
  for v in order:cases.append({'id':s['id']+'-'+v['role']+'-r'+str(repeat),'scenario':s,'variant':v,'repeat':repeat})
save(out/'prepared-plan.json',{'cases':cases,'execute_requested':args.execute,'base_revision':manifest['base_revision']})
if not args.execute:print('Validated',len(cases),'cases; no workloads launched');raise SystemExit()
assert args.gate and json.loads(args.gate.read_text()).get('state')=='stable_boundary_ready_for_coordination_timing','Root must finish builds and pause old timing at a stable boundary'
for k in list(os.environ):
 if k.startswith('GLOB2_'):os.environ.pop(k,None)
env=os.environ.copy();env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
if args.max_cases:cases=cases[:args.max_cases]
for case in cases:
 d=out/case['id'];d.mkdir(exist_ok=True)
 if (d/'receipt.json').exists():raise RuntimeError('Do not silently reuse timing receipt: '+str(d))
 s=case['scenario'];v=case['variant'];profile=d/'profile';envcase=dict(env,GLOB2_USER_DIR=str(profile),GLOB2_USER_DATA_DIR=str(profile));envcase.update(v.get('env',{}));envcase.update(s.get('env',{}))
 command=['taskset','-c',','.join(map(str,s['allocation'])),v['binary']]
 command+=['game','run'] if s['kind']=='game' else[];command+=[str(x).replace('{dispatch_policy}',v.get('dispatch_policy','round-robin')).replace('{participants}',str(s.get('resolved_participants',s['budget']))) for x in s['args']];command+=['--output-dir',str(d/'native')] if s['kind']=='game' else[]
 start=time.monotonic();timedout=False;terminated=None
 with (d/'stdout.log').open('w') as log:
  child=subprocess.Popen(command,cwd=v['cwd'],env=envcase,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
  while True:
   pid,status,usage=os.wait4(child.pid,os.WNOHANG)
   if pid:break
   now=time.monotonic()
   if now-start>s.get('timeout_seconds',180) and not timedout:
    os.killpg(child.pid,signal.SIGTERM);timedout=True;terminated=now
   if timedout and now-terminated>5:
    try:os.killpg(child.pid,signal.SIGKILL)
    except ProcessLookupError:pass
   time.sleep(.02)
 wall=time.monotonic()-start;rc=os.waitstatus_to_exitcode(status);child.returncode=rc
 validation_errors=[];native=None;harness=[]
 try:native=json.loads((d/'native/result.json').read_text()) if (d/'native/result.json').exists() else None
 except (OSError,json.JSONDecodeError) as ex:validation_errors.append('Invalid native JSON: '+str(ex))
 text=(d/'stdout.log').read_text(errors='replace')
 for line in text.splitlines():
  if line.startswith('COORDINATION_RESULT '):
   try:harness.append(json.loads(line.split(' ',1)[1]))
   except json.JSONDecodeError as ex:validation_errors.append('Invalid micro JSON: '+str(ex))
 if s['kind']=='game':
  if not native:validation_errors.append('Missing native result')
  else:
   target=int(s['args'][s['args'].index('--ticks')+1]);initial=s.get('initial_tick',0)
   if native.get('ticks')!=target or target-initial<=0:validation_errors.append('Native resumed tick count differs from requested cap minus initial tick')
   if not isinstance(native.get('run_ns'),int) or native['run_ns']<=0:validation_errors.append('Missing/invalid inner run wall')
 else:
  if len(harness)!=1:validation_errors.append('Missing/excess micro result')
  else:
   h=harness[0];rounds,jobs,iterations=map(int,s['args'][3:6]);pattern=s['args'][2];expected=rounds*jobs*(8 if pattern=='lanes' else 1)
   if h.get('jobs_checked')!=expected or h.get('rounds')!=rounds or h.get('jobs_per_round')!=jobs*(8 if pattern=='lanes' else 1):validation_errors.append('Micro job count differs from planned payload')
   if h.get('iterations')!=iterations or h.get('pattern')!=pattern:validation_errors.append('Micro payload shape differs')
   if h.get('participants')!=s.get('resolved_participants',s['budget']):validation_errors.append('Micro actual participants differ')
   if not isinstance(h.get('wall_ns'),int) or h['wall_ns']<=0:validation_errors.append('Invalid micro inner wall')
 receipt={'validation_errors':validation_errors,'case':case,'command':command,'binary_sha256':digest(v['binary']),'status':'timeout' if timedout else 'completed' if rc==0 else 'failed','returncode':rc,'process_wall_seconds':wall,'user_cpu_seconds':usage.ru_utime,'system_cpu_seconds':usage.ru_stime,'total_cpu_seconds':usage.ru_utime+usage.ru_stime,'voluntary_context_switches':usage.ru_nvcsw,'involuntary_context_switches':usage.ru_nivcsw,'peak_rss_kib':usage.ru_maxrss,'native':native,'harness':harness,'variant_env':v.get('env',{}),'scenario_env':s.get('env',{}),'fixture_sha256':s.get('fixture_sha256',{}),'design_claims':v.get('design_claims',{}),'mutex_accounting_scope':'Record queue pop/callback-start global dispatch mutex absence separately from remaining shared completion/dependency mutex; do not infer this from throughput.'}
 if native and isinstance(native.get('ticks'),int) and isinstance(native.get('run_ns'),int):
  initial=s.get('initial_tick',0);receipt['resumed_ticks']=native['ticks']-initial;receipt['inner_run_wall_seconds']=native['run_ns']/1e9;receipt['pure_run_cpu_available']=False
 save(d/'receipt.json',receipt)
 if validation_errors:raise RuntimeError('Retained validation error before follow-on: '+case['id']+' '+str(validation_errors))
 if rc or timedout:raise RuntimeError('Retained failed/timed-out case; investigate before continuing: '+case['id'])
print('Completed',len(cases),'cases')
