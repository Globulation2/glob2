import datetime,fcntl,hashlib,json,os,pathlib,signal,subprocess,time,statistics,csv,traceback
B=pathlib.Path(__file__).resolve().parent;P=json.loads((B/'PLAN.json').read_text());R=pathlib.Path(P['frozen_runtime']);O=B/'timing';O.mkdir(exist_ok=True)
lock=(O/'lock').open('w');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
def stamp():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def save():
 p=O/'STATUS.tmp';p.write_text(json.dumps(state,indent=2)+'\n');p.replace(O/'STATUS.json')
state={'status':'waiting_for_isolated_build','supervisor_pid':os.getpid(),'started_utc':stamp(),'plan_sha256':sha(B/'PLAN.json'),'rows':[]};save()

def run(name,m,binary,arm,probe,ticks,proof=False):
 out=(B/'equivalence'if proof else O)/name;out.mkdir(parents=True,exist_ok=False)
 args=[str(binary),'game','run','--output-dir',str(out),'--profile',str(out/'profile'),'--map-file',str(B/'maps'/m['group']/'map.map.gz'),'--game-seed',str(m['spec']['game_seed']),'--ticks',str(ticks),'--benchmark-warmup','0','--gpu','off','--compute-threads','1','--compute-telemetry','off','--data-dir',str(R/'data')]
 if arm is not None:args+=['--resource-gradient-exploration',{'adaptive':'0','fixed32':'32','fixed128':'128'}[arm],'--resource-gradient-exploration-seed','181101']
 if probe:args+=['--resource-gradient-probe','--gradient-spatial-overlap','on']
 if proof:args+=['--telemetry','checksums','--save','final']
 for ai in m['spec']['ais']:args+=['--player',ai]
 for alliance in m['spec']['alliances']:args+=['--alliance',str(alliance)]
 env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',LD_LIBRARY_PATH=str(R/'lib/glob2'))
 (out/'command.json').write_text(json.dumps({'argv':['taskset','-c','2','nice','-n','19',*args],'cwd':str(R),'environment':{k:env[k]for k in ['SDL_VIDEODRIVER','SDL_AUDIODRIVER','LD_LIBRARY_PATH']}},indent=2))
 start=time.monotonic();capped=False
 with(out/'stdout.log').open('w')as so,(out/'stderr.log').open('w')as se:
  proc=subprocess.Popen(['taskset','-c','2','nice','-n','19',*args],cwd=R,env=env,stdout=so,stderr=se,start_new_session=True)
  state['active']={'name':name,'pid':proc.pid,'started_utc':stamp(),'proof':proof};save()
  while True:
   pid,status,usage=os.wait4(proc.pid,os.WNOHANG)
   if pid:break
   if time.monotonic()-start>=P['timeout_seconds_per_process']:
    capped=True;os.killpg(proc.pid,signal.SIGKILL);pid,status,usage=os.wait4(proc.pid,0);break
   time.sleep(.1)
  rc=os.waitstatus_to_exitcode(status);proc.returncode=rc
 elapsed=time.monotonic()-start;result=json.loads((out/'result.json').read_text())if(out/'result.json').exists()else None
 ticks_done=result.get('ticks')if result else None
 progress=None
 if(out/'progress.jsonl').exists():
  for line in (out/'progress.jsonl').read_text().splitlines():
   try:progress=json.loads(line)
   except ValueError:pass
 row={'name':name,'map_group':m['map_group'],'group':m['group'],'arm':arm or 'adaptive-original-default','probe':probe,'proof':proof,'attempt':1,'returncode':rc,'capped':capped,'wall_seconds':elapsed,'process_user_seconds':usage.ru_utime,'process_system_seconds':usage.ru_stime,'process_total_cpu_seconds':usage.ru_utime+usage.ru_stime,'maximum_rss_kib':usage.ru_maxrss,'ticks':ticks_done,'last_progress':progress,'result':result,'output':str(out),'artifact_hashes':{f:sha(out/f)for f in ['world.checksums','snapshot.checksums','game.replay.checksums','final.game.gz']if(out/f).exists()}}
 if ticks_done:
  row['process_cpu_ms_per_tick']=1000*row['process_total_cpu_seconds']/ticks_done
  row['wall_tps']=ticks_done/elapsed
  if result.get('benchmark_measured_ticks'):row['benchmark_run_cpu_ms_per_tick']=result['benchmark_run_cpu_ns']/1e6/result['benchmark_measured_ticks']
 (out/'MEASUREMENT.json').write_text(json.dumps(row,indent=2)+'\n');state.pop('active',None);return row
try:
 deadline=time.monotonic()+1800
 while True:
  receipt=B/'perf-build/STATUS.json'
  if receipt.exists():
   build=json.loads(receipt.read_text())
   if build['status']=='failed':raise RuntimeError(build)
   if build['status']=='built':break
  if time.monotonic()>deadline:raise RuntimeError('isolated build not ready in1800seconds; no games started')
  time.sleep(2)
 binary=pathlib.Path(build['binary']);assert sha(binary)==build['binary_sha256'];assert sha(R/'bin/glob2')==P['frozen_binary_sha256']
 state.update(status='equivalence',perf_binary_sha256=build['binary_sha256'],patch_sha256=build['patch_sha256']);save()
 m=P['maps'][0];proofs=[]
 pairs=[('adaptive',run('original-adaptive-off',m,R/'bin/glob2',None,False,1024,True),run('patched-adaptive-off',m,binary,'adaptive',False,1024,True))]
 for arm in ['fixed32','fixed128']:pairs.append((arm,run('frozen-'+arm+'-on',m,R/'bin/glob2',arm,True,1024,True),run('patched-'+arm+'-off',m,binary,arm,False,1024,True)))
 for arm,a,b in pairs:
  assert a['returncode']==b['returncode']==0 and not a['capped']and not b['capped'],(arm,a,b)
  assert len(a['artifact_hashes'])==4 and a['artifact_hashes']==b['artifact_hashes'],(arm,a['artifact_hashes'],b['artifact_hashes'])
  proofs.append({'arm':arm,'left':a,'right':b,'exact_four_artifacts':True})
 (B/'EQUIVALENCE-PASSED.json').write_text(json.dumps({'source':P['source_revision'],'perf_binary_sha256':build['binary_sha256'],'scope':'same initial heldout map1024ticks; exact per-tick world/snapshot/replay checksums and final save; adaptive original default versus patched explicit0, frozen fixed probeON versus patched fixed probeOFF','pairs':proofs},indent=2)+'\n')
 state.update(status='timing',equivalence_passed=True);save()
 for rep in range(P['timing_repeats']):
  for mi,m in enumerate(P['maps']):
   order=P['orders'][mi]if rep==0 else list(reversed(P['orders'][mi]))
   for arm in order:
    row=run('repeat'+str(rep)+'-'+m['group']+'-'+arm,m,binary,arm,False,P['timing_ticks']);state['rows'].append(row);save();print(row['name'],row['returncode'],row['process_total_cpu_seconds'],flush=True)
 summary={'status':'complete','finished_utc':stamp(),'scope':'short4096tick3heldout-map2counterbalanced-repeat prospective comparison; same isolated performance-only binary allarms, quality probeOFF; includes same explicit behavior decision logging allarms','limits':P['limits'],'perf_binary_sha256':build['binary_sha256'],'patch_sha256':build['patch_sha256'],'total_attempts':len(state['rows']),'total_capped':sum(r['capped']for r in state['rows']),'total_failed':sum(r['returncode']!=0 and not r['capped']for r in state['rows']),'all_attempt_cpu_seconds':sum(r['process_total_cpu_seconds']for r in state['rows']),'all_attempt_wall_seconds':sum(r['wall_seconds']for r in state['rows']),'rows':state['rows'],'by_arm':{}}
 for arm in P['arms']:
  rows=[r for r in state['rows']if r['arm']==arm];completed=[r for r in rows if r['returncode']==0 and not r['capped']and r['ticks']==P['timing_ticks']]
  summary['by_arm'][arm]={'attempts':len(rows),'fully_finished':len(completed),'capped':sum(r['capped']for r in rows),'all_attempt_cpu_seconds':sum(r['process_total_cpu_seconds']for r in rows),'all_attempt_wall_seconds':sum(r['wall_seconds']for r in rows),'measured_ticks_in_results':sum(r['ticks']or 0 for r in rows),'median_complete_process_cpu_ms_per_tick':statistics.median(r['process_cpu_ms_per_tick']for r in completed)if completed else None,'median_complete_benchmark_run_cpu_ms_per_tick':statistics.median(r['benchmark_run_cpu_ms_per_tick']for r in completed)if completed else None}
 (B/'TIMING-SUMMARY.json').write_text(json.dumps(summary,indent=2)+'\n');state.update(status='complete',finished_utc=stamp());save()
except BaseException as e:
 state.update(status='failed',error=repr(e),traceback=traceback.format_exc(),finished_utc=stamp());save();raise
