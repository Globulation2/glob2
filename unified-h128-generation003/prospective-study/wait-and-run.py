import datetime,fcntl,hashlib,json,os,pathlib,subprocess,time,traceback
B=pathlib.Path(__file__).resolve().parent;REQUEST=B/'STUDY-REQUEST.json';P=json.loads(REQUEST.read_text());lock=(B/'study.lock').open('w');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)

def stamp():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def write():
 p=B/'STATUS.tmp';p.write_text(json.dumps(state,indent=2)+'\n');p.replace(B/'STATUS.json')
state={'status':'waiting_for_positive_gen003_qualification','supervisor_pid':os.getpid(),'started_utc':stamp(),'source_revision':P['source_revision'],'request_sha256':sha(REQUEST),'phase_history':[]};write()
def stage(script):
 directory=B/('stage-'+script.removesuffix('.py'));directory.mkdir(exist_ok=False)
 state.update(status=script,active={'script':script,'launch_intent_utc':stamp()});write()
 with(directory/'stdout.log').open('w')as so,(directory/'stderr.log').open('w')as se:
  child=subprocess.Popen(['python3','-u',str(B/script)],cwd=B,stdout=so,stderr=se,stdin=subprocess.DEVNULL,start_new_session=True)
  state['active']['pid']=child.pid;write();rc=child.wait()
 state['phase_history'].append({'script':script,'pid':child.pid,'returncode':rc,'finished_utc':stamp()});state.pop('active',None);write()
 if rc:raise RuntimeError(script+' failed; inspect preserved stage logs, no automatic retry')
try:
 deadline=time.monotonic()+86400;receipt=pathlib.Path(P['qualification_receipt'])
 while not receipt.exists():
  if time.monotonic()>deadline:raise TimeoutError('No positive gen003 receipt within24hours; no games/builds launched')
  time.sleep(10)
 q=json.loads(receipt.read_text());assert q['source_revision']==P['source_revision'];assert q['model_sha256']==P['model_sha256'];assert q['header_sha256']==P['header_sha256'];assert q['ordinary_release']is True and q['model_trained']is True and q['stable_promotion']is False
 for key in ['main_build','integer_bounds_parity_530_ubsan','adaptive_no_fixed_interval_override','probe_off_on_threads_1_4','mac_linux_world_and_finalsave','on_thread_diagnostic_csv_exact']:assert q['checks'][key]is True,key
 binary=pathlib.Path(P['frozen_runtime'])/'bin/glob2';assert sha(binary)==q['binary_sha256'];P['frozen_binary_sha256']=q['binary_sha256']
 for m in P['maps']:assert sha(B/'maps'/m['group']/'map.map.gz')==m['sha256']
 final=B/'PLAN.json';assert not final.exists();final.write_text(json.dumps(P,indent=2)+'\n');(B/'PLAN.sha256').write_text(sha(final)+'\n')
 (B/'QUALIFICATION-ACCEPTED.json').write_text(json.dumps({'accepted_utc':stamp(),'source_revision':P['source_revision'],'receipt_path':str(receipt),'receipt_sha256':sha(receipt),'binary_sha256':sha(binary),'plan_sha256':sha(final),'checks':q['checks']},indent=2)+'\n')
 state['qualification_accepted_utc']=stamp();state['plan_sha256']=sha(final);write()
 stage('run-quality.py');stage('summarize-quality.py');stage('build-perf-only.py');stage('run-perf-study.py');stage('finalize-study.py')
 state.update(status='complete',finished_utc=stamp());write()
except BaseException as e:
 state.update(status='failed',error=repr(e),traceback=traceback.format_exc(),finished_utc=stamp());write();raise
