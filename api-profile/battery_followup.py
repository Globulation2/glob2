import json,os,subprocess,time,shlex,hashlib,random
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];OUT=Path(__file__).resolve().parent
cases=json.loads((ROOT/'artifacts/lazy-gradient/cases.json').read_text())
bins={'previous':ROOT/'artifacts/lazy-gradient-cleanup/glob2','api':OUT/'glob2','profile':OUT/'glob2-profile'}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
plan=[]
for repeat in [3,4]:
 for c in cases:
  order=['api','previous'] if repeat==3 else ['previous','api']
  for v in order:plan.append(dict(case=c['name'],repeat=repeat,variant=v))
(OUT/'battery-followup-plan.json').write_text(json.dumps({'reason':'User chose to stay on battery. Run the previously declared two opposite-order pairs per workload on battery; retain all initial results and separately analyze battery-only matched pairs.','runs':plan},indent=2)+'\n')
env=os.environ.copy()
for k in list(env):
 if k.startswith('GLOB2_'):env.pop(k)
env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
def run(c,v,label):
 dest=OUT/'performance'/label;dest.parent.mkdir(exist_ok=True);log=dest.with_suffix('.log')
 command=[str(bins[v]),'--run-game','--map-file',str(ROOT/'artifacts/lazy-gradient/runs'/c['name']/'generated/map-r0.map'),'--game-seed',str(c['game_seed']),'--ticks',str(c['ticks']),'--telemetry','team-timeline','--lazy-building-gradients','true','--output-dir',str(dest)]
 for ai in c['players']:command+=['--player',ai]
 before=subprocess.check_output(['pmset','-g','batt'],text=True)
 assert "'Battery Power'" in before, 'Follow-up requires battery power'
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
for v in ['previous','api']:
 r=run(cases[0],v,'battery-warmup-'+v)
 with (OUT/'battery-warmups.jsonl').open('a') as f:f.write(json.dumps(r)+'\n')
for b in plan:
 c=next(c for c in cases if c['name']==b['case']);r=run(c,b['variant'],f"{b['case']}-r{b['repeat']}-{b['variant']}");r['repeat']=b['repeat']
 with (OUT/'profile-measurements.jsonl').open('a') as f:f.write(json.dumps(r)+'\n')
print('COMPLETE 16 battery follow-up runs',flush=True)
