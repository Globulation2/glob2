import gzip,json,subprocess,time
from pathlib import Path
BASE=Path('/Users/bradley/glob2/output/maxima-repairs-confirmation-v21')
CONFIG='/Users/bradley/glob2/output/maxima-live-controls-v21/ssh-config'
REMOTE='/home/bradley/glob2-win-continuation-20260908/output/repairs-confirmation-v21'
HOSTS=['devlaptop.local','pharaoh-dev-2.local','pharaoh-dev-3.local','therig.local']
def atomic(path,value):
 tmp=path.with_suffix('.pending');tmp.write_text(json.dumps(value,indent=2)+'\n');tmp.replace(path)
code='''import json,sqlite3
from pathlib import Path
base=Path(%r);out={}
for p in base.glob('batch-*'):
 if not (p/'queue.sqlite').exists():continue
 db=sqlite3.connect('file:'+str(p/'queue.sqlite')+'?mode=ro',uri=True)
 out[p.name]=[{'id':r[0],'status':r[1]} for r in db.execute('select id,status from jobs')]
print(json.dumps(out))
''' % REMOTE
while True:
 snapshots={};running=0;uncertain=0
 for host in HOSTS:
  res=subprocess.run(['ssh','-F',CONFIG,host,'python3 -'],input=code,text=True,capture_output=True,timeout=30,check=True)
  batches=json.loads(res.stdout);snapshots[host]={}
  for batch,rows in batches.items():
   dest=BASE/batch/'results';dest.mkdir(parents=True,exist_ok=True)
   keys=[r['id'] for r in rows if r['status']=='complete' and not (dest/r['id']/'result.json.gz').exists()]
   if keys:
    subprocess.run(['rsync','-az','-e','ssh -F '+CONFIG,'--files-from=-',host+':'+REMOTE+'/'+batch+'/runs/',str(dest)+'/'],input=''.join(k+'/result.json\n' for k in keys),text=True,check=True,timeout=120)
    for key in keys:
     p=dest/key/'result.json';value=json.loads(p.read_text());assert value['execution_id']==key
     compressed=p.with_suffix('.json.gz');tmp=p.with_suffix('.json.gz.pending')
     with gzip.open(tmp,'wb') as out:out.write(p.read_bytes())
     tmp.replace(compressed);p.unlink()
   counts={status:sum(r['status']==status for r in rows) for status in {r['status'] for r in rows}}
   snapshots[host][batch]=counts;running+=counts.get('running',0);uncertain+=counts.get('needs_investigation',0)
 pairs=0
 for manifest in BASE.glob('batch-*/manifest.json'):
  m=json.loads(manifest.read_text());dest=manifest.parent/'results'
  pairs+=sum(all((dest/row[arm]/'result.json.gz').exists() for arm in ('on','off')) for row in m['comparisons'])
 status={'time':time.time(),'stage':'retired_draining' if running else 'retired','reason':'user deprioritized inconclusive repairs; mainline performance work first','completed_pairs':pairs,'running':running,'needs_investigation':uncertain,'new_dispatch':False,'default_changes':False,'inference':'Last scheduled look remains look-01000.json; no equivalence claim','hosts':snapshots}
 atomic(BASE/'DRAIN_STATUS.json',status);atomic(BASE/'STATUS.json',status)
 if not running:break
 time.sleep(30)
