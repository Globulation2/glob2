from pathlib import Path
import subprocess,json,os,time,sys,hashlib
r=Path.cwd();b=r/'artifacts/resource-growth/cleanup';env=dict(os.environ,LD_LIBRARY_PATH='/tmp/glob2-sdl3/prefix/lib')
# The settled build must finish before any executable is used.
while True:
 lines=(b/'build-final.log').read_text().splitlines()
 if lines and lines[-1]=='scons: done building targets.':break
 if any('scons: building terminated because of errors.' in l.lower() for l in lines):raise RuntimeError('Build failed')
 time.sleep(10)
results=[]
def run(name,cmd):
 (b/(name+'-command.json')).write_text(json.dumps({'cwd':str(r),'command':cmd,'environment':{'LD_LIBRARY_PATH':env['LD_LIBRARY_PATH']}},indent=2)+'\n')
 with (b/(name+'.log')).open('w') as f:status=subprocess.run(cmd,cwd=r,env=env,stdout=f,stderr=subprocess.STDOUT).returncode
 results.append({'name':name,'exit_code':status});(b/'validation.json').write_text(json.dumps(results,indent=2)+'\n');print(name,status,flush=True)
 return status
for name,binary,filters in [('engine','engine',['--coverage-profile','compatibility','--exclude-tag','golden']),('golden','engine',['--tag','golden']),('unit','unit',['--coverage-profile','compatibility'])]:
 run(name,['python3','test/run_tests.py','--binary',binary,'--no-display','-j','4','--artifacts',str(b/name),'--junit',str(b/(name+'.xml')),*filters])
exe=r/'build/linux/client/release/src/glob2'
for name,extra in [('legacy-zero-workers',[]),('legacy-workers',['--parallel-ai'])]:
 run(name,['python3','test/check_telemetry_simulation.py',str(exe),'--output',str(b/name),*extra])
run('sim-version',['python3','test/check_sim_revision.py','--base','origin/master'])
sys.path.insert(0,str(r/'test'));from benchmark_parallel_compute import execute,digest
comparisons=[]
for scenario in json.loads((r/'artifacts/resource-growth/final-master/manifest.json').read_text())['scenarios']:
 name=scenario['id'];reference=r/'artifacts/resource-growth/final-master/correctness'/name/'shared'
 for count in [1,4]:
  dest=b/'continuation'/name/f't{count}'
  args=scenario['args']+['--compute-threads',str(count),'--resource-growth-delay','8','--telemetry','checksums']
  os.environ['LD_LIBRARY_PATH']=env['LD_LIBRARY_PATH']
  execute(exe,args,dest,cwd=r)
  matches={f:digest(reference/f)==digest(dest/f) for f in ['world.checksums','game.replay.checksums']}
  comparisons.append({'scenario':name,'compute_threads':count,'reference_revision':'61b6ff740','matches':matches})
  (b/'continuation.json').write_text(json.dumps(comparisons,indent=2)+'\n');assert all(matches.values()),comparisons[-1]
  print(name,count,'matches pre-cleanup',flush=True)
# Removed controls must be rejected, rather than silently ignored.
cmd=[str(exe),'--run-game','--resource-growth-execution','owner','--ticks','1','--output-dir',str(b/'removed-option')]
status=run('removed-option',cmd);assert status!=0
print('continuation comparisons complete',flush=True)
