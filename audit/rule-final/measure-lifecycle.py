from pathlib import Path
import os,json,subprocess,statistics,math
root=Path.cwd();out=Path(__file__).resolve().parent;binary_dir=root/'artifacts/gradient-cpu-audit';env={k:v for k,v in os.environ.items() if not k.startswith(('GLOB2_','SDL_'))};env.update(GLOB2_GRADIENT_BACKEND='cpu',GLOB2_GRADIENT_VERIFY='off')
rows=[]
for pair in range(8):
 for threaded in (False,True):
  for v in (('before','master','fixed','candidate') if pair%2==0 else ('candidate','fixed','master','before')):
   cmd=['taskset','-c','0',str(binary_dir/(v+'-lifecycle')),'1000000',*(['threaded'] if threaded else [])]
   r=subprocess.run(cmd,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True);assert r.returncode==0,(v,r.stderr)
   (out/f'lifecycle-{pair}-{int(threaded)}-{v}.txt').write_text(r.stdout)
   times={};metadata={}
   for line in r.stdout.splitlines():
    p=line.split()
    if p[0]=='job_bytes':metadata=dict(zip(p[::2],map(int,p[1::2])))
    else:times[p[0]]=int(p[2])/int(p[1])
   rows.append({'pair':pair,'threaded':threaded,'variant':v,'command':cmd,'ns_per_operation':times,'metadata':metadata})
   (out/'lifecycle-cost-runs.json').write_text(json.dumps(rows,indent=2)+'\n')
summary=[]
for threaded in (False,True):
 for v in ('before','master','fixed','candidate'):
  selected=[r for r in rows if r['threaded']==threaded and r['variant']==v]
  phases={}
  for k in selected[0]['ns_per_operation']:
   data=[r['ns_per_operation'][k] for r in selected];m=statistics.mean(data);h=2.364624251*statistics.stdev(data)/math.sqrt(8)
   phases[k]={'mean_ns':m,'ci95':[m-h,m+h]}
  summary.append({'threaded':threaded,'variant':v,'phases':phases,'metadata':selected[0]['metadata']})
(out/'lifecycle-cost-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print('COMPLETE',len(rows))
