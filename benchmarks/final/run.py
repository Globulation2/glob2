from pathlib import Path
import json, os, subprocess, statistics
root=Path.cwd(); out=root/'artifacts/terrain/benchmark-review-final'; out.mkdir(exist_ok=True)
old=Path('/tmp/glob2-materials-baseline-source')
env=dict(os.environ,LD_LIBRARY_PATH='/tmp/glob2-terrain-sdl-patched/prefix/lib')
records=[]
for repeat in range(6):
    for name,workspace in ([('before',old),('after',root)] if repeat % 2 == 0 else [('after',root),('before',old)]):
        destination=out/f'{name}-{repeat+1}'
        command=['taskset','-c','31','python3','test/run_tests.py','--binary','engine','--filter','TerrainValidation/*','--artifacts',str(destination),'--junit',str(destination)+'.xml','--timeout','300']
        load=os.getloadavg()
        with Path(str(destination)+'.log').open('w') as log:
            result=subprocess.run(command,cwd=workspace,env=env,stdout=log,stderr=subprocess.STDOUT)
        if result.returncode: raise RuntimeError(f'{name} failed: {destination}.log')
        p=next(destination.glob('TerrainValidation/*/timing.txt'))
        metrics={k:float(v) for k,v in (line.split() for line in p.read_text().splitlines())}
        records.append(dict(name=name,repeat=repeat+1,cwd=str(workspace),command=command,load_average=load,metrics=metrics))
        print(name, repeat+1, metrics, flush=True)
(out/'runs.json').write_text(json.dumps(records,indent=2)+'\n')
keys=set.intersection(*(set(r['metrics']) for r in records))
medians={name:{k:statistics.median(r['metrics'][k] for r in records if r['name']==name) for k in sorted(keys)} for name in ('before','after')}
(out/'medians.json').write_text(json.dumps(medians,indent=2)+'\n')
