"""Bounded same-host engine benchmark; exploratory timing, no win-rate inference."""
import copy,json,resource,statistics,subprocess,sys,time
from pathlib import Path
OLD=Path('/home/bradley/glob2-win-continuation-20260908')
NEW=Path('/home/bradley/glob2-maxima-mainline-0e9092a79')
sys.path.insert(0,str(OLD/'tools'));import maxima_win_experiment as e
p=json.loads((OLD/'output/repairs-confirmation-v21/batch-01000/protocol.json').read_text())
out=NEW/'output/performance';out.mkdir(parents=True,exist_ok=False)
binaries={'old':Path(p['binary']),'mainline':NEW/'build-portable/src/glob2'}
for b in binaries.values():assert b.is_file()
rows=[]
for i in range(10):
 s=e.scenario(p,'qualification',9000+i)
 fmt=list(e.FORMATS)[i//2];count=e.FORMATS[fmt]
 s.update(format=fmt,opponent=[1,2,5,6][i%4],map=p['maps'][fmt][0],partition=0,swap=0,seat=0,offset=1)
 s['players']=[{'player':j,'team':j if fmt=='2v2' else (1+j*s['map']['teams']//count)%s['map']['teams'],'focal':j<2 if fmt=='2v2' else j==0} for j in range(count)]
 s['scenario_id']=e.identity({k:v for k,v in s.items() if k!='scenario_id'})
 settings=e.arm_settings(p,s);pair={}
 for label in (['old','mainline'] if i%2==0 else ['mainline','old']):
  folder=out/f'case-{i:02d}'/label;folder.mkdir(parents=True)
  protocol=copy.deepcopy(p);protocol['binary']=str(binaries[label]);protocol['binary_sha256']=e.sha(binaries[label])
  audit=folder/'audit.jsonl';args,env=e.command(protocol,s,settings,20000,audit)
  e.atomic(folder/'command.json',{'argv':args,'identities':env})
  cpu=resource.getrusage(resource.RUSAGE_CHILDREN);start=time.monotonic()
  with (folder/'engine.log').open('w') as log:subprocess.run(['taskset','-c','31',*args],cwd=OLD,env=e.clean_environment(),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
  elapsed=time.monotonic()-start;after=resource.getrusage(resource.RUSAGE_CHILDREN)
  receipt=e.read_audit(audit,env,s,settings);ticks=receipt['terminal']['tick']
  pair[label]={'seconds':elapsed,'cpu_seconds':after.ru_utime+after.ru_stime-cpu.ru_utime-cpu.ru_stime,'ticks':ticks,'ticks_per_second':ticks/elapsed,'binary_sha256':protocol['binary_sha256']}
 rows.append({'case':i,'format':fmt,'opponent':s['opponent'],'timings':pair,'speedup':pair['mainline']['ticks_per_second']/pair['old']['ticks_per_second']})
 e.atomic(out/'progress.json',rows)
summary={'scope':'10 scenarios, alternating engine order, same host/CPU31, same maps/seeds/settings, 20000 tick cap; engine behavior may differ; not whole-game throughput or outcome inference','cases':rows,'median_ticks_per_second_speedup':statistics.median(r['speedup'] for r in rows),'passed':True}
e.atomic(out/'RESULT.json',summary);print(json.dumps(summary),flush=True)
