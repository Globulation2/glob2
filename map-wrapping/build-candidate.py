from pathlib import Path
import subprocess,os,json,shutil,hashlib
root=Path.cwd();out=root/'artifacts/map-wrapping';tree=Path('/home/bradley/.codex/worktrees/serial-opt-gcc13/glob2')
assert (out/'baseline-build.exit').read_text().strip()=='0'
patch=subprocess.check_output(['git','diff','HEAD^','HEAD','--binary']);(out/'source.patch').write_bytes(patch)
paths=subprocess.check_output(['git','diff','HEAD^','HEAD','--name-only'],text=True).splitlines()
changed=set(subprocess.check_output(['git','diff','--name-only'],cwd=tree,text=True).splitlines())
assert changed<=set(paths)|{'SConstruct','src/app/cli/Headless.cpp'},changed-set(paths)
for name in paths:
 if not (tree/name).exists() or (tree/name).read_bytes()!=(root/name).read_bytes():shutil.copy2(root/name,tree/name)
revision=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();(out/'candidate-revision.txt').write_text(revision+'\n')
cmd=json.loads((root/'artifacts/serial-opt/gcc13-build-command.json').read_text());cmd[1]='-j12'
(out/'candidate-build-command.json').write_text(json.dumps(cmd,indent=2))
with (out/'candidate-build.log').open('w') as f:
 subprocess.run(cmd,cwd=tree,env=os.environ|{'GLOB2_SDL3_PREFIX':str(root/'build/serial-audit-deps/prefix')},stdout=f,stderr=subprocess.STDOUT,check=True)
for n,p in [('candidate-glob2','src/glob2'),('glob2-unit-tests','test/glob2-unit-tests'),('glob2-engine-tests','test/glob2-engine-tests')]:
 shutil.copy2(tree/'build/serial-opt-gcc13'/p,out/n)
print('Candidate built',revision,flush=True)
