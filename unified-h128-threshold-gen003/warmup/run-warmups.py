#!/usr/bin/env python3
"""Single-worker frozen heldout warmups. No training ingestion or adaptive sweep."""
import datetime,fcntl,hashlib,json,os,pathlib,signal,subprocess,time,traceback
D=pathlib.Path(__file__).resolve().parent;P=json.loads((D/'WARMUP-PROPOSAL.json').read_text());R=pathlib.Path('/home/bradley/glob2/artifacts/gradient-unified-20261019/round-003/candidate-gen003/runtime-linux-frozen')
L=(D/'warmup.lock').open('a');fcntl.flock(L,fcntl.LOCK_EX|fcntl.LOCK_NB)
def stamp():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def atomic(p,v):
 t=p.with_suffix(p.suffix+'.tmp');t.write_text(json.dumps(v,sort_keys=True)+'\n');t.replace(p)
state=dict(status='STARTING',pid=os.getpid(),utc=stamp(),plan_sha256=sha(D/'WARMUP-PROPOSAL.json'),source_revision=P['source_revision'],model_sha256=P['model_sha256'],rows=[],no_training_ingestion=True)
def save(**kw):state.update(kw,updated_utc=stamp());atomic(D/'STATUS.json',state)
def run(out,args,cap):
 out.mkdir(exist_ok=False);command=['taskset','-c','2','nice','-n','19',str(R/'bin/glob2'),'game','run','--output-dir',str(out),'--profile',str(out/'profile'),'--benchmark-warmup','0','--gpu','off','--compute-threads','1','--compute-telemetry','off','--data-dir',str(R/'data'),*args]
 env={k:v for k,v in os.environ.items() if not k.startswith('GLOB2_')};env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',LD_LIBRARY_PATH=str(R/'lib/glob2'))
 atomic(out/'COMMAND.json',dict(argv=command,cwd=str(R),environment={k:env[k] for k in ['SDL_VIDEODRIVER','SDL_AUDIODRIVER','LD_LIBRARY_PATH']},wall_cap_seconds=cap))
 start=time.monotonic();capped=False
 with (out/'stdout.log').open('w') as so,(out/'stderr.log').open('w') as se:
  p=subprocess.Popen(command,cwd=R,env=env,stdout=so,stderr=se,start_new_session=True);save(active=dict(pid=p.pid,name=out.name,started_utc=stamp(),wall_cap_seconds=cap))
  while True:
   done,status,usage=os.wait4(p.pid,os.WNOHANG)
   if done:break
   if time.monotonic()-start>=cap:
    capped=True;os.killpg(p.pid,signal.SIGKILL);done,status,usage=os.wait4(p.pid,0);break
   time.sleep(.2)
  p.returncode=os.waitstatus_to_exitcode(status)
 result=json.loads((out/'result.json').read_text()) if (out/'result.json').exists() else None
 row=dict(returncode=p.returncode,capped=capped,wall_seconds=time.monotonic()-start,process_cpu_seconds=usage.ru_utime+usage.ru_stime,result=result,output=str(out));atomic(out/'PROCESS.json',row);state.pop('active',None);return row
try:
 expected='5f6616edf50ac7ccfd3878edd70cd4b85719731a8a73453d716ab7d53303b848'
 assert sha(R/'bin/glob2')==expected
 save(status='RUNNING',binary_sha256=expected,threshold_milli=128000,probe=False,cpu=2,nice=19,workers=1,ticks_semantics='ABSOLUTE terminal stepCounter; benchmark_measured_ticks=final-start-warmup; arm target must be saved tick+requested additional ticks')
 for m in P['maps']:
  g=m['group'];receipt=D/(g+'-CHECKPOINT.json')
  if receipt.exists():state['rows'].append(json.loads(receipt.read_text()));continue
  if (D/(g+'-INTENT.json')).exists():raise RuntimeError('Unresolved prior warmup intent; never retry automatically: '+g)
  path=D/'maps'/g/'map.map.gz';assert sha(path)==m['sha256']
  atomic(D/(g+'-INTENT.json'),dict(utc=stamp(),group=g,map_group=m['map_group'],map_sha256=m['sha256'],planned_ticks=16384,attempt=1))
  args=['--map-file',str(path),'--game-seed',str(m['spec']['game_seed']),'--ticks','16384','--save','final']
  for ai in m['spec']['ais']:args+=['--player',ai]
  for alliance in m['spec']['alliances']:args+=['--alliance',str(alliance)]
  row=run(D/'runs'/g,args,1800);result=row['result'] or {};saved=D/'runs'/g/'final.game.gz';teams=result.get('teams',[])
  row.update(group=g,map_group=m['map_group'],format=m['format'],map_sha256=m['sha256'],game_seed=m['spec']['game_seed'],source_revision=P['source_revision'],model_sha256=P['model_sha256'],binary_sha256=expected,baseline_threshold_milli=128000,saved_tick=result.get('ticks'),save_minor=result.get('resolved',{}).get('save_version'),save_sha256=sha(saved) if saved.exists() else None,save_path=str(saved),active_teams=sum(t.get('alive',False) for t in teams),unit_count=sum(t.get('units',0) for t in teams),team_counts=[{k:t.get(k) for k in ['team','alliance','alive','units','workers','explorers','warriors','buildings']} for t in teams])
  usable=row['returncode']==0 and not row['capped'] and row['saved_tick']==16384 and result.get('termination')=='tick_cap' and row['active_teams']>=2 and saved.exists()
  if usable:
   assert row['save_minor']==178
   checked=run(D/'load-checks'/g,['--load-game',str(saved),'--ticks','16385'],1800)
   cr=checked['result'] or {};row['load_check']=dict(returncode=checked['returncode'],ticks=cr.get('ticks'),measured_ticks=cr.get('benchmark_measured_ticks'),initialChecksum=cr.get('initialChecksum'),save_minor=cr.get('resolved',{}).get('save_version'))
   row['loaded_tick']=cr.get('ticks',0)-cr.get('benchmark_measured_ticks',0)
   usable=checked['returncode']==0 and cr.get('ticks')==16385 and cr.get('benchmark_measured_ticks')==1 and row['loaded_tick']==16384 and row['load_check']['save_minor']==178
  row['usable_shared_checkpoint']=usable;row['unusable_policy']='Preserved; no replacement/retry or outcome-based selection'
  atomic(receipt,row);state['rows'].append(row);save(status='RUNNING',completed=len(state['rows']))
 save(status='COMPLETE',completed=4,usable=sum(r['usable_shared_checkpoint'] for r in state['rows']),finished_utc=stamp())
except Exception:
 save(status='FAILED-REQUIRES-REVIEW',error=traceback.format_exc());raise
