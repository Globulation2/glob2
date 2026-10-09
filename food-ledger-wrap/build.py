from pathlib import Path
import subprocess,os,time,json,shutil,hashlib
root=Path.cwd();out=root/'artifacts/food-ledger-wrap';tree=Path('/home/bradley/.codex/worktrees/serial-opt-gcc13/glob2')
while not (root/'artifacts/serial-reanalysis/run.exit').exists():time.sleep(2)
assert (root/'artifacts/serial-reanalysis/run.exit').read_text().strip()=='0'
changed=set(subprocess.check_output(['git','diff','--name-only'],cwd=tree,text=True).splitlines())
production=['src/ai/maxima/'+name for name in ('AIMaximaFoodLedger.cpp','AIMaximaFoodLedger.h','AIMaximaPlacement.cpp','MaximaFoodLedgerStandaloneTest.cpp')]
if changed:
 assert changed<=set(production+['SConstruct','src/app/cli/Headless.cpp']),changed
 for name in production:assert (root/name).read_bytes()==(tree/name).read_bytes(),name
else:
 subprocess.run(['git','apply',str(out/'source.patch')],cwd=tree,check=True)
 subprocess.run(['git','apply',str(root/'artifacts/serial-audit/instrumentation.patch')],cwd=tree,check=True)
cmd=json.loads((root/'artifacts/serial-opt/gcc13-build-command.json').read_text());cmd[-1]='unit-tests'
(out/'build-command.json').write_text(json.dumps(cmd,indent=2))
with (out/'build.log').open('w') as f:subprocess.run(cmd,cwd=tree,env=os.environ|{'GLOB2_SDL3_PREFIX':str(root/'build/serial-audit-deps/prefix')},stdout=f,stderr=subprocess.STDOUT,check=True)
for name,src in [('glob2','src/glob2'),('glob2-unit-tests','test/glob2-unit-tests')]:shutil.copy2(tree/'build/serial-opt-gcc13'/src,out/name)
with (out/'glob2').open('rb') as f:digest=hashlib.file_digest(f,'sha256').hexdigest()
(out/'metadata.json').write_text(json.dumps({'base_revision':subprocess.check_output(['git','rev-parse','HEAD'],cwd=tree,text=True).strip(),'source_patch_sha256':hashlib.sha256((out/'source.patch').read_bytes()).hexdigest(),'binary_sha256':digest,'compiler':subprocess.check_output(['g++-13','--version'],text=True),'command':cmd},indent=2))
print('Built masked candidate and unit harness',flush=True)
