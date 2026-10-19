import pathlib,subprocess,json,os,signal,time,datetime,hashlib,fcntl
B=pathlib.Path(__file__).resolve().parent;P=json.loads((B/'PLAN.json').read_text());lock=(B/'supervisor.lock').open('w');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
assert hashlib.sha256((B/'run-research.py').read_bytes()).hexdigest()==P['research_script_sha256']
cmd=[P['python'],'-u',str(B/'run-research.py')];env=dict(os.environ,OPENBLAS_NUM_THREADS='1',OMP_NUM_THREADS='1',MKL_NUM_THREADS='1',NUMEXPR_NUM_THREADS='1');state={'started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'supervisor_pid':os.getpid(),'argv':cmd,'cwd':str(B),'environment':{k:env[k]for k in ['OPENBLAS_NUM_THREADS','OMP_NUM_THREADS','MKL_NUM_THREADS','NUMEXPR_NUM_THREADS']},'plan_sha256':hashlib.sha256((B/'PLAN.json').read_bytes()).hexdigest(),'wall_timeout_seconds':P['budget']['wall_seconds_limit'],'scope':'singleCPU nice19 artifactonly; no games/emission/production changes'}
with(B/'research.log').open('w')as log:
 child=subprocess.Popen(cmd,cwd=B,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True);state['research_pid']=child.pid;(B/'LAUNCHED.json').write_text(json.dumps(state,indent=2)+'\n')
 try:rc=child.wait(timeout=P['budget']['wall_seconds_limit'])
 except subprocess.TimeoutExpired:
  state['wall_capped']=True;os.killpg(child.pid,signal.SIGTERM)
  try:rc=child.wait(timeout=5)
  except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);rc=child.wait()
state.update(returncode=rc,finished_utc=datetime.datetime.now(datetime.timezone.utc).isoformat());(B/'SUPERVISOR-COMPLETE.json').write_text(json.dumps(state,indent=2)+'\n')
