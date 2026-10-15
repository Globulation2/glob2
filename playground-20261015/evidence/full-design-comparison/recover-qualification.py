#!/usr/bin/env python3
"""Research-only recovery: preserve known baseline failures; reuse focus/baseline,260paritytasks."""
from pathlib import Path
import ast,collections,concurrent.futures,datetime,fcntl,fnmatch,hashlib,importlib.util,json,os,re,shutil,signal,sys,threading,time,traceback,xml.etree.ElementTree as ET
O=Path(__file__).resolve().parent;P=O.parent;HELPER_SHA='d8d9760565941e5acb4c119dbf8e6072ef0813d5c3575514907b647691c68e2f'
def sha(p):
 with Path(p).open('rb')as f:return hashlib.file_digest(f,'sha256').hexdigest()
read=lambda p:json.loads(Path(p).read_text())
assert sha(O/'qualify-all.py')==HELPER_SHA
spec=importlib.util.spec_from_file_location('reviewedqualifier',O/'qualify-all.py');q=importlib.util.module_from_spec(spec);spec.loader.exec_module(q)
lock=(O/'qualification.lock').open('a+');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
for proc in Path('/proc').glob('[0-9]*'):
 try:comm=(proc/'comm').read_text().strip();state=(proc/'stat').read_text().split(') ',1)[1].split()[0]
 except OSError:continue
 assert state=='Z'or not comm.startswith(('cc1','clang','g++','gcc','lto1','glob2')),('Activecompiler/nativeatqualificationlaunch',proc.name,comm)
review=read(O/'recovery-qualification-independent-review.json');approval=read(O/'recovery-root-approved.json');assert review['approved']and approval['approved']and review['source_sha256']==approval['source_sha256']==sha(Path(__file__))
for item in approval['bound_files']:assert sha(item['path'])==item['sha256']
known_path=O/'matched-baseline-failures-review.json';known=read(known_path);assert known['state']=='matched_failures_independently_confirmed_not_test_pass';knownref={'path':str(known_path),'sha256':sha(known_path)}
variants=read(O/'built-variants.json')['variants'];assert len(variants)==14 and variants[0]['role']=='baseline';vs={v['role']:v for v in variants};cases=read(P/'correctness-cases.json')['cases'];assert len(cases)==20;cs={c['id']:c for c in cases}
build=read(O/'builds-complete.json');assert build['state']=='complete';outputs={x['path']:x['sha256']for source in build['sources']for x in source['outputs'].values()}
for v in variants:
 assert sha(v['binary'])==v['sha256']
 for kind in ['unit','engine']:
  if v[kind]in outputs:assert sha(v[kind])==outputs[v[kind]]
for source in build['sources']:
 for f,h in source['source']['source_hashes'].items():assert sha(Path(source['source']['cwd'])/f)==h
filters={'unit':['ComputeExecutor/*','ReplayStepCounter/*','HiringRanking/*','TurnHarness/two clients on clean links execute identical turns*','TurnHarness/a restarted client reloads, replays the full log and rejoins*'],'engine':['AIPipeline/*','SharedWorkerLifecycle/*','SimulationReadPhase/*','BuildingGradientInvalidation/*','ResourceGrowth/*','TeamStatsPipeline/*','TurretTargeting/*','HiringPipeline/*','HiringPlanner/*','AIStateContinuation/*','LegacyAIState/*']}
def events(cases):
 result=[]
 for c in cases:
  ee=[x for x in c if x.tag in['failure','error']]
  if ee:
   assert all(x.tag=='failure'for x in ee),'Timeout/errors notallowlisted'
   result.append({'suite':c.get('classname'),'name':c.get('name'),'events':[{'type':e.get('type'),'message':e.get('message'),'body':re.sub(r'(?m)^  logged: name := 0x[0-9a-fA-F]+\s*$','  logged: name := <volatile-address>',e.text or'')}for e in ee]})
 return result
focused={}
for v in variants:
 records=[]
 for kind in ['unit','engine']:
  target=O/'qualification'/v['role']/('focused-'+kind);command=read(target/'command.json');text=(target/'stdout.log').read_text();assert sha(target/'stdout.log')==command['stdout_sha256']and command['cwd']==v['cwd']and command['binary_sha256']==v['sha256'];assert command['env_overrides']=={k:x for k,x in q.env_for(v,target).items()if k.startswith(('GLOB2_','SDL_'))}
  args=command['command'];assert args[:2]==['taskset','-c']and args[2]in[','.join(map(str,x))for x in q.MASKS]and '--binary'in args and args[args.index('--binary')+1]==kind
  assert args[args.index('--build-dir')+1]==str(Path(v[kind]).parents[1]);assert args[args.index('--junit')+1]==str(target/'junit.xml')
  tests=list(ET.parse(target/'junit.xml').getroot().iter('testcase'));assert not any(c.find('skipped')is not None for c in tests);inventory={f:[c.get('classname','')+'/'+c.get('name','')for c in tests if fnmatch.fnmatchcase(c.get('classname','')+'/'+c.get('name',''),f)]for f in filters[kind]};assert all(inventory.values());bad=events(tests)
  if kind=='unit':assert not bad and command['returncode']==0
  else:assert command['returncode']==1 and len(tests)==162 and bad==known['baseline_failures']
  records.append({'kind':kind,'cases':len(tests),'failures':[{'class':x['suite'],'name':x['name']}for x in bad],'known_baseline_failure_events':bad,'passed_cases':len(tests)-len(bad),'skips':[],'tested_binary_sha256':sha(v[kind]),'filter_inventory':inventory,'junit_sha256':sha(target/'junit.xml'),'command_receipt':str(target/'command.json'),'original_returncode':command['returncode'],'reused_focused_evidence':True})
 focused[v['role']]=records
# Revalidate20baselinecontexts,includingimmutablehistoricalcommand/envprovenance.
def revalidate(v,case,r,target,historical=None):
 command=read(r['command_receipt']);historic=historical or target;mask=command['command'][2];assert mask in[','.join(map(str,x))for x in q.MASKS];expected=['taskset','-c',mask,v['binary'],'game','run',*case['args'],'--write-replay','--telemetry','checksums','--output-dir',str(historic/'native')]
 assert command['command']==expected and command['cwd']==v['cwd']and command['returncode']==0 and command['binary_sha256']==r['binary_sha256']==v['sha256']and r['case']==case['id']and r['role']==v['role'];assert command['env_overrides']=={k:x for k,x in q.env_for(v,historic).items()if k.startswith(('GLOB2_','SDL_'))};assert sha(target/'stdout.log')==command['stdout_sha256']and sha(target/'native/result.json')==r['result_sha256']
 for f,h in case['fixture_sha256'].items():assert sha(f)==h
 n=read(target/'native/result.json');assert n['status']=='completed'and n['ticks']==case['initial_tick']+case['measured_ticks']and(n['initialChecksum'],n['finalChecksum'])==(r['initialChecksum'],r['finalChecksum']);checks,details=q.h.sidecars(target/'native',case);assert checks==r['checksums']
 if 'opencl'in case['args']:assert n['gradient_backend']=='opencl'and n['gradient_gpu_fields']>0
 return dict(r,sidecar_details=details,actual_gradient_backend=n['gradient_backend'],actual_gradient_device=n.get('gradient_device'),actual_gradient_gpu_fields=n.get('gradient_gpu_fields'))
baseline={}
for r in read(O/'baseline-parity-reuse.json')['traces']:
 case=cs[r['case']];target=Path(r['command_receipt']).parent;baseline[r['case']]=revalidate(variants[0],case,r,target,O/'qualification/baseline/parity'/r['case'])
assert len(baseline)==20
pending=[(v,c)for v in variants if v['role']!='baseline'for c in cases];done={v['role']:{}for v in variants};done['baseline']=baseline;cv=threading.Condition();gpu_busy=False;failures=[]
def interrupt(*_):
 q.stop.set()
 with cv:cv.notify_all()
signal.signal(signal.SIGTERM,interrupt);signal.signal(signal.SIGINT,interrupt)
def worker(index):
 global gpu_busy
 while True:
  with cv:
   while True:
    if q.stop.is_set():return
    selected=next((i for i,(_,c)in enumerate(pending)if 'opencl'not in c['args']or not gpu_busy),None)
    if selected is not None:break
    if not pending:return
    cv.wait(.2)
   v,case=pending.pop(selected);is_gpu='opencl'in case['args']
   if is_gpu:gpu_busy=True
  try:
   target=O/'qualification'/v['role']/'parity'/case['id'];receipt=target/'receipt.json'
   if receipt.exists():r=revalidate(v,case,read(receipt),target)
   else:
    if target.exists():
     dest=O/'recovery-partial-parity-attempts'/(v['role']+'-'+case['id']+'-'+str(time.time_ns()));dest.parent.mkdir(exist_ok=True);target.rename(dest)
    r=q.parity(v,case,q.MASKS[index%4],baseline[case['id']]);r=revalidate(v,case,r,target)
   b=baseline[case['id']];assert(r['initialChecksum'],r['finalChecksum'],r['checksums'])==(b['initialChecksum'],b['finalChecksum'],b['checksums'])
   with cv:done[v['role']][case['id']]=r;q.save(O/'research-qualification-progress.json',{'state':'parity','completed_new_contexts':sum(len(x)for k,x in done.items()if k!='baseline'),'pending':len(pending),'roles':{k:len(x)for k,x in done.items()},'GPUadmission':1,'CPUworkers':8,'two_processes_per_mask':True})
  except BaseException as e:
   with cv:failures.append({'role':v['role'],'case':case['id'],'error':repr(e),'traceback':traceback.format_exc()});q.stop.set()
  finally:
   with cv:
    if is_gpu:gpu_busy=False
    cv.notify_all()
try:
 with concurrent.futures.ThreadPoolExecutor(max_workers=8)as pool:list(pool.map(worker,range(8)))
 assert not q.stop.is_set()and not failures,failures
 # FreshmissingunitGradient+weightedverboseproof mayrunconcurrently; do notadmitrolesbeforeitpasses.
 gap_path=O/'focused-gap-checks/gap-checks-complete.json';end=time.monotonic()+6*3600
 while not gap_path.exists():assert not q.stop.is_set()and time.monotonic()<end;time.sleep(1)
 gaps=read(gap_path);assert gaps['state']=='passed'and set(gaps['roles'])==set(vs)
 admitted={};resolved=[]
 for v in variants:
  assert not q.stop.is_set(),'Stoppedbeforeadmission'
  assert len(done[v['role']])==20;extra=[];g=gaps['roles'][v['role']]
  assert set(g)==({'gradient','weighted_verbose'}if v['role'].startswith('weighted-')else{'gradient'})
  for name,record in g.items():
   assert record['state']=='passed'and sha(record['path'])==record['hash']and sha(record['command_receipt'])==record['command_receipt_sha256'];command=read(record['command_receipt']);target=Path(record['command_receipt']).parent
   assert command['returncode']==0 and command['cwd']==v['cwd']and command['unit_sha256_before']==command['unit_sha256_after']==record['unit_sha256']==sha(v['unit']);assert command['command'][:2]==['taskset','-c']and command['command'][3]==v['unit'];
   if v['unit']in outputs:assert command['build_expected_unit_sha256']==outputs[v['unit']]
   assert all(command['env'][k]==value for k,value in v.get('env',{}).items());assert command['env']['SDL_VIDEODRIVER']==command['env']['SDL_AUDIODRIVER']=='dummy'and command['env']['GLOB2_ASSET_DIR']==v['cwd']and command['env']['GLOB2_USER_DIR']==command['env']['GLOB2_USER_DATA_DIR']==str(target/'profile');assert sha(target/'stdout.log')==command['stdout_sha256']
   common={'cases':record['cases'],'command_receipt':record['command_receipt'],'tested_binary_sha256':record['unit_sha256'],'fresh_supplemental_metadata':{'path':record['path'],'sha256':record['hash']}}
   if name=='gradient':
    assert sha(record['junit'])==record['junit_sha256'];tests=list(ET.parse(record['junit']).getroot().iter('testcase'));assert len(tests)==record['cases']==len(set(record['names']))==22 and [c.get('name')for c in tests]==record['names']and all(c.get('classname')=='src/map/gradient/GradientPipelineHarness.cpp'and all(c.find(t)is None for t in ['failure','error','skipped'])for c in tests);extra.append(dict(common,kind='unit_gradient',failures=[],skips=[],junit_sha256=record['junit_sha256'],names=record['names'],junit_classname='src/map/gradient/GradientPipelineHarness.cpp'))
   else:
    text=Path(record['log']).read_text();assert sha(record['log'])==record['stdout_sha256']and record['cases']==2 and record['assertions']==73 and record['no_warning_skips']and 'WARN'not in text;assert re.search(r'test cases:\s*2\s*\|\s*2 passed',text)and re.search(r'assertions:\s*73\s*\|\s*73 passed',text);extra.append(dict(common,kind='weighted_verbose',assertions=73,no_warning=True,actual_participants=[3,4]))
  # Adapt freshsupplementalrecordsintoexplicitfocused record kinds below.
  records=focused[v['role']]+extra
  r={'state':'qualified_with_known_baseline_failures','research_qualification_only':True,'role':v['role'],'variant':v,'focused_tests':records,'exact_per_tick_contexts':20,'traces':[done[v['role']][c['id']]for c in cases],'base_revision':v['base_revision'],'baseline_failure_review':knownref,'known_baseline_failures':known['baseline_failures'],'fresh_supplemental_manifest':{'path':str(gap_path),'sha256':sha(gap_path)},'limits':['Known2baselineenginefailuresretainedNOTpassed.','Researchheadlessqualificationonly; noplatform/browser/livepresentation/performanceacceptance.']};target=O/'qualification'/v['role']/'complete.json'
  assert not q.stop.is_set(),'Stoppedbeforerolepublication'
  if target.exists():assert read(target)==r
  else:q.save(target,r)
  ref={'state':r['state'],'path':str(target),'sha256':sha(target)};admitted[v['role']]=ref;resolved.append(dict(v,qualification=dict(ref,state_key='state',expected_state=r['state'])))
 old=O/'qualification-complete.json';archive=O/'recovery-2/qualification-complete.json';archive.parent.mkdir(exist_ok=True)
 if old.exists()and not archive.exists():shutil.copyfile(old,archive)
 assert not q.stop.is_set(),'Stoppedbeforeglobalpublication'
 q.save(old,{'state':'qualified_with_known_baseline_failures','research_qualification_only':True,'roles':admitted,'failed_roles':[],'base_revision':variants[0]['base_revision'],'baseline_failure_review':knownref,'known_baseline_failures':known['baseline_failures'],'baseline_contexts_reused':20,'other_contexts_verified':260,'supplemental_manifest':{'path':str(gap_path),'sha256':sha(gap_path)}})
 assert not q.stop.is_set(),'Stoppedbeforeresolvedpublication'
 q.save(O/'campaign/resolved-variants.json',{'state':'qualified_with_known_baseline_failures','base_revision':variants[0]['base_revision'],'variants':resolved,'baseline_failure_review':knownref})
except BaseException as e:
 q.stop.set();q.cleanup();q.save(O/'research-qualification-failure.json',{'state':'failed_requires_review','error':repr(e),'case_failures':failures,'traceback':traceback.format_exc()});raise
