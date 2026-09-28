import json,os,subprocess,time,shlex,hashlib,random
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];OUT=Path(__file__).resolve().parent
cases=json.loads((ROOT/'artifacts/lazy-gradient/cases.json').read_text())
bins={'previous':ROOT/'artifacts/lazy-gradient-cleanup/glob2','api':OUT/'glob2','profile':OUT/'glob2-profile'}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
plan=[];rng=random.Random(20260928)
for repeat in range(3):
 ordered=list(cases);rng.shuffle(ordered)
 for c in ordered:
  order=['previous','api'] if repeat%2==0 else ['api','previous']
  if cases.index(c)%2:order.reverse()
  for v in order:plan.append(dict(case=c['name'],repeat=repeat,variant=v))
for repeat in range(3):
 for c in cases:plan.append(dict(case=c['name'],repeat=repeat,variant='profile'))
(OUT/'profile-plan.json').write_text(json.dumps({'source_commit':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'power_settings':subprocess.check_output(['pmset','-g','custom'],text=True),'seed':20260928,'binary_sha256':{k:sha(v) for k,v in bins.items()},'cases':cases,'runs':plan,'note':'Three paired old/new lazy repetitions per original case, alternating AB/BA; three instrumented runs per case. Phase counters perturb the hot loop and are not used for speedup claims. All runs retained.'},indent=2)+'\n')
env=os.environ.copy()
for k in list(env):
 if k.startswith('GLOB2_'):env.pop(k)
env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
def run(c,v,label):
 dest=OUT/'performance'/label;dest.parent.mkdir(exist_ok=True);log=dest.with_suffix('.log')
 command=[str(bins[v]),'--run-game','--map-file',str(ROOT/'artifacts/lazy-gradient/runs'/c['name']/'generated/map-r0.map'),'--game-seed',str(c['game_seed']),'--ticks',str(c['ticks']),'--telemetry','team-timeline','--lazy-building-gradients','true','--output-dir',str(dest)]
 for ai in c['players']:command+=['--player',ai]
 before=subprocess.check_output(['pmset','-g','batt'],text=True)
 thermal=subprocess.check_output(['pmset','-g','therm'],text=True)
 competing=[s for s in subprocess.check_output(['ps','-axo','pid,time,comm'],text=True).splitlines() if s.rstrip().endswith('/glob2')]
 print('RUN',label,before.splitlines()[0],flush=True)
 start=time.monotonic()
 with log.open('w') as f:
  child=subprocess.Popen(command,cwd=ROOT,env=env,stdout=f,stderr=subprocess.STDOUT)
  _,status,usage=os.wait4(child.pid,0);child.returncode=os.waitstatus_to_exitcode(status)
 assert child.returncode==0
 wall=time.monotonic()-start;after=subprocess.check_output(['pmset','-g','batt'],text=True)
 result=json.loads((dest/'result.json').read_text());base=json.loads((ROOT/'artifacts/lazy-gradient/runs'/c['name']/'base/result.json').read_text())
 assert all(result[k]==base[k] for k in ['ticks','termination','teams'])
 perf={};profile={}
 for line in log.read_text().splitlines():
  if line.startswith('GLOB2_PERF_FINAL '):
   d=dict(x.split('=',1) for x in shlex.split(line)[1:] if '=' in x)
   if 'scope' in d:perf[d['scope']]=d
  if line.startswith('GRADIENT_COST_PROFILE '):profile={k:int(v) for k,v in (x.split('=') for x in line.split()[1:])}
 if v=='profile':assert profile['builds']>0
 row=dict(case=c['name'],variant=v,label=label,command=command,cpu_s=usage.ru_utime+usage.ru_stime,wall_s=wall,peak_rss_bytes=usage.ru_maxrss,before=before,after=after,thermal=thermal,other_glob2_processes=competing,result=result,performance=perf,profile=profile)
 print('DONE',label,round(row['cpu_s'],3),'CPU seconds',flush=True)
 return row
for v in bins:
 r=run(cases[0],v,'warmup-'+v)
 with (OUT/'profile-warmups.jsonl').open('a') as f:f.write(json.dumps(r)+'\n')
for b in plan:
 c=next(c for c in cases if c['name']==b['case']);r=run(c,b['variant'],f"{b['case']}-r{b['repeat']}-{b['variant']}");r['repeat']=b['repeat']
 with (OUT/'profile-measurements.jsonl').open('a') as f:f.write(json.dumps(r)+'\n')
print('COMPLETE 36 measured runs',flush=True)
