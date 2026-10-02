import os,time,subprocess,json,pathlib
root=pathlib.Path(__file__).resolve().parents[2];out=root/'artifacts/memory-optimization'
while True:
 try:os.kill(4928,0)
 except ProcessLookupError:break
 time.sleep(2)
results={}
commands=[('kernel',['python3',str(out/'kernel_benchmark.py')]),('profile',['python3',str(out/'profile_candidate.py')]),('peak-memory',['python3',str(out/'peak_memory.py')]),('engine-tests',['python3','test/run_tests.py','--binary','engine','--filter','*Gradient*/*','--filter','*Lifecycle*/*','--filter','*Continuation*/*','--junit',str(out/'engine-junit.xml')]),('server-build',['scons','-j4','release=1','server=1'])]
for name,cmd in commands:
 print('Starting',name,flush=True)
 with (out/f'{name}-final.log').open('w') as log:p=subprocess.run(cmd,cwd=root,stdout=log,stderr=subprocess.STDOUT)
 results[name]=p.returncode;(out/'post-validation.json').write_text(json.dumps(results,indent=2));print('Finished',name,p.returncode,flush=True)
