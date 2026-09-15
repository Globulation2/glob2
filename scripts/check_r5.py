from pathlib import Path
import json,os,subprocess,tempfile
root=Path(__file__).resolve().parents[2];out=root/'artifacts/forts-tuning/r5-checks';out.mkdir(exist_ok=True)
cases=[('small',6,6,1,{'home-size':20}),('crowded',7,7,4,{}),('wide',8,7,3,{}),('tall',7,8,3,{}),('large',9,9,12,{}),('large-one',9,9,1,{}),('large-forts',8,8,4,{'home-size':26,'village-size':12,'river-width':13}),('small-towns',8,8,4,{'village-size':8,'river-width':3}),('scarce',7,7,4,{f'{r}-amount':0 for r in ['wheat','wood','stone','fruit','algae']}),('abundant',7,7,4,{f'{r}-amount':300 for r in ['wheat','wood','stone','fruit','algae']})]
rows=[]
for name,w,h,n,options in cases:
 for lakes in [0,1,3]:
  for seed in [11,13]:
   target=out/f'{name}-l{lakes}-s{seed}.json'
   command=[str(root/'build/src/MapGeneratorStudy'),'34',str(seed),'forts-lake-check',f'w={w}',f'h={h}',f'teams={n}',f'lakes={lakes}',f'result={target}']+[f'{k}={v}' for k,v in options.items()]
   with tempfile.TemporaryDirectory(prefix='forts-lake-check-') as profile,target.with_suffix('.log').open('w') as log:
    r=subprocess.run(command,cwd=root,env=dict(os.environ,GLOB2_USER_DIR=profile),stdout=log,stderr=log)
   report=json.loads(target.read_text());row={'case':name,'lakes':lakes,'seed':seed,'exit':r.returncode,'status':report['status'],'command':command};rows.append(row)
   if r.returncode:print(row,flush=True)
(out/'manifest.json').write_text(json.dumps(rows,indent=2)+'\n')
print(len(rows),'attempts',sum(r['exit']!=0 for r in rows),'failures',flush=True)
