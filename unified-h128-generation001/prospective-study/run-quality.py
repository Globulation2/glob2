import datetime,fcntl,hashlib,json,os,pathlib,signal,subprocess,time
B=pathlib.Path(__file__).resolve().parent;P=json.loads((B/'PLAN.json').read_text());R=pathlib.Path(P['frozen_runtime']);O=B/'quality';O.mkdir(exist_ok=True)
lock=(O/'lock').open('w');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
def stamp():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def write(v):
 p=O/'STATUS.tmp';p.write_text(json.dumps(v,indent=2)+'\n');p.replace(O/'STATUS.json')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
assert sha(R/'bin/glob2')==P['frozen_binary_sha256']
state={'status':'running','started_utc':stamp(),'supervisor_pid':os.getpid(),'scope':'diagnostic quality only; all timings excluded from CPU comparison','plan_sha256':sha(B/'PLAN.json'),'rows':[]};write(state)
try:
 for mi,m in enumerate(P['maps']):
  mp=B/'maps'/m['group']/'map.map.gz';assert sha(mp)==m['sha256']
  for arm in P['orders'][mi]:
   out=O/(m['group']+'-'+arm);out.mkdir(exist_ok=False)
   args=[str(R/'bin/glob2'),'game','run','--output-dir',str(out),'--profile',str(out/'profile'),'--map-file',str(mp),'--game-seed',str(m['spec']['game_seed']),'--ticks',str(P['quality_ticks']),'--benchmark-warmup','0','--gpu','off','--compute-threads','1','--compute-telemetry','off','--data-dir',str(R/'data'),'--resource-gradient-probe','--gradient-spatial-overlap','on','--resource-gradient-exploration',{'adaptive':'0','fixed32':'32','fixed128':'128'}[arm],'--resource-gradient-exploration-seed','181101']
   for ai in m['spec']['ais']:args+=['--player',ai]
   for alliance in m['spec']['alliances']:args+=['--alliance',str(alliance)]
   (out/'command.json').write_text(json.dumps(args,indent=2))
   env=dict(os.environ,LD_LIBRARY_PATH=str(R/'lib/glob2'),SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
   t=time.monotonic()
   with (out/'stdout.log').open('w')as so,(out/'stderr.log').open('w')as se:
    p=subprocess.Popen(['taskset','-c','2','nice','-n','19',*args],cwd=R,env=env,stdout=so,stderr=se,start_new_session=True)
    state['active']={'pid':p.pid,'map_group':m['map_group'],'arm':arm,'started_utc':stamp()};write(state)
    capped=False
    try:rc=p.wait(timeout=P['timeout_seconds_per_process'])
    except subprocess.TimeoutExpired:
     capped=True;os.killpg(p.pid,signal.SIGKILL);rc=p.wait()
   row={'map_group':m['map_group'],'group':m['group'],'arm':arm,'returncode':rc,'capped':capped,'operational_wall_seconds':time.monotonic()-t,'output':str(out),'attempt':1,'result':json.loads((out/'result.json').read_text()) if(out/'result.json').exists()else None,'hashes':{q.name:sha(q)for q in out.glob('*.csv')}}
   state['rows'].append(row);state.pop('active',None);write(state);print(m['group'],arm,rc,flush=True)
 state['status']='complete';state['finished_utc']=stamp();write(state)
except BaseException as e:
 state['status']='failed';state['error']=repr(e);state['finished_utc']=stamp();write(state);raise
