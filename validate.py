from pathlib import Path
import concurrent.futures,hashlib,json,mmap,platform,subprocess,sys
R=Path.cwd();D=R/'artifacts/echo-save-continuity/validation-final';D.mkdir(parents=True,exist_ok=True)
I=Path('/home/bradley/glob2-maxima-final/inputs') if platform.system()=='Linux' else R/'artifacts/final-maxima/inputs'
B=R/('build/'+platform.system().lower()+'/client/release/src/glob2')
sys.path.insert(0,str(R/'test'));from compare_save_continuation import compare,records
old=Path('/home/bradley/glob2-maxima-final/artifacts/final-maxima') if platform.system()=='Linux' else R/'artifacts/final-maxima'
def run(case):
 name,args,stop,save=case;out=D/name;out.mkdir(exist_ok=True)
 cmd=[str(B),'--run-game',*args,'--ticks',str(stop),'--telemetry','checksums','--save','every:'+str(save),'--output-dir',str(out)]
 (out/'command.json').write_text(json.dumps(cmd,indent=2))
 with (out/'run.log').open('w') as f:subprocess.run(cmd,stdout=f,stderr=f,check=True)
 child=out/'resumed';cmd=[str(B),'--run-game','--load-game',str(out/f'checkpoint-{save}.game'),'--ticks',str(stop),'--telemetry','checksums','--output-dir',str(child)]
 (out/'resume-command.json').write_text(json.dumps(cmd,indent=2))
 with (out/'resume.log').open('w') as f:subprocess.run(cmd,stdout=f,stderr=f,check=True)
 result={'case':name}
 try:result['resumed_matching_ticks']=compare(out/'game.replay.checksums',child/'game.replay.checksums')
 except ValueError as e:result['resume_error']=str(e)
 if (old/name/'game.replay.checksums').exists():
  try:result['unchanged_original_ticks']=compare(old/name/'game.replay.checksums',out/'game.replay.checksums')
  except ValueError as e:result['original_error']=str(e)
 for mode,path in [('full',out),('resumed',child)]:
  with (path/'game.replay.checksums').open('rb') as f:
   result[mode+'_sha256']=hashlib.file_digest(f,'sha256').hexdigest();f.seek(0)
   with mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as data:hashes={str(t):hashlib.sha256(v).hexdigest() for t,v in records(data)}
  (path/'hashes.json').write_text(json.dumps(hashes,indent=2)+'\n');result[mode+'_ticks']=len(hashes)
 print(result,flush=True);return result
cases=[]
for name,g,a,b in [('g1',1,'maxima','nicowar'),('g52',52,'maxima','nicowar'),('nicowar-mirror',1,'nicowar','nicowar'),('echo-mixed',1,'econo','nicowar')]:
 cases.append((name,['--map-file',str(I/f'g{g}-s6101-r0.map'),'--game-seed','6101','--player',a,'--player',b],8192,4096))
cases += [('v118',['--load-game',str(old/'g1/checkpoint-4096.game')],8192,6144),('v117',['--load-game',str(I/'v117.game')],8192,7168),('v115',['--load-game',str(I/'old-v115.game')],30512,30256),('crowded',['--load-game',str(I/'crowded.game')],579424,577376)]
if len(sys.argv)>1:cases=[c for c in cases if c[0] in sys.argv[1:]]
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as ex:results=list(ex.map(run,cases))
(D/('results-'+','.join(sys.argv[1:])+'.json' if len(sys.argv)>1 else 'results.json')).write_text(json.dumps(results,indent=2)+'\n')
