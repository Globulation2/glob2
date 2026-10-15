from pathlib import Path
import datetime,fcntl,hashlib,json,os,subprocess,time,traceback
O=Path(__file__).resolve().parent
def sha(p):
 with Path(p).open('rb')as f:return hashlib.file_digest(f,'sha256').hexdigest()
def save(p,d):Path(p).write_text(json.dumps(d,indent=2)+'\n')
lock=(O/'build.lock').open('a+');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
try:
 old=json.loads((O/'recovery-1/builds-complete.json').read_text());previous={x['source']['label']:x for x in old['sources']};current=[]
 for manifest in [O/'preparation.json',O/'weighted-preparation.json']:
  for src in json.loads(manifest.read_text())['sources']:
   label=src['label'];prior=previous[label];out=O/'recovery-1'/('unit-rebuild-'+label);out.mkdir(exist_ok=True)
   for k in ['native','engine']:assert sha(prior['outputs'][k]['path'])==prior['outputs'][k]['sha256']
   for name,h in src['source_hashes'].items():assert sha(Path(src['cwd'])/name)==h
   assert all(h==src['source_hashes'][name]for name,h in prior['source']['source_hashes'].items()if name!='src/common/ComputeExecutorHarness.cpp')
   cmd=src['build_command'][:src['build_command'].index('unit-tests')]+['unit-tests'];env={k:v for k,v in os.environ.items()if not k.startswith('GLOB2_')};env.update(src['env']);t=time.monotonic()
   save(O/'build-progress.json',{'state':'rebuilding_test_harness_only','source':label})
   with(out/'build.log').open('w')as log:r=subprocess.run(cmd,cwd=src['cwd'],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=1200)
   receipt={'source':src,'command':cmd,'returncode':r.returncode,'elapsed_seconds':time.monotonic()-t,'game_and_engine_binaries_unchanged':True,'original_full_build_seconds':prior['elapsed_seconds'],'original_build_receipt':str(O/'builds'/label/'complete.json'),'utc':datetime.datetime.now(datetime.timezone.utc).isoformat()};save(out/'receipt.json',receipt);assert r.returncode==0,label
   for k in ['native','engine']:assert sha(prior['outputs'][k]['path'])==prior['outputs'][k]['sha256']
   receipt['outputs']={k:{'path':src['expected_'+k],'sha256':sha(src['expected_'+k])}for k in ['native','unit','engine']};receipt['state']='built';current.append(receipt)
 save(O/'builds-complete.json',{'state':'complete','sources':current,'recovery':'Unit-harness-onlyrebuild; allgameandenginebinaryhashes unchanged.','utc':datetime.datetime.now(datetime.timezone.utc).isoformat()});save(O/'build-progress.json',{'state':'complete','sources':4,'harness_recovery_complete':True})
except BaseException as e:save(O/'build-failure.json',{'state':'failed','error':repr(e),'traceback':traceback.format_exc()});raise
