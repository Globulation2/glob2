"""Await exact qualified+source-pushed candidate, admit once, arm terminal-only fit."""
import pathlib,sys,json,os,fcntl,time,subprocess,datetime,traceback
C=pathlib.Path(__file__).resolve().parent;D=C.parent;sys.path.insert(0,str(D/'launch-tool-source'));lock=(C/'qualification-guardian.lock').open('a');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
from tools.tournaments.common import atomic_json,file_hash
state=dict(status='WAITING-EXACT-GEN005-QUALIFICATION-AND-POSTQUALIFICATION',pid=os.getpid(),no_automatic_model_selection=True)
def save(**values):state.update(values,utc=datetime.datetime.now(datetime.timezone.utc).isoformat());atomic_json(C/'qualification-guardian-status.json',state)
try:
 request=json.loads((C/'DEPLOYMENT-PREPARATION.json').read_text());save(request_sha256=file_hash(C/'DEPLOYMENT-PREPARATION.json'))
 while True:
  if file_hash(C/'DEPLOYMENT-PREPARATION.json')!=state['request_sha256']:raise ValueError('Frozen guardian request changed')
  paths=[C/'QUALIFIED-RESEARCH-CANDIDATE.json',C/request['qualified_linux_bundle_metadata'],C/request['post_qualification_receipt']]
  if all(p.exists() for p in paths):break
  save(missing=[p.name for p in paths if not p.exists()]);time.sleep(30)
 gate=json.loads(paths[0].read_text());post=json.loads(paths[2].read_text())
 assert gate['source_revision']==request['expected_native_source_revision']==post['source'] and post['status']==request['expected_post_qualification_status']
 assert gate['model_sha256']==request['expected_model_sha256'] and gate['header_sha256']==request['expected_header_sha256'] and gate['sim_revision']==90 and gate['save_version']==180 and gate['threshold_milli']==16000 and gate['status']=='QUALIFIED-FOR-RESEARCH-CANDIDATE-ONLY'
 assert not (C/'A16-LAUNCH-INTENT.json').exists() and not (C/'LAUNCHED-A16-CANDIDATE-128.json').exists(),'Existing launch owns this candidate'
 save(status='PREPARING-EXACT-QUALIFIED-CANDIDATE',gate_sha256=file_hash(paths[0]),post_qualification_sha256=file_hash(paths[2]))
 subprocess.run([sys.executable,str(C/'prepare-qualified-a16.py')],check=True)
 subprocess.run([sys.executable,str(C/'launch-qualified-a16-once.py')],check=True)
 launched=json.loads((C/'LAUNCHED-A16-CANDIDATE-128.json').read_text());save(status='CANDIDATE-LAUNCHED-ARMING-1280-WATCHER',coordinator=launched)
 W=D/'cumulative-1280';assert not (W/'WATCHER-LAUNCHED.json').exists(),'Existing1280watcher owns process'
 atomic_json(W/'WATCHER-LAUNCH-INTENT.json',dict(utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),candidate_launch_sha256=file_hash(C/'LAUNCHED-A16-CANDIDATE-128.json'),script_sha256=file_hash(W/'terminal-watcher.py'),prepared_sha256=file_hash(W/'WATCHER-PREPARED.json')))
 with (W/'terminal-watcher.log').open('a') as out:watch=subprocess.Popen([sys.executable,str(W/'terminal-watcher.py')],cwd=W,stdout=out,stderr=subprocess.STDOUT,start_new_session=True)
 atomic_json(W/'WATCHER-LAUNCHED.json',dict(pid=watch.pid,utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),expected_slots=1280,fit_threshold_milli=16000,automatic_emit_build_promotion=False,candidate_launch_sha256=file_hash(C/'LAUNCHED-A16-CANDIDATE-128.json'),script_sha256=file_hash(W/'terminal-watcher.py')))
 save(status='COMPLETE-CANDIDATE-LAUNCHED-1280-WATCHER-ARMED',watcher_pid=watch.pid)
except Exception:
 save(status='FAILED-REQUIRES-REVIEW',error=traceback.format_exc());raise
