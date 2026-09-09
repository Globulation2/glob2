"""One isolated debugger reproduction; never a queue retry or inferential result."""
import json,subprocess,os
from pathlib import Path
root=Path('/home/bradley/glob2-maxima-mainline-100k-final');run=root/'output/farm-protection-confirmation-100k/runs/77cfd89e3edcc3d22814278348dae0c4d1b9d398a2cc493872c2b652672c296e'
out=root/'output/crash-77cfd-asan';out.mkdir(exist_ok=False)
record=json.loads((run/'100000.command.json').read_text());args=record['argv'];args=[v.replace(str(run),str(out)) for v in args];(out/'checkpoints').mkdir();(out/'command.json').write_text(json.dumps({'purpose':'isolated diagnostic only, no queue mutation','argv':args},indent=2))
# Strip taskset wrapper if present; pin the debugger and inferior together.
if args[:2]==['taskset','-c']:args=args[3:]
env={k:v for k,v in os.environ.items() if not k.startswith(('GLOB2_MAXIMA','GLOB2_NICOWAR_V3'))};env.update(OMP_NUM_THREADS='1',OPENBLAS_NUM_THREADS='1',MKL_NUM_THREADS='1')
args[0]='/home/bradley/glob2-maxima-mainline-0e9092a79/build-sanitize/src/glob2'
env['ASAN_OPTIONS']='detect_leaks=0:abort_on_error=1'
env['UBSAN_OPTIONS']='print_stacktrace=1:halt_on_error=1'
cmd=['taskset','-c','30',*args]
with (out/'gdb.log').open('w') as log:r=subprocess.run(cmd,cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=1800)
(out/'DONE.json').write_text(json.dumps({'debugger_exit_code':r.returncode,'purpose':'diagnostic; not game result'}))
