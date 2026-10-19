#!/usr/bin/env python3
"""Sequential matched ordinary-release comparison, artifact-only and Linux-only."""
import pathlib,json,hashlib,sys,os,time,subprocess,argparse,statistics,signal,csv,collections,datetime
HERE=pathlib.Path(__file__).resolve().parent
REF='97bc11aeb0c93b38c22c0f3abbdec43e2379aad8';CAND='2d4dc8e9e1051a80b98fb9c73cf2e7e6610173ec'
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def read(p):return json.loads(pathlib.Path(p).read_text())
def write(p,v):
 p=pathlib.Path(p);p.parent.mkdir(parents=True,exist_ok=True);t=p.with_name(p.name+'.tmp');t.write_text(json.dumps(v,indent=2)+'\n');os.replace(t,p)
def environment():
 freq={}
 for p in pathlib.Path('/sys/devices/system/cpu').glob('cpu*/cpufreq/scaling_*'):
  if p.name in ('scaling_governor','scaling_cur_freq'):
   try:freq[str(p)]=p.read_text().strip()
   except OSError:pass
 return dict(utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),load=os.getloadavg(),affinity=sorted(os.sched_getaffinity(0)),frequency=freq,background=subprocess.check_output(['ps','-eo','pid,ppid,psr,pcpu,pmem,etime,args','--sort=-pcpu'],text=True).splitlines()[:40])
def execute(args,cwd,out,timeout=1800):
 out=pathlib.Path(out);out.mkdir(parents=True,exist_ok=True);identity=dict(command=args,cwd=str(cwd),timeout_seconds=timeout)
 if (out/'run-receipt.json').exists():
  v=read(out/'run-receipt.json');assert all(v[k]==identity[k] for k in identity),'Resume command changed';return v
 assert not (out/'RUNNING.json').exists(),'Unresolved earlier process; inspect before resume'
 write(out/'environment-before.json',environment());env={k:v for k,v in os.environ.items() if not k.startswith('GLOB2_')};env.update(HOME=str(out/'home'),XDG_CONFIG_HOME=str(out/'home/.config'));(out/'home').mkdir(exist_ok=True)
 start=time.monotonic();wall=time.time();killed=False;term=None;usage=None
 with (out/'stdout.log').open('wb') as stdout,(out/'stderr.log').open('wb') as stderr:
  process=subprocess.Popen(args,cwd=cwd,env=env,stdout=stdout,stderr=stderr,start_new_session=True);write(out/'RUNNING.json',dict(pid=process.pid,start_epoch=wall,**identity))
  while True:
   pid,status,usage=os.wait4(process.pid,os.WNOHANG)
   if pid:process.returncode=os.waitstatus_to_exitcode(status);break
   if time.monotonic()-start>=timeout and term is None:os.kill(process.pid,signal.SIGTERM);term=time.monotonic();killed=True
   elif term is not None and time.monotonic()-term>=10:os.kill(process.pid,signal.SIGKILL)
   time.sleep(.05)
 elapsed=time.monotonic()-start;write(out/'environment-after.json',environment());v=dict(**identity,started_epoch=wall,wall_seconds=elapsed,exit_code=process.returncode,user_seconds=usage.ru_utime,system_seconds=usage.ru_stime,total_cpu_seconds=usage.ru_utime+usage.ru_stime,peak_rss_bytes=usage.ru_maxrss*1024,censored=killed)
 if (out/'result.json').exists():v['result']=read(out/'result.json');v['result_sha256']=sha(out/'result.json')
 write(out/'run-receipt.json',v);(out/'RUNNING.json').unlink();return v
p=argparse.ArgumentParser();p.add_argument('action',choices=['prepare','run','aggregate']);p.add_argument('--qualification',required=True);p.add_argument('--output',required=True);a=p.parse_args();G=read(a.qualification);assert G['status']=='qualified' and G['approved_for_prospective'] is True;assert os.uname().sysname=='Linux';assert G['host']=='therig.local';assert G['receipts']
for r in G['receipts']:assert sha(r['path'])==r['sha256']
for arm,source in [('reference',REF),('candidate',CAND)]:
 info=G['arms'][arm];assert info['source_revision']==source;assert sha(pathlib.Path(info['runtime'])/'bin/glob2')==info['binary_sha256']
old=G['common_generator'];assert old['source_revision']=='f65c8d1698f6deefc4820209e69ad704f91d5776' and sha(pathlib.Path(old['runtime'])/'bin/glob2')==old['binary_sha256'];cpu=G['cpu'];assert cpu in os.sched_getaffinity(0);os.sched_setaffinity(0,{cpu})
O=pathlib.Path(a.output).resolve();O.mkdir(parents=True,exist_ok=True);plan=read(HERE/'plan.json');assert sha(HERE/'plan.json')==read(HERE/'plan-sha256.json')['sha256'];binding=dict(plan_sha256=sha(HERE/'plan.json'),qualification_sha256=sha(a.qualification),reference_source=REF,candidate_source=CAND,actual_arms=G['arms'],cpu=cpu,control_override='Original protocol named54cdc;97bc incorporates migration-only repair, explicit root-approved updated control before execution.')
if (O/'binding.json').exists():assert read(O/'binding.json')==binding
else:write(O/'binding.json',binding)
def base(spec,arm,out,load=None):
 runtime=pathlib.Path(G['arms'][arm]['runtime']);cmd=[str(runtime/'bin/glob2'),'game','run','--output-dir',str(out),'--compute-threads','1','--gpu','off','--compute-telemetry','off']
 if load:cmd+=['--load-game',str(load)]
 else:
  cmd+=['--game-seed',str(spec['game_seed']),'--map-file',str(O/'maps'/str(spec['map_seed'])/arm/'migrated.map')]
  for ai in spec['ais']:cmd+=['--player',ai]
  for alliance in spec['alliances']:cmd+=['--alliance',str(alliance)]
 return runtime,cmd
if a.action=='prepare':
 index=[]
 for s in plan['games']:
  d=O/'maps'/str(s['map_seed']);raw=d/'original';raw.mkdir(parents=True,exist_ok=True);mp=raw/'map.map.gz';runtime=pathlib.Path(old['runtime']);cmd=[str(runtime/'bin/glob2'),'map','generate',s['generator'],'--seed',str(s['map_seed']),'--width',str(s['width']),'--height',str(s['height']),'--teams',str(s['players']),'--output',str(mp),'--report-file',str(raw/'generation.json'),'--set','workers=4']
  if not mp.exists():
   receipt=execute(cmd,runtime,raw,timeout=300);assert receipt['exit_code']==0 and not receipt['censored']
  states={};arms={}
  for arm in ['reference','candidate']:
   dst=d/arm;runtime=pathlib.Path(G['arms'][arm]['runtime']);receipt=execute([str(runtime/'bin/glob2'),'game','migrate','--input',str(mp),'--legacy-adaptive-174','--output-dir',str(dst)],runtime,dst,timeout=300);assert receipt['exit_code']==0 and not receipt['censored'];r=receipt['result'];assert r['input_sha256']==sha(mp) and r['source_version']==174 and r['save_version']==176 and r['ticks_advanced']==0 and r['reload_state_equal'] and not r['is_saved_game'];assert sha(dst/'migrated.map')==r['output_sha256'];states[arm]=r['normalized_state'];arms[arm]=dict(migrated_sha256=r['output_sha256'],receipt_sha256=sha(dst/'run-receipt.json'))
  assert states['reference']==states['candidate'],'Different normalized common initial state';index.append(dict(spec=s,original_sha256=sha(mp),normalized_state_sha256=hashlib.sha256(json.dumps(states['reference']).encode()).hexdigest(),arms=arms))
 write(O/'maps-prepared.json',index)
elif a.action=='run':
 assert (O/'maps-prepared.json').exists();write(O/'status.json',dict(stage='running'))
 for s in plan['games']:
  for repeat,arm in enumerate(['reference','candidate','candidate','reference']):
   out=O/'timing'/str(s['map_seed'])/(str(repeat)+'-'+arm);runtime,cmd=base(s,arm,out);cmd+=['--ticks',str(s['timing_ticks']),'--benchmark-warmup',str(min(1024,s['timing_ticks']//4))];v=execute(cmd,runtime,out)
   assert v['censored'] or v['exit_code']==0,('Timing failure',str(out))
  for arm in ['reference','candidate']:
   warm=O/'diagnostic'/str(s['map_seed'])/arm/'warm';runtime,cmd=base(s,arm,warm);cmd+=['--ticks',str(s['diagnostic_warmup_ticks']),'--save','final'];v=execute(cmd,runtime,warm)
   if v['censored']:continue
   assert v['exit_code']==0
   saves=list(warm.glob('final.game*'));assert len(saves)==1
   if v['result']['ticks']<s['diagnostic_warmup_ticks']:write(warm/'early-end.json',dict(ticks=v['result']['ticks'],diagnostic_window_available=False));continue
   out=warm.parent/'window';runtime,cmd=base(s,arm,out,load=saves[0]);cmd+=['--ticks',str(s['diagnostic_warmup_ticks']+s['diagnostic_window_ticks']),'--resource-gradient-probe'];v=execute(cmd,runtime,out);assert v['censored'] or v['exit_code']==0
 write(O/'status.json',dict(stage='runs-terminal',no_automatic_promotion=True))
else:
 records=[];pairs=[];diagnostics=[]
 for s in plan['games']:
  runs=[]
  for repeat,arm in enumerate(['reference','candidate','candidate','reference']):
   file=O/'timing'/str(s['map_seed'])/(str(repeat)+'-'+arm)/'run-receipt.json'
   if not file.exists():continue
   v=read(file);r=v.get('result',{});ticks=r.get('ticks',0);measured=r.get('benchmark_measured_ticks',0);row=dict(map_seed=s['map_seed'],format=s['format'],arm=arm,repeat=repeat,censored=v['censored'],ticks=ticks,total_cpu_seconds=v['total_cpu_seconds'],wall_seconds=v['wall_seconds'],ticks_per_second=ticks/v['wall_seconds'] if ticks else None,cpu_ms_per_tick=v['total_cpu_seconds']*1000/ticks if ticks else None,benchmark_measured_ticks=measured,benchmark_cpu_ms_per_tick=r.get('benchmark_run_cpu_ns',0)/1e6/measured if measured else None,termination=r.get('termination'),receipt_sha256=sha(file));records.append(row);runs.append(row)
  if len(runs)==4:
   for x,y in [(0,1),(3,2)]:
    ref,cand=runs[x],runs[y]
    if not ref['censored'] and not cand['censored'] and ref['benchmark_cpu_ms_per_tick'] and cand['benchmark_cpu_ms_per_tick']:pairs.append(dict(map_seed=s['map_seed'],format=s['format'],reference_repeat=x,candidate_repeat=y,benchmark_cpu_per_tick_ratio=cand['benchmark_cpu_ms_per_tick']/ref['benchmark_cpu_ms_per_tick'],process_cpu_per_tick_ratio=cand['cpu_ms_per_tick']/ref['cpu_ms_per_tick'],same_ticks=ref['ticks']==cand['ticks']))
  for arm in ['reference','candidate']:
   directory=O/'diagnostic'/str(s['map_seed'])/arm/'window';detail=dict(map_seed=s['map_seed'],arm=arm,available=directory.exists(),tables={})
   for name in ['gradient-contradictions.csv','gradient-episodes.csv','gradient-quality.csv','gradient-planning-quality.csv']:
    file=directory/name
    if not file.exists():continue
    raw=file.read_bytes();complete=raw[:raw.rfind(b'\n')+1];reader=csv.DictReader(complete.decode().splitlines());rows=list(reader);assert all(None not in r and None not in r.values() for r in rows);rawrows=len(rows);rows=[r for r in rows if r.get('complete','1')=='1'];sums={}
    for key in reader.fieldnames or []:
     try:sums[key]=sum(float(r[key]) for r in rows)
     except ValueError:pass
    detail['tables'][name]=dict(rows=len(rows),raw_rows=rawrows,filter='Original complete==1 when column exists; otherwise all complete CSVrecords',numeric_sums=sums,raw_sha256=sha(file),discarded_unterminated_bytes=len(raw)-len(complete))
   totals=detail['tables'].get('gradient-contradictions.csv',{}).get('numeric_sums',{});q=totals.get('movement_queries',0);detail['live_rates']={key:totals[key]/q for key in ['reachable_no_move','sidestep_used'] if q and key in totals};diagnostics.append(detail)
 ratios=[r['benchmark_cpu_per_tick_ratio'] for r in pairs];write(O/'summary.json',dict(binding=binding,timing_runs=records,pairs=pairs,paired_benchmark_cpu_ratio=dict(count=len(ratios),median=statistics.median(ratios) if ratios else None,min=min(ratios) if ratios else None,max=max(ratios) if ratios else None),diagnostics=diagnostics,limitations=['Two paired repeats/map; no statistical confidence claim.','Diagnostic numeric sums are raw descriptive quantities, not interchangeable movement/planning labels or fullgame quality.','Differing tick counts/game endings and cappedruns are explicit; no fullgame CPU extrapolation.','Latewarm states are eacharm trajectories, never same-state counterfactual labels.'],no_automatic_promotion=True));print(json.dumps(dict(runs=len(records),pairs=len(pairs),ratios=ratios)))
