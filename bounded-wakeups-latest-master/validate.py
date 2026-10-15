from pathlib import Path
import sys,json,os,subprocess,hashlib,datetime,concurrent.futures,threading,fnmatch,xml.etree.ElementTree as ET,importlib.util,traceback
O=Path(__file__).resolve().parent;R=Path('/home/bradley/.codex/worktrees/engine-optimization/glob2');P=O.parent/'october-regression/coordination-redesign'
def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def now():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def write(p,v):
 p=Path(p);p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(v,indent=2)+'\n')
sys.path.insert(0,str(P))
spec=importlib.util.spec_from_file_location('sidecar_helpers',P/'parallel-parity.py');h=importlib.util.module_from_spec(spec);spec.loader.exec_module(h)
gpu=threading.Lock()
checks=[]
def run(cmd,out,extra=None):
 out.mkdir(parents=True,exist_ok=True)
 env={k:v for k,v in os.environ.items() if not k.startswith('GLOB2_')};env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',GLOB2_ASSET_DIR=str(R),GLOB2_USER_DATA_DIR=str(out/'profile'),GLOB2_USER_DIR=str(out/'profile'),GLOB2_OPENCL_DEVICE='0');env.update(extra or {})
 with (out/'stdout.log').open('w') as f: r=subprocess.run(cmd,cwd=R,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=1200)
 receipt={'command':cmd,'cwd':str(R),'env':{k:v for k,v in env.items() if k.startswith(('GLOB2_','SDL_'))},'rc':r.returncode,'stdout_sha256':sha(out/'stdout.log'),'utc':now()};write(out/'command.json',receipt);return r.returncode

def units(role):
 d=O/role
 (d/'test').mkdir(exist_ok=True)
 for n in ['glob2-unit-tests','glob2-engine-tests']:
  p=d/'test'/n
  if not p.exists():p.symlink_to(d/n)
 ff={'unit':['ComputeExecutor/*','ReplayStepCounter/*','HiringRanking/*','TurnHarness/two clients on clean links execute identical turns*','TurnHarness/a restarted client reloads, replays the full log and rejoins*'], 'engine':['AIPipeline/*','SharedWorkerLifecycle/*','SimulationReadPhase/*','TeamStatsPipeline/*','HiringPipeline/*','HiringPlanner/*','AIStateContinuation/*','LegacyAIState/*','BuildingGradientInvalidation/*']}
 res=[]
 for kind,filters in ff.items():
  out=d/('focused-'+kind);cmd=['python3','test/run_tests.py','--build-dir',str(d),'--binary',kind,'--no-display','--timeout','180','-j','4','--artifacts',str(out/'cases'),'--junit',str(out/'junit.xml')]
  for f in filters:cmd+=['--filter',f]
  rc=run(cmd,out);cases=list(ET.parse(out/'junit.xml').getroot().iter('testcase'));assert cases
  inventory={f:[c.get('classname','')+'/'+c.get('name','') for c in cases if fnmatch.fnmatchcase(c.get('classname','')+'/'+c.get('name',''),f)] for f in filters}
  bad=[c.get('classname','')+'/'+c.get('name','') for c in cases if any(c.find(x) is not None for x in ['failure','error','skipped'])]
  item={'role':role,'kind':kind,'rc':rc,'cases':len(cases),'bad':bad,'inventory':inventory};write(out/'summary.json',item);assert rc==0 and not bad and all(inventory.values()),item;res.append(item)
 # Assert the regression did run its partial-wakeup cases; no WARN fallback.
 out=d/'partial-verbose';rc=run([str(d/'glob2-unit-tests'),'--test-suite=ComputeExecutor','--test-case=idle workers retain parallelism*','--success=true','--no-colors=true'],out)
 text=(out/'stdout.log').read_text();expected=54 if role=='candidate' else 24;assert rc==0 and 'WARN' not in text and f'{expected} passed' in text,(role,text[-2000:]);res.append({'role':role,'kind':'partial-verbose','assertions':expected,'warnings':False})
 return res

def parity(case):
 for path,digest in case['fixture_sha256'].items():assert sha(path)==digest
 results=[]
 gate=gpu if 'opencl' in case['args'] else threading.Lock()
 with gate:
  for role in ['baseline','candidate']:
   out=O/role/'parity'/case['id'];cmd=[str(O/role/'glob2'),'game','run',*case['args'],'--write-replay','--telemetry','checksums','--output-dir',str(out/'native')]
   rc=run(cmd,out);assert rc==0,(role,case['id'],rc)
   n=json.loads((out/'native/result.json').read_text());assert n['status']=='completed' and n['ticks']==case['initial_tick']+case['measured_ticks'],n
   sides,details=h.sidecars(out/'native',case)
   item={'role':role,'case':case['id'],'initialChecksum':n['initialChecksum'],'finalChecksum':n['finalChecksum'],'checksums':sides,'details':details,'compute_threads':n['compute_threads'],'gradient_gpu_fields':n.get('gradient_gpu_fields'),'gradient_device':n.get('gradient_device'),'result_sha256':sha(out/'native/result.json')}
   if 'opencl' in case['args']:assert n.get('gradient_gpu_fields',0)>0 and n.get('gradient_device'),('No actual GPU work',item)
   if case['args'][case['args'].index('--compute-threads')+1]=='auto':assert item['compute_threads']==32,item
   write(out/'receipt.json',item);results.append(item)
 assert all(results[0][k]==results[1][k] for k in ['initialChecksum','finalChecksum','checksums']),case['id']
 return {'case':case['id'],'matched':True,'rows':case['measured_ticks'],'results':results}

def main():
 builds=json.loads((O/'builds-complete.json').read_text());assert builds['state']=='passed'
 for v in builds['results']:
  for b in v['binaries'].values():assert sha(b['path'])==b['sha256']
 suite=[]
 for role in ['baseline','candidate']:suite.extend(units(role));write(O/'focused-progress.json',suite)
 cases=json.loads((P/'correctness-cases.json').read_text())['cases'];write(O/'cases.json',cases)
 traces=[]
 with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
  for r in pool.map(parity,cases):traces.append(r);write(O/'parity-progress.json',{'completed':len(traces),'total':len(cases),'pairs':traces})
 golden=[]
 for role in ['baseline','candidate']:
  out=O/role/'golden';cmd=[str(O/role/'glob2'),'match','verify','test/fixtures/multiplayer/FourSquares1.g2mr','--map-file','maps/FourSquares1.map.gz','--output-dir',str(out),'--compute-threads','4'];assert run(cmd,out)==0
  trace=out/'checksums.txt';assert trace.read_bytes()==(R/'test/fixtures/multiplayer/FourSquares1.verify-trace.txt').read_bytes();golden.append({'role':role,'sha256':sha(trace),'rows':sum(1 for l in trace.read_text().splitlines() if not l.startswith('#'))})
 for v in builds['results']:
  for b in v['binaries'].values():assert sha(b['path'])==b['sha256']
 write(O/'validation-complete.json',{'state':'passed','utc':now(),'builds':builds,'focused':suite,'parity':traces,'golden':golden,'limits':['Linux x86-64 and one NVIDIA OpenCL device only; fresh browser/mobile/Windows/macOS runtime not exercised','No new wall-time benchmark; prior timing remains historical on its recorded engine base']})
try:main()
except BaseException:write(O/'validation-failure.json',{'utc':now(),'traceback':traceback.format_exc()});raise
