"""Frozen five-way paired hospital policy ablation. Run in the diagnostic lab."""
import concurrent.futures,gzip,json,os,pathlib,shutil,subprocess,time
ROOT=pathlib.Path.cwd();OUT=ROOT/'artifacts/defense-study';BATCH=OUT/'hospital-ratio';BATCH.mkdir(parents=True,exist_ok=True)
variants=['control','beds30','beds40','beds50','beds60'];cases=[]
for large in [False,True]:
 for i in range(24):
  seed=(4001 if large else 3001)+i;gen=[15,2,48][i%3]
  mode='ffa' if large else ['nicowar','cabino','ffa'][(i//3+i%3)%3]
  players=['maxima','cabino','nicowar','nicowar'] if mode=='ffa' else ['maxima',mode]
  rotation=(i//3)%len(players);players=players[rotation:]+players[:rotation]
  cases.append(dict(id=f'g{gen}-s{seed}-{mode}',generator=gen,seed=seed,players=players,game_seed=12000+seed,ticks=60000,size=8 if large else 7,stratum='large' if large else 'small'))
design=dict(cases=cases,variants=variants,primary='paired actual engine win fraction; unresolved separately',secondary='pressure-sample operating bed deficit per warrior, combat worker deaths, hospital completions, resource expenditure proxy',inference='independent case blocks; paired t 95% intervals, exact discordant win tests; Holm correction across four control comparisons',stopping='all 240 games at fixed 60000 tick cap; no outcome-based stopping',selection='0.5 provisional default; assess all ratios, do not call a winner without uncertainty',control='adopted surplus towers with former hospital policy')
(BATCH/'design.json').write_text(json.dumps(design,indent=2))
def invoke(cmd,dest,control=False):
 dest.mkdir(parents=True,exist_ok=True);env=os.environ.copy()
 env['GLOB2_MAXIMA_BASE']=str(OUT/'hospital-control.strategy' if control else ROOT/'data/maxima/base.strategy')
 (dest/'command.json').write_text(json.dumps(dict(command=cmd,base=env['GLOB2_MAXIMA_BASE']),indent=2));start=time.time()
 if shutil.disk_usage(OUT).free<2*1024**3:raise RuntimeError('Disk admission limit')
 with gzip.open(dest/'stdout.log.gz','wb',compresslevel=1) as out:
  with subprocess.Popen(cmd,cwd=OUT/'hospital-control-root' if control else ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT) as p:shutil.copyfileobj(p.stdout,out);code=p.wait()
 (dest/'execution.json').write_text(json.dumps(dict(exit=code,seconds=time.time()-start)))
 for p in dest.glob('*.game'):
  with p.open('rb') as src,gzip.open(str(p)+'.gz','wb',compresslevel=1) as out:shutil.copyfileobj(src,out)
  p.unlink()
 if code:raise RuntimeError((dest,code))
def generate(s):
 dest=BATCH/'maps'/s['id']
 if (dest/'map-r0.map').exists():return
 invoke([str(OUT/'binaries/baseline'),'--generate-map','--generator',str(s['generator']),'--map-seed',str(s['seed']),'--param',f"teams={len(s['players'])}",'--param',f"width={s['size']}",'--param',f"height={s['size']}",'--write-map','true','--output-dir',str(dest)],dest,True)
def play(job):
 v,s=job;dest=BATCH/v/s['id']
 if (dest/'execution.json').exists() and (dest/'result.json').exists() and json.loads((dest/'execution.json').read_text())['exit']==0:return
 if dest.exists():
  archived=BATCH/'failed-attempts'/v/(s['id']+'-'+str(time.time_ns()));archived.parent.mkdir(parents=True,exist_ok=True);dest.rename(archived)
 binary='towers-lazy' if v=='control' else 'hospital-ratio'
 cmd=[str(OUT/'binaries'/binary),'--run-game','--map-file',str(BATCH/'maps'/s['id']/'map-r0.map'),'--game-seed',str(s['game_seed']),'--ticks',str(s['ticks']),'--save','initial','--save','final','--telemetry','team-timeline','--telemetry','maxima','--output-dir',str(dest)]
 for ai in s['players']:cmd+=['--player',ai]
 if v!='control':cmd+=['--ai-param',f"{s['players'].index('maxima')}:military.hospital_beds_per_warrior_percent={v[4:]}"]
 invoke(cmd,dest,v=='control');r=json.loads((dest/'result.json').read_text());print(v,s['id'],r['teams'][s['players'].index('maxima')]['outcome'],r['ticks'],flush=True)
def prepare_control_root():
 # The headless CLI uses the base file relative to its working directory.
 control=OUT/'hospital-control-root';(control/'data/maxima').mkdir(parents=True,exist_ok=True)
 for source in ROOT.iterdir():
  if source.name!='data' and not (control/source.name).exists():(control/source.name).symlink_to(source)
 for source in (ROOT/'data').iterdir():
  if source.name!='maxima' and not (control/'data'/source.name).exists():(control/'data'/source.name).symlink_to(source)
 for source in (ROOT/'data/maxima').iterdir():
  dest=control/'data/maxima'/source.name
  if not dest.exists():dest.symlink_to(OUT/'hospital-control.strategy' if source.name=='base.strategy' else source)
if __name__=='__main__':
 prepare_control_root()
 with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:list(pool.map(generate,cases))
 with concurrent.futures.ThreadPoolExecutor(max_workers=12) as pool:
  for f in concurrent.futures.as_completed([pool.submit(play,(v,s)) for s in cases for v in variants]):f.result()
