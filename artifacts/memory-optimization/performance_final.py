import pathlib,subprocess,json,time,os
root=pathlib.Path(__file__).resolve().parents[2];out=root/'artifacts/memory-optimization'
# Do not overlap this task's heap profiler with timing.
while True:
 try:os.kill(25330,0)
 except ProcessLookupError:break
 time.sleep(2)
print('Waiting for other compilation and engine-test workloads',flush=True)
quiet=0
while quiet<5:
 rows=subprocess.check_output(['ps','-axo','pcpu=,command='],text=True).splitlines()
 busy=[r for r in rows if float(r.split(None,1)[0])>10 and any(s in r for s in ('/clang','/glob2-engine-tests','/clang++','/wasm-opt','/wasm-ld','fseventsd')) and 'performance_final.py' not in r]
 quiet=0 if busy else quiet+1
 time.sleep(2)
print('Starting performance checks',flush=True)
results={}
commands=[('kernel',['python3',str(out/'kernel_benchmark.py')]),('peak-memory',['python3',str(out/'peak_memory.py')]),('cpu-populated',['python3',str(root/'tools/memory_benchmark.py'),'--baseline',str(out/'baseline-source/build/darwin/client/release/src/glob2'),'--candidate',str(root/'build/darwin/client/release/src/glob2'),'--fixture','30000='+str(root/'artifacts/memory-profile/game/checkpoint-30000.game.gz'),'--fixture','45000='+str(root/'artifacts/memory-profile/game/checkpoint-45000.game.gz'),'--output',str(out/'cpu-populated')])]
for name,cmd in commands:
 print('Starting',name,flush=True)
 with (out/f'{name}-final.log').open('w') as log:p=subprocess.run(cmd,cwd=root,stdout=log,stderr=subprocess.STDOUT)
 results[name]=p.returncode;(out/'performance-final.json').write_text(json.dumps(results,indent=2));print('Finished',name,p.returncode,flush=True)
