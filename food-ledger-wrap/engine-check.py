from pathlib import Path
import subprocess,os,time,json
root=Path.cwd();out=root/'artifacts/food-ledger-wrap';tree=Path('/home/bradley/.codex/worktrees/serial-opt-gcc13/glob2')
while not (out/'validation.exit').exists():time.sleep(2)
assert (out/'validation.exit').read_text().strip()=='0'
cmd=json.loads((out/'build-command.json').read_text());cmd[-1]='engine-tests'
(out/'engine-build-command.json').write_text(json.dumps(cmd,indent=2))
with (out/'engine-build.log').open('w') as f:subprocess.run(cmd,cwd=tree,env=os.environ|{'GLOB2_SDL3_PREFIX':str(root/'build/serial-audit-deps/prefix')},stdout=f,stderr=subprocess.STDOUT,check=True)
cmd=['python3','test/run_tests.py','--build-dir',str(tree/'build/serial-opt-gcc13'),'--binary','engine','--filter','Maxima.*/*','--junit',str(out/'engine-tests.xml')]
(out/'engine-test-command.json').write_text(json.dumps(cmd,indent=2))
with (out/'engine-tests.log').open('w') as f:subprocess.run(cmd,cwd=tree,stdout=f,stderr=subprocess.STDOUT,check=True)
print('PASS affected engine harness build and Maxima integration suites',flush=True)
