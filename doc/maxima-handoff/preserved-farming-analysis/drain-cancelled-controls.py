import json,subprocess,time,concurrent.futures
from pathlib import Path
live=Path('/Users/bradley/glob2');b=live/'output/maxima-conversion-fixed-controls';cfg=str(live/'output/maxima-live-controls-v21/ssh-config');remote='/home/bradley/glob2-maxima-conversion-fixed/output/fresh-controls';hosts=list(json.loads((b/'allocations.json').read_text()))
def collect(h):
 code="import sqlite3,json;d=sqlite3.connect("+repr(remote+'/queue.sqlite')+");print(json.dumps(list(d.execute('select id,status from jobs'))))"
 r=subprocess.run(['ssh','-F',cfg,'-o','ConnectTimeout=8',h,'python3 -'],input=code,text=True,capture_output=True,check=True,timeout=20);rows=json.loads(r.stdout);keys=[k for k,s in rows if s=='complete']
 files=''.join(k+'/result.json\n' for k in keys)
 subprocess.run(['rsync','-az','-e','ssh -o ConnectTimeout=8 -F '+cfg,'--files-from=-',h+':'+remote+'/runs/',str(b/'results')+'/'],input=files,text=True,capture_output=True,check=True,timeout=45)
 return h,{s:sum(x==s for _,x in rows) for _,s in rows}
for _ in range(240):
 status={};errors={}
 with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
  futures={pool.submit(collect,h):h for h in hosts}
  for f,h in futures.items():
   try:k,v=f.result();status[k]=v
   except Exception as error:errors[h]=str(error)
 (b/'CANCELLED_DRAIN_STATUS.json').write_text(json.dumps({'time':time.time(),'hosts':status,'errors':errors,'new_dispatch':False},indent=2)+'\n')
 if not errors and not any(v.get('running',0) for v in status.values()):break
 time.sleep(15)
else:raise RuntimeError('drain incomplete; inspect preserved queues')
