from pathlib import Path
import json,os,subprocess,hashlib,time,signal,re,shlex,datetime,fcntl,traceback
O=Path(__file__).resolve().parent;R=Path('/home/bradley/.codex/worktrees/engine-optimization/glob2')
def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def save(n,v):
 p=O/n;p.parent.mkdir(parents=True,exist_ok=True);tmp=p.with_suffix(p.suffix+'.tmp');tmp.write_text(json.dumps(v,indent=2)+'\n');tmp.replace(p)
def now():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def census():
 found=[]
 for p in Path('/proc').glob('[0-9]*'):
  try:
   comm=(p/'comm').read_text().strip();stat=(p/'stat').read_text().split(') ',1)[1].split();args=(p/'cmdline').read_bytes().replace(b'\0',b' ').decode(errors='replace')
  except OSError:continue
  if stat[0]!='Z' and comm.startswith(('cc1','lto1','clang','g++','gcc','c++','glob2')):found.append({'pid':int(p.name),'comm':comm,'starttime':stat[19],'command':args})
 return found

def parse(text,seed,cap,rc):
 lines=re.findall(r'^GLOB2_GAME_END (.*)$',text,re.M)
 if cap:return {'status':'cap','ticks':None,'winner':None,'parsed':None}
 assert rc==0 and len(lines)==1,('Bad exit/summary',rc,len(lines))
 fields=dict(x.split('=',1) for x in shlex.split(lines[0]));assert int(fields['seed'])==seed and fields['map']=='Playground'
 players=fields['players'].split(',');ai=[x for x in players if x.endswith(':Nicowar')];local=[x for x in players if x.endswith(':local')]
 assert len(players)==9 and len(ai)==8 and len(local)==1 and sorted(int(x.split(':')[0][4:]) for x in ai)==list(range(8)),players
 roster=re.findall(r'Player: (Nicowar[^\n]*) for team (\d+)',text);assert len(roster)==8 and sorted(int(x[1]) for x in roster)==list(range(8)),roster
 tick=int(fields['ticks']);win=int(fields['winner_team']);assert tick>0 and 0<=win<8,fields
 human=re.findall(r'automaticEndingGame ended: (\d+) ticks',text);assert human==[str(tick)]
 return {'status':'winner','ticks':tick,'winner':win,'parsed':fields,'roster_valid':True}

def environment(profile,seed=None):
 e={k:v for k,v in os.environ.items() if not k.startswith('GLOB2_')};e.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',GLOB2_USER_DIR=str(profile),GLOB2_USER_DATA_DIR=str(profile),GLOB2_ASSET_DIR=str(R))
 if seed is not None:e.update(GLOB2_TEST_SEED=str(seed),GLOB2_TEST_MAX_TICKS='2147483647')
 return e

def preflight(binary):
 q=O/'qualification';q.mkdir();env=environment(q/'profile');commands=[]
 tests=[('golden',[str(binary),'match','verify','test/fixtures/multiplayer/FourSquares1.g2mr','--map-file','maps/FourSquares1.map.gz','--output-dir',str(q/'golden'),'--compute-threads','4']),('auto',[str(binary),'game','run','--map-file',str(R/'maps/Playground.map'),'--game-seed','1056243202','--ticks','64','--compute-threads','auto','--gradient-backend','cpu',*sum((['--player','nicowar'] for _ in range(8)),[]),'--output-dir',str(q/'auto')])]
 for name,cmd in tests:
  with (q/(name+'.log')).open('w') as f:r=subprocess.run(cmd,cwd=R,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=120)
  assert r.returncode==0,(name,r.returncode);commands.append({'name':name,'command':cmd,'env':{k:v for k,v in env.items() if k.startswith(('SDL_','GLOB2_'))},'returncode':r.returncode})
 assert (q/'golden/checksums.txt').read_bytes()==(R/'test/fixtures/multiplayer/FourSquares1.verify-trace.txt').read_bytes()
 n=json.loads((q/'auto/result.json').read_text());assert n['status']=='completed' and n['ticks']==64 and n['compute_threads']==32 and n['gradient_backend']=='cpu'
 save('qualification.json',{'state':'passed','commands':commands,'auto_participants':n['compute_threads'],'golden_trace_sha256':sha(q/'golden/checksums.txt'),'native_result_sha256':sha(q/'auto/result.json'),'utc':now()})

def main():
 lock=(O/'run.lock').open('a+');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
 p=json.loads((O/'request.json').read_text());b=json.loads((O/'build-complete.json').read_text());binary=Path(b['binary']);assert sha(binary)==b['binary_sha256'];assert sha(p['map_file'])==p['map_sha256'];assert sorted(os.sched_getaffinity(0))==p['CPU_affinity']
 assert not(O/'qualification.json').exists(),'Do not duplicate run controller'
 deadline=time.monotonic()+1200
 while True:
  found=census()
  if not found:break
  assert time.monotonic()<deadline,('Competing native/compiler work',found);save('progress.json',{'state':'waiting_for_clean_host','competing':found,'utc':now()});time.sleep(5)
 preflight(binary);time.sleep(2);assert not census()
 receipts=[];tournament=time.monotonic();save('launch.json',{'pid':os.getpid(),'starttime':Path('/proc/self/stat').read_text().split(') ',1)[1].split()[19],'binary_sha256':b['binary_sha256'],'script_sha256':sha(__file__),'request_sha256':sha(O/'request.json'),'utc':now()})
 for i,seed in enumerate(p['seeds']):
  assert not census(),('Native/compiler began before measured game',census());assert sha(binary)==b['binary_sha256'] and sha(p['map_file'])==p['map_sha256']
  q=O/'runs'/str(seed);q.mkdir(parents=True);profile=q/'profile';profile.mkdir();env=environment(profile,seed)
  cmd=['taskset','-c',','.join(map(str,p['CPU_affinity'])),str(binary),'dev','random-games','--runs','1','--ticks','2147483647','--map','Playground','--matchup',','.join(['nicowar']*8),'--compute-threads','auto','--gradient-backend','cpu']
  capped=False;sample=None;started=now();background_load=os.getloadavg();child=None;reaped=False
  save('progress.json',{'state':'running','completed':len(receipts),'total':32,'active_seed':seed,'seed_index':i,'utc':started})
  try:
   with (q/'stdout.log').open('w') as f:
    t=time.monotonic();child=subprocess.Popen(cmd,cwd=R,env=env,stdout=f,stderr=subprocess.STDOUT,start_new_session=True)
    while True:
     pid,status,usage=os.wait4(child.pid,os.WNOHANG)
     if pid:child.returncode=os.waitstatus_to_exitcode(status);reaped=True;break
     elapsed=time.monotonic()-t
     if sample is None and elapsed>=3:
      proc=Path('/proc')/str(child.pid)
      try:sample={'task_count':len(list((proc/'task').iterdir())),'status':(proc/'status').read_text(),'elapsed':elapsed}
      except OSError:sample={'sample_missed':True}
     if elapsed>=p['cap_seconds']:
      capped=True;os.killpg(child.pid,signal.SIGTERM);end=time.monotonic()+10
      while time.monotonic()<end:
       pid,status,usage=os.wait4(child.pid,os.WNOHANG)
       if pid:child.returncode=os.waitstatus_to_exitcode(status);reaped=True;break
       time.sleep(.1)
      if not reaped:os.killpg(child.pid,signal.SIGKILL);_,status,usage=os.wait4(child.pid,0);child.returncode=os.waitstatus_to_exitcode(status);reaped=True
      break
     time.sleep(.05)
   wall=time.monotonic()-t;capped=capped or wall>=p['cap_seconds'];text=(q/'stdout.log').read_text(errors='replace');parsed=parse(text,seed,capped,child.returncode);cpu=usage.ru_utime+usage.ru_stime
   assert not list(profile.rglob('*.checksums')),'Unexpected checksum telemetry'
   result={**parsed,'seed':seed,'seed_index':i,'revision':b['revision'],'binary_sha256':b['binary_sha256'],'map_sha256':p['map_sha256'],'command':cmd,'cwd':str(R),'env':{k:v for k,v in env.items() if k.startswith(('GLOB2_','SDL_'))},'allocation':p['CPU_affinity'],'compute_requested':'auto','compute_participants_preflight':32,'started_utc':started,'finished_utc':now(),'started_monotonic':t,'finished_monotonic':t+wall,'raw_wait4_status':status,'raw_wait4_usage':{k:getattr(usage,k) for k in dir(usage) if k.startswith('ru_')},'e2e_seconds':wall,'user_cpu_seconds':usage.ru_utime,'system_cpu_seconds':usage.ru_stime,'total_cpu_seconds':cpu,'peak_rss_kib':usage.ru_maxrss,'voluntary_context_switches':usage.ru_nvcsw,'involuntary_context_switches':usage.ru_nivcsw,'returncode':child.returncode,'cap_seconds':p['cap_seconds'],'stdout_sha256':sha(q/'stdout.log'),'loadavg_start':background_load,'process_sample':sample,'telemetry':'off','inner_tps':None,'checksum_files':[]}
   result.update(e2e_tps=parsed['ticks']/wall if parsed['ticks'] else None,cpu_per_tick=cpu/parsed['ticks'] if parsed['ticks'] else None)
   save(Path('runs')/str(seed)/'receipt.json',result);receipts.append(result)
   save('progress.json',{'state':'running' if i<31 else 'timing_complete','completed':len(receipts),'total':32,'elapsed':time.monotonic()-tournament,'last':{'seed':seed,'e2e_seconds':wall,'cpu_seconds':cpu,'ticks':parsed['ticks']},'utc':now()})
  finally:
   if child is not None and not reaped:
    os.killpg(child.pid,signal.SIGKILL);_,status,usage=os.wait4(child.pid,0);child.returncode=os.waitstatus_to_exitcode(status)
    save(Path('runs')/str(seed)/'aborted.json',{'status':status,'usage':list(usage),'warning':'Infrastructure-aborted run, not a planned cap.'})
 assert sha(binary)==b['binary_sha256'] and not census();save('complete.json',{'state':'complete','runs':32,'winners':sum(r['status']=='winner' for r in receipts),'caps':sum(r['status']=='cap' for r in receipts),'total_elapsed':time.monotonic()-tournament,'receipts':{str(Path('runs')/str(r['seed'])/'receipt.json'):sha(O/'runs'/str(r['seed'])/'receipt.json') for r in receipts},'binary_sha256':b['binary_sha256'],'request_sha256':sha(O/'request.json'),'utc':now()})
if __name__=='__main__':
 try:main()
 except BaseException:save('failure.json',{'traceback':traceback.format_exc(),'utc':now()});raise
