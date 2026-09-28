from pathlib import Path
import concurrent.futures,gzip,hashlib,json,mmap,platform,subprocess,sys
R=Path.cwd();D=R/'artifacts/final-maxima';D.mkdir(parents=True,exist_ok=True)
I=D/'inputs' if (D/'inputs').exists() else R/'inputs'
B=R/('build/'+platform.system().lower()+'/client/release/src/glob2')
sys.path.insert(0,str(R/'test'))
from compare_save_continuation import records,compare
fixture=I/'old-v115.game';fixture.write_bytes(gzip.decompress((R/'test/maxima/fixtures/save-continuation/checkpoint-30000-v115.game.gz').read_bytes()))
def run(case):
 name,args,stop,save=case;out=D/name;out.mkdir(exist_ok=True)
 cmd=[str(B),'--run-game',*args,'--ticks',str(stop),'--telemetry','checksums','--output-dir',str(out)]
 if save:cmd+=['--save','every:'+str(save)]
 (out/'command.json').write_text(json.dumps(cmd,indent=2))
 with (out/'run.log').open('w') as f:subprocess.run(cmd,stdout=f,stderr=f,check=True)
 p=out/'game.replay.checksums'
 with p.open('rb') as f:
  with mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as data:
   hashes={str(t):hashlib.sha256(v).hexdigest() for t,v in records(data)}
 (out/'hashes.json').write_text(json.dumps(hashes,indent=2)+'\n')
 result={'case':name,'ticks':len(hashes),'state_digest':hashlib.sha256(json.dumps(hashes,sort_keys=True).encode()).hexdigest()}
 if save:
  child=out/'resumed';cmd=[str(B),'--run-game','--load-game',str(out/f'checkpoint-{save}.game'),'--ticks',str(stop),'--telemetry','checksums','--output-dir',str(child)]
  (out/'resume-command.json').write_text(json.dumps(cmd,indent=2))
  with (out/'resume.log').open('w') as f:subprocess.run(cmd,stdout=f,stderr=f,check=True)
  try: result['resumed_matching_ticks']=compare(p,child/'game.replay.checksums')
  except ValueError as error: result['resume_error']=str(error)
 print(result,flush=True);return result
cases=[('g'+str(g),['--map-file',str(I/f'g{g}-s6101-r0.map'),'--game-seed','6101','--player','maxima','--player','nicowar'],8192,4096) for g in (1,52)]
cases += [('v117',['--load-game',str(I/'v117.game')],8192,7168),('crowded',['--load-game',str(I/'crowded.game')],579424,577376),('v115',['--load-game',str(fixture)],30512,30256)]
if len(sys.argv)>1:cases=[c for c in cases if c[0] in sys.argv[1:]]
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:results=list(pool.map(run,cases))
(D/'validation.json').write_text(json.dumps(results,indent=2)+'\n')
