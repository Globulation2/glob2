import sys,os,json,subprocess,concurrent.futures
sys.path.insert(0,'/tmp/gauntlet-evidence/centroid-review-v9')
from study import inspect
ROOT='/tmp/gauntlet-evidence/centroid-extremes';BIN='/tmp/gauntlet-evidence/glob2-v9'
def run(spec):
 size,n,court,gate,level,seed=spec;path=f'{ROOT}/w{size}-n{n}-c{court}-g{gate}-l{level}-s{seed}';os.makedirs(path,exist_ok=True)
 cmd=[BIN,'--generate-map','--generator','58','--map-seed',str(seed),'--param',f'teams={n}','--param',f'width={size}','--param',f'height={size}','--param',f'starting-towers={level}','--param',f'court-size={court}','--param',f'gate-width={gate}','--param','partition-wall=3','--report','terrain','--output-dir',path]
 p=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True);r={'size':2**size,'n':n,'court':court,'gate':gate,'level':level,'seed':seed,'exit':p.returncode}
 if p.returncode:r['error']=p.stdout[-2000:]
 else:
  try:r['homes']=inspect(path)
  except Exception as e:r['error']=repr(e)
 return r
if __name__=='__main__':
 specs=[(sz,n,c,g,l,s) for sz,n in [(8,2),(8,4),(8,8),(9,2),(9,12)] for c in [80,120] for g in [5,9] for l in [1,3] for s in [1,2]]
 with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool,open(ROOT+'/results.jsonl','w') as f:
  for r in pool.map(run,specs):f.write(json.dumps(r)+'\n');f.flush();print(r['size'],r['n'],r['court'],r['gate'],r['level'],r['seed'],r.get('error','ok'),flush=True)
