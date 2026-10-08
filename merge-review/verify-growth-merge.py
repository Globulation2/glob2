from pathlib import Path
import subprocess, json, os, hashlib, platform, sys
r=Path.cwd(); b=r/'artifacts/resource-growth/merge-review'
env=dict(os.environ, LD_LIBRARY_PATH='/tmp/glob2-sdl3/prefix/lib')
results=[]
def run(name, args):
    (b/(name+'-command.json')).write_text(json.dumps({'cwd':str(r),'command':args,'environment':{'LD_LIBRARY_PATH':env['LD_LIBRARY_PATH']}},indent=2)+'\n')
    with (b/(name+'.log')).open('w') as f:
        code=subprocess.run(args,env=env,stdout=f,stderr=subprocess.STDOUT).returncode
    results.append({'name':name,'exit_code':code})
    (b/'validation.json').write_text(json.dumps(results,indent=2)+'\n')
    print(name,code,flush=True)
    return code
for name,binary,extra in [('engine','engine',['--coverage-profile','compatibility','--exclude-tag','golden']),('golden','engine',['--tag','golden']),('unit','unit',['--coverage-profile','compatibility'])]:
    run(name,['python3','test/run_tests.py','--binary',binary,'--no-display','-j4','--artifacts',str(b/name),'--junit',str(b/(name+'.xml')),*extra])
exe=r/'build/linux/client/release/src/glob2'
for name,extra in [('maxima-zero',[]),('maxima-workers',['--parallel-ai'])]:
    run(name,['python3','test/maxima/check_save_continuation_fixture.py',str(exe),'--output',str(b/name),*extra])
run('sim-version',['python3','test/check_sim_revision.py','--base','origin/master'])
run('diff-check',['git','diff','--check','origin/master...HEAD'])
sys.path.insert(0,str(r/'test'))
from benchmark_parallel_compute import execute,digest
os.environ['LD_LIBRARY_PATH']=env['LD_LIBRARY_PATH']
comparisons=[]
for scenario in json.loads((r/'artifacts/resource-growth/final-master/manifest.json').read_text())['scenarios']:
    name=scenario['id']; reference=r/'artifacts/resource-growth/final-master/correctness'/name/'shared'
    for count in [1,4]:
        dest=b/'continuation'/name/f't{count}'
        args=scenario['args']+['--compute-threads',str(count),'--resource-growth-delay','8','--telemetry','checksums']
        execute(exe,args,dest,cwd=r)
        matches={f:digest(reference/f)==digest(dest/f) for f in ['world.checksums','game.replay.checksums']}
        comparisons.append({'scenario':name,'compute_threads':count,'reference_revision':'61b6ff740','matches':matches})
        (b/'continuation.json').write_text(json.dumps(comparisons,indent=2)+'\n')
        assert all(matches.values()),comparisons[-1]
        print(name,count,'matches pre-cleanup',flush=True)
