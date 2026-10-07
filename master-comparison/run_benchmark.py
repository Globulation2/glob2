import argparse,csv,hashlib,json,os,pathlib,resource,statistics,subprocess,time,platform
ROOT=pathlib.Path(__file__).resolve().parent
WORK={v:pathlib.Path('/home/bradley/.codex/worktrees')/f'render-bench-{v}'/'glob2' for v in ('baseline','final')}
FIXTURES={'balanced':ROOT/'fixture-balanced/initial.game.gz','large512':ROOT/'fixture-512/final.game.gz'}
BIN={v:r/'build/linux/client/release/test/glob2-engine-tests' for v,r in WORK.items()}

def cpu_stats():
 return {r.split()[0]:[int(v) for v in r.split()[1:]] for r in pathlib.Path('/proc/stat').read_text().splitlines() if r.startswith('cpu')}

def run(case,cores,variant,repeat,ticks,warmup,draw=1,verify=0,phase='measure'):
 name=f'{phase}-{case}-{cores}cpu-draw{draw}-{repeat}-{variant}'
 out=ROOT/'runs'/name;out.mkdir(parents=True,exist_ok=False)
 env={**os.environ,'GLOB2_TEST_DISPLAY':'1','SDL_AUDIODRIVER':'dummy','SDL_VIDEODRIVER':'x11','LP_NUM_THREADS':'1',
      'GLOB2_USER_DATA_DIR':str(out/'profile'),'GLOB2_TEST_ARTIFACTS_ROOT':str(out/'artifacts'),
      'GLOB2_BENCH_FIXTURE':str(FIXTURES[case]),'GLOB2_BENCH_THREADS':str(min(cores,4)),'GLOB2_BENCH_DRAW':str(draw),
      'GLOB2_BENCH_FPS':'60','GLOB2_BENCH_WARMUP':str(warmup),'GLOB2_BENCH_TICKS':str(ticks),'GLOB2_BENCH_VERIFY':str(verify)}
 cpus={2:'14,15',4:'12-15',8:'8-15'}[cores]
 command=['taskset','-c',cpus,str(BIN[variant]),'--test-suite=RenderRevisionBenchmark','--no-breaks=true']
 meta={'name':name,'case':case,'cores':cores,'variant':variant,'repeat':repeat,'draw':draw,'verify':verify,'phase':phase,
       'command':command,'cwd':str(WORK[variant]),'environment':{k:v for k,v in env.items() if k.startswith(('GLOB2_','SDL_','LP_'))},
       'load_before':os.getloadavg(),'cpu_before':cpu_stats(),'started_epoch':time.time()}
 with (out/'run.log').open('w') as log:
  p=subprocess.Popen(command,cwd=WORK[variant],env=env,stdout=log,stderr=subprocess.STDOUT)
  _,status,usage=os.wait4(p.pid,0);p.returncode=os.waitstatus_to_exitcode(status)
 meta.update(returncode=p.returncode,load_after=os.getloadavg(),cpu_after=cpu_stats(),finished_epoch=time.time(),lifetime_cpu_s=usage.ru_utime+usage.ru_stime,lifetime_peak_rss_bytes=usage.ru_maxrss*1024)
 (out/'invocation.json').write_text(json.dumps(meta,indent=2)+'\n')
 if p.returncode:raise RuntimeError(f'{name}: exit {p.returncode}; {out}/run.log')
 metric=list(out.rglob('metrics.csv'));assert len(metric)==1,metric
 data=next(csv.DictReader(metric[0].open()));data={k:int(v) for k,v in data.items()}
 assert data['rss_median_bytes']>0, ('no steady-state RSS samples',name)
 if draw:assert data['frames']>0, ('no steady-state rendered frames',name)
 rows=list(csv.DictReader(next(out.rglob('ticks.csv')).open()))
 for key in ('simulation_ns','owner_iteration_ns','completion_interval_ns'):
  values=sorted(int(r[key]) for r in rows)
  for q in (50,95,99):data[f'{key}_p{q}']=values[min(len(values)-1,len(values)*q//100)]
 data['tps']=data['measured_ticks']*1e9/data['wall_ns'];data['cpu_us_per_tick']=data['process_cpu_ns']/data['measured_ticks']/1000;data['owner_cpu_us_per_tick']=data['owner_cpu_ns']/data['measured_ticks']/1000;data['process_cpu_percent']=100*data['process_cpu_ns']/data['wall_ns']
 data.update({k:meta[k] for k in ('name','case','cores','variant','repeat','draw','verify','phase')})
 with (ROOT/'results.jsonl').open('a') as f:f.write(json.dumps(data)+'\n')
 print(f"{name}: {data['width']}x{data['height']}, units {data['initial_units']}->{data['final_units']}, {data['tps']:.1f} ticks/s, {data['cpu_us_per_tick']:.1f} CPU us/tick, RSS {data['rss_median_bytes']/2**20:.1f} MiB",flush=True)
 return data,out

def pair(case,cores,repeat,ticks,warmup,draw=1,verify=0,phase='measure'):
 result={};order=('baseline','final') if repeat%2==0 else ('final','baseline')
 for variant in order:result[variant]=run(case,cores,variant,repeat,ticks,warmup,draw,verify,phase)
 a,b=(result[v][0] for v in ('baseline','final'))
 for key in ('initial_checksum','final_checksum','initial_tick','measured_ticks','initial_units','final_units','initial_buildings','final_buildings'):
  assert a[key]==b[key],(case,cores,repeat,key,a[key],b[key])
 if verify:
  traces=[next(result[v][1].rglob('checksums.txt')).read_bytes() for v in ('baseline','final')];assert traces[0]==traces[1],(case,'per-tick trace mismatch')
 return a,b

if __name__=="__main__":
 args=argparse.ArgumentParser();args.add_argument('phase',choices=['pilot','verify','measure','control','wide']);opt=args.parse_args()
 if opt.phase=='pilot':
  plans=[]
  for case in FIXTURES:
   for cores in (2,4):
    a,b=pair(case,cores,0,1000,256,phase='pilot')
    ticks=max(2000,min(30000,int(min(a['tps'],b['tps'])*10)//100*100))
    plans.append({'case':case,'cores':cores,'ticks':ticks,'warmup':500})
  for case in FIXTURES:
   common=min(p['ticks'] for p in plans if p['case']==case)
   for p in plans:
    if p['case']==case:p['ticks']=common
  (ROOT/'plan.json').write_text(json.dumps(plans,indent=2)+'\n')
 else:
  plans=json.loads((ROOT/'plan.json').read_text())
  if opt.phase=='verify':
   for p in plans:pair(p['case'],p['cores'],0,p['ticks'],p['warmup'],verify=1,phase='verify')
  if opt.phase=='measure':
   for repeat in range(6):
    for p in (plans if repeat%2==0 else list(reversed(plans))):pair(p['case'],p['cores'],repeat,p['ticks'],p['warmup'])
  if opt.phase=='control':
   for repeat in range(4):
    for p in plans:
     if p['cores']==4:pair(p['case'],4,repeat,p['ticks'],p['warmup'],draw=0,phase='control')

 if opt.phase=='wide':
  wide=[p for p in plans if p['cores']==4]
  for p in wide:pair(p['case'],8,0,p['ticks'],p['warmup'],verify=1,phase='verify')
  for repeat in range(6):
   for p in (wide if repeat%2==0 else list(reversed(wide))):pair(p['case'],8,repeat,p['ticks'],p['warmup'])
