"""Explicitly approved final winner cohorts at a fresh natural campaign boundary."""
from pathlib import Path
import datetime,fcntl,hashlib,json,os,signal,subprocess,sys,time,traceback
O=Path(__file__).resolve().parent;P=O.parents[1];F=O.parent/'final-cohort';sys.path.insert(0,str(P))
from process_cleanup import process_identity,kill_owned_tree
def interrupted(signum,frame):raise KeyboardInterrupt('Controller signal '+str(signum))
signal.signal(signal.SIGTERM,interrupted);signal.signal(signal.SIGINT,interrupted)
lock=(O/'controller.lock').open('a+');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
def sha(q):
 with Path(q).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def read(q):return json.loads(Path(q).read_text())
def save(q,x):
 q=Path(q);t=q.with_suffix('.tmp');t.write_text(json.dumps(x,indent=2)+'\n');t.replace(q)
def now():return datetime.datetime.now(datetime.timezone.utc).isoformat()
a=read(O/'root-approved.json');review=read(O/'independent-review.json')
assert a['approved'] and review['approved'] and a['controller_sha256']==sha(__file__)==review['controller_sha256']
required=[F/'launcher.py',F/'analyze.py',F/'plan.json',F/'root-approved.json',F/'independent-review.json',P/'process_cleanup.py',O.parent/'wake-only-allcore/latest-resolve-complete.json',O.parent/'balanced-n4/analysis.json',O.parent/'final-boundary-launch.json',O.parent/'final-boundary-request.json']
assert set(map(str,required))<={x['path'] for x in a['bound_files']}
for x in a['bound_files']:assert sha(x['path'])==x['sha256'],x['path']
campaign=a['campaign'];assert campaign['pid']==4072783 and str(campaign['starttime'])=='327146496'
assert a['boundary_nonce']=='64cdc9087b0c4c8685c775910272846d'
assert Path(a['boundary_status_file'])==O.parent/'final-boundary-status.json'
fapproval=read(F/'root-approved.json');assert fapproval['approved'] and fapproval['campaign']==campaign and set(fapproval['regimes'])=={'serial','waves'}
assert fapproval['source_sha256']==sha(F/'launcher.py') and fapproval['plan_sha256']==sha(F/'plan.json')
plan=read(F/'plan.json');assert plan['base_revision']=='054804590ee1dd109f09642fc05654c324f08344' and len(set(plan['seeds']))==32 and plan['cap_seconds']==3600
resolved=read(plan['latest_resolve']);assert resolved['base_revision']==plan['base_revision']
for v in resolved['variants']:assert sha(v['binary'])==v['sha256']==fapproval['binary_hashes'][v['role']]
end=time.monotonic()+a.get('wait_seconds',14400)
while True:
 status=Path(a['boundary_status_file'])
 if status.exists():
  g=read(status)
  if g.get('boundary_nonce')==a['boundary_nonce']:break
 assert time.monotonic()<end,'Fresh boundary wait timeout';time.sleep(1)
assert g['state']=='paused_between_cohorts' and g['target']=='combined-auto-cpu/Oazis' and g['completed_receipts']==16 and not g['active_games'] and g['no_measured_run_interrupted'] and g['runner_pid']==campaign['pid'] and str(g['runner_starttime'])==str(campaign['starttime'])
def stopped():
 ident=process_identity(campaign['pid']);assert ident and ident[1]==str(campaign['starttime']);q=Path('/proc')/str(campaign['pid']);assert b'expanded-runner.py' in(q/'cmdline').read_bytes() and (q/'stat').read_text().split(') ',1)[1].split()[0]=='T'
 for q in Path('/proc').glob('[0-9]*/cmdline'):
  try:argv=[x.decode(errors='replace') for x in q.read_bytes().split(b'\0') if x];state=(q.parent/'stat').read_text().split(') ',1)[1].split()[0]
  except(OSError,IndexError):continue
  if state=='Z' or not argv or int(q.parent.name)==os.getpid():continue
  assert not Path(argv[0]).name.startswith('glob2'),('Active native game',q.parent.name)
  if Path(argv[0]).name.startswith('python'):
   assert not any(Path(x).name in ['paired-controller.py','resume-after-parity-handoff.py','parallel-parity.py','pilot.py','controller.py','final-controller.py','launcher.py','timing-runner.py'] for x in argv[1:3]),('Other controller',q.parent.name)
stopped();assert not(O/'started.json').exists()
for regime in ['serial','waves']:assert not(F/regime).exists(),'Never silently reuse attempted final regime'
save(O/'started.json',{'pid':os.getpid(),'starttime':process_identity(os.getpid())[1],'nonce':a['boundary_nonce'],'utc':now()});complete=False
try:
 for regime in ['serial','waves']:
  spec=plan['regimes'][regime];assert spec['runs']==128 and spec['concurrent_games']==(1 if regime=='serial' else 4)
  stage_timeout=((spec['runs']+spec['concurrent_games']-1)//spec['concurrent_games'])*(plan['cap_seconds']+30)+600
  for stage,cmd,timeout in [(regime,['python3',str(F/'launcher.py'),regime,'--execute'],stage_timeout),(regime+'-analysis',['python3',str(F/'analyze.py'),regime],300)]:
   stopped()
   for x in a['bound_files']:assert sha(x['path'])==x['sha256'],x['path']
   save(O/'progress.json',{'state':'running','stage':stage,'utc':now()});assert not(O/(stage+'-receipt.json')).exists()
   with(O/(stage+'.log')).open('w') as log:
    env={k:v for k,v in os.environ.items() if not k.startswith('GLOB2_')};child=subprocess.Popen(cmd,cwd=P,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True);start=time.monotonic()
    try:rc=child.wait(timeout=timeout)
    except BaseException:kill_owned_tree(child.pid,O/(stage+'-cleanup.json'));child.wait(timeout=30);raise
   save(O/(stage+'-receipt.json'),{'command':cmd,'returncode':rc,'elapsed_seconds':time.monotonic()-start,'timeout_seconds':timeout});assert rc==0
  c=read(F/regime/'complete.json');analysis=read(F/regime/'analysis.json');assert c['state']=='complete' and c['runs']==128 and analysis['regime']==regime and analysis['state']=='validated' and len(analysis['maps'])==2
 save(O/'complete.json',{'state':'complete','utc':now(),'runs':256,'regimes':{r:{n:sha(F/r/n) for n in ['complete.json','analysis.json']} for r in ['serial','waves']}});complete=True
except BaseException as e:save(O/'failure.json',{'error':str(e),'traceback':traceback.format_exc()});raise
finally:
 stopped()
 if complete:
  os.kill(campaign['pid'],signal.SIGCONT);save(O/'progress.json',{'state':'resumed_after_complete','utc':now()});save(P/'campaign-hold.json',{'state':'resumed_after_final_cohorts','runner_pid':campaign['pid'],'runner_starttime':campaign['starttime'],'boundary_nonce':a['boundary_nonce'],'no_measured_run_interrupted':True})
 else:
  save(O/'progress.json',{'state':'failed_needs_repair','utc':now()});save(P/'campaign-hold.json',dict(g,needs_repair=True,reason='Finalcohort failure; retain clean paused boundary'))
