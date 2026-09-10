import json,subprocess,os
from pathlib import Path
root=Path('/home/bradley/glob2-maxima-defense-fix');key='ca3cddca076e15fe90357c7959b9ef3fcff5c67d886170568c8bc2bd2c53d5af';src=root/'output/farming-sub-switches-confirmation/runs'/key;out=root/'output/farming-abort-diagnostic';out.mkdir(exist_ok=False)
a=json.loads((src/'100000.command.json').read_text())['argv'][3:];a=[x.replace(str(src),str(out)) for x in a]
cmd=['taskset','-c','15','gdb','--batch','-ex','set pagination off','-ex','set disable-randomization off','-ex','run','-ex','thread apply all bt full','--args',*a]
with (out/'gdb.log').open('w') as f:r=subprocess.run(cmd,cwd=root,stdout=f,stderr=subprocess.STDOUT,timeout=1200)
(out/'DONE.json').write_text(json.dumps({'exit':r.returncode,'diagnostic_only':True,'never_pool':True}))
