from pathlib import Path
import subprocess,os,json,shutil,hashlib
root=Path.cwd();out=root/'artifacts/map-wrapping';tree=Path('/home/bradley/.codex/worktrees/serial-opt-gcc13/glob2')
changed=subprocess.check_output(['git','diff','--name-only'],cwd=tree,text=True).splitlines()
allowed={'SConstruct','src/app/cli/Headless.cpp', *['src/ai/maxima/'+n for n in ('AIMaximaFoodLedger.cpp','AIMaximaFoodLedger.h','AIMaximaPlacement.cpp','MaximaFoodLedgerStandaloneTest.cpp')]}
assert set(changed)<=allowed,changed
(out/'previous-aux.patch').write_bytes(subprocess.check_output(['git','diff','--binary'],cwd=tree))
for name in allowed-{'SConstruct','src/app/cli/Headless.cpp'}:
 assert (tree/name).read_bytes()==subprocess.check_output(['git','show','8b6c98194:'+name]),name
subprocess.run(['git','restore','--',*changed],cwd=tree,check=True)
base=subprocess.check_output(['git','rev-parse','HEAD^'],text=True).strip()
subprocess.run(['git','checkout','--detach',base],cwd=tree,check=True)
subprocess.run(['git','apply',str(root/'artifacts/serial-audit/instrumentation.patch')],cwd=tree,check=True)
cmd=json.loads((root/'artifacts/serial-opt/gcc13-build-command.json').read_text());cmd=cmd[:-1]
(out/'baseline-build-command.json').write_text(json.dumps(cmd,indent=2))
with (out/'baseline-build.log').open('w') as f:
 subprocess.run(cmd,cwd=tree,env=os.environ|{'GLOB2_SDL3_PREFIX':str(root/'build/serial-audit-deps/prefix')},stdout=f,stderr=subprocess.STDOUT,check=True)
shutil.copy2(tree/'build/serial-opt-gcc13/src/glob2',out/'baseline-glob2')
(out/'baseline-revision.txt').write_text(base+'\n')
print('Baseline built',base,flush=True)
