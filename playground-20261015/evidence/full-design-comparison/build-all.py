#!/usr/bin/env python3
"""Sequential release source builds; all CPUs within a build, no timed games."""
from pathlib import Path
import datetime,fcntl,hashlib,json,os,subprocess,time,traceback
O=Path(__file__).resolve().parent

def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def save(p,v):
 p=Path(p);p.parent.mkdir(parents=True,exist_ok=True);q=p.with_suffix('.tmp');q.write_text(json.dumps(v,indent=2)+'\n');q.replace(p)
def now():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def checkquiet():
 for q in Path('/proc').glob('[0-9]*'):
  try:c=(q/'comm').read_text().strip();s=(q/'stat').read_text().split(') ',1)[1].split()[0]
  except (OSError,IndexError):continue
  assert s=='Z' or not c.startswith('glob2'),('Native game overlaps build',q.name,c)
lock=(O/'build.lock').open('a+');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
assert json.loads((O/'build-window.json').read_text())['state']=='clean_build_window'
save(O/'build-started.json',{'pid':os.getpid(),'utc':now(),'script_sha256':sha(__file__)})
try:
 sources=[]
 for manifest in [O/'preparation.json',O/'weighted-preparation.json']:
  end=time.monotonic()+7200
  while not manifest.exists():
   assert time.monotonic()<end,'Source preparation timeout';save(O/'build-progress.json',{'state':'waiting_for_source','manifest':str(manifest),'utc':now()});time.sleep(2)
  spec=json.loads(manifest.read_text());assert spec['base_revision']=='054804590ee1dd109f09642fc05654c324f08344'
  for source in spec['sources']:
   label=source['label'];out=O/'builds'/label;out.mkdir(parents=True,exist_ok=True)
   for name,h in source['source_hashes'].items():assert sha(Path(source['cwd'])/name)==h,(label,name)
   receipt=out/'complete.json'
   if receipt.exists():
    r=json.loads(receipt.read_text());assert r['source']==source and all(sha(x['path'])==x['sha256'] for x in r['outputs'].values());sources.append(r);continue
   checkquiet();save(O/'build-progress.json',{'state':'building','source':label,'utc':now()})
   env={k:v for k,v in os.environ.items() if not k.startswith('GLOB2_')};env.update(source['env']);t=time.monotonic()
   with (out/'build.log').open('w') as log:
    r=subprocess.run(source['build_command'],cwd=source['cwd'],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=7200)
   result={'source':source,'returncode':r.returncode,'elapsed_seconds':time.monotonic()-t,'utc':now(),'manifest_sha256':sha(manifest)}
   save(out/'receipt.json',result);assert r.returncode==0,('Build failed',label)
   for name,h in source['source_hashes'].items():assert sha(Path(source['cwd'])/name)==h,(label,name)
   result['outputs']={key:{'path':source['expected_'+key],'sha256':sha(source['expected_'+key])} for key in ['native','unit','engine']};result['state']='built';save(receipt,result);sources.append(result)
 save(O/'builds-complete.json',{'state':'complete','utc':now(),'sources':sources});save(O/'build-progress.json',{'state':'complete','sources':len(sources),'utc':now()})
except BaseException as e:
 save(O/'build-failure.json',{'state':'failed','error':str(e),'traceback':traceback.format_exc(),'utc':now()});raise
