#!/usr/bin/env python3
"""Reproduce Comb parameter matrices using native xargs workers.

python3 study.py prepare /absolute/path/to/study
cp build/src/glob2 /absolute/path/to/study/glob2
xargs -0 -n1 -P6 /bin/bash -c < /absolute/path/to/study/jobs.nul
python3 study.py report /absolute/path/to/study

Replace prepare with workers for the 640-case starting-worker matrix.
"""
import json,random,shlex,sys,pathlib,collections,statistics
root=pathlib.Path(__file__).resolve().parent
keys=['wheat-amount','wood-amount','stone-amount','algae-amount','fruit-amount']
if sys.argv[1] in ('prepare', 'workers'):
 out=root/sys.argv[2];out.mkdir(exist_ok=True)
 binary=out/'glob2';jobs=[]
 def add(kind,seed,w,h,t,opts,control='',value=None):
  jobs.append(dict(study=kind,seed=seed,w=w,h=h,teams=t,set=opts,control=control,value=value))
 for w,h in [(256,256),(256,512),(512,256),(512,512)]:
  for t in range(2,9 if w==h==512 else 5):
   for pen in range(2,5):
    for seed in [7,23,101,401]:add('shape',seed,w,h,t,{'peninsulas':pen})
 for seed in [7,29,401,913]:
  for key in keys+['peninsulas']:
   for v in (range(2,5) if key=='peninsulas' else range(0,301,25)):
    add('knob',seed,256,256,4,{key:v},key,v)
 for w,h in [(256,256),(256,512),(512,256),(512,512)]:
  for pen in range(2,5):
   for seed in [7,29,401]:
    for mask in range(32):add('extreme',seed,w,h,8 if w==h==512 else 4,dict(zip(keys,[300 if mask>>k&1 else 0 for k in range(5)]),peninsulas=pen))
 rng=random.Random(82931)
 for i in range(2000):
  w,h=rng.choice([(256,256),(256,512),(512,256),(512,512)])
  add('random',rng.randrange(1,2**31),w,h,rng.randint(2,8 if w==h==512 else 4),dict(zip(keys,[rng.randrange(13)*25 for _ in keys]),peninsulas=rng.randint(2,4)))
 if sys.argv[1]=='workers':
  jobs=[]
  for w,h in [(256,256),(256,512),(512,256),(512,512)]:
   for workers in range(1,9):
    for pen in [2,4]:
     for seed in [7,401]:jobs.append(dict(study='workers',seed=seed,w=w,h=h,teams=8 if w==h==512 else 4,workers=workers,set={'peninsulas':pen},control='workers',value=workers))
  rng=random.Random(4158)
  for i in range(512):
   w,h=rng.choice([(256,256),(256,512),(512,256),(512,512)])
   opts={k:rng.randrange(13)*25 for k in keys};opts['peninsulas']=rng.randint(2,4)
   jobs.append(dict(study='random-workers',seed=rng.randrange(1,2**31),w=w,h=h,teams=rng.randint(2,8 if w==h==512 else 4),workers=rng.randint(1,8),set=opts,control='',value=None))
 json.dump(jobs,open(out/'manifest.json','w'),indent=2)
 with open(out/'jobs.nul','wb') as f:
  for i,j in enumerate(jobs):
   cmd=[str(binary),'--generate-map','comb','--seed',str(j['seed']),'--width',str(j['w']),'--height',str(j['h']),'--teams',str(j['teams']),'--json',str(out/f'{i}.json')]
   cmd+=['--workers',str(j.get('workers',4))]
   for k,v in j['set'].items():cmd+=['--set',f'{k}={v}']
   f.write((shlex.join(cmd)+' > '+shlex.quote(str(out/f'{i}.log'))+' 2>&1\0').encode())
 print(len(jobs),'jobs',dict(collections.Counter(j['study'] for j in jobs)))
else:
 out=root/sys.argv[2];jobs=json.load(open(out/'manifest.json'));counts=collections.Counter();fail=[];rows=[]
 for i,j in enumerate(jobs):
  p=out/f'{i}.json'
  if not p.exists():continue
  try:d=json.load(open(p))
  except:continue
  ok=d.get('report_type')!='generation_failure';counts[(j['study'],ok)]+=1
  if not ok:
   log=(out/f'{i}.log').read_text();fail.append((i,j,log[-700:]));continue
  r={**j,'index':i,'resource':{k:v['coverage']['tiles'] for k,v in d['resources']['types'].items()}}
  rows.append(r)
 print('completed',sum(counts.values()),'/',len(jobs),'counts',dict(counts),'failures',len(fail))
 print(json.dumps(fail[:8],indent=2))
 json.dump({'counts':{str(k):v for k,v in counts.items()},'failures':fail,'rows':rows},open(out/'analysis.json','w'),indent=2)
 for key in keys+['peninsulas']:
  vals=collections.defaultdict(list)
  res={'wheat-amount':'wheat','wood-amount':'wood','stone-amount':'stone','algae-amount':'algae','fruit-amount':'cherry'}.get(key)
  if res:
   for r in rows:
    if r['study']=='knob' and r['control']==key:vals[r['value']].append(sum(r['resource'].get(k,0) for k in (['cherry','orange','prune'] if key=='fruit-amount' else [res])))
   print(key,[(v,round(statistics.mean(a),1)) for v,a in sorted(vals.items())])
