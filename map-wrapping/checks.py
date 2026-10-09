from pathlib import Path
import subprocess,os,json,time,concurrent.futures
root=Path.cwd();out=root/'artifacts/map-wrapping';tree=Path('/home/bradley/.codex/worktrees/serial-opt-gcc13/glob2')
while not (out/'candidate-build.exit').exists():time.sleep(2)
assert (out/'candidate-build.exit').read_text().strip()=='0'
env=os.environ.copy();env.pop('WAYLAND_DISPLAY',None);env.pop('XDG_SESSION_TYPE',None);env['SDL_VIDEO_DRIVER']='x11'
def run(name,cmd,cwd=tree,extra=None):
 (out/f'{name}-command.json').write_text(json.dumps(cmd,indent=2))
 with (out/f'{name}.log').open('w') as f:r=subprocess.run(cmd,cwd=cwd,env=env| (extra or {}),stdout=f,stderr=subprocess.STDOUT)
 (out/f'{name}.exit').write_text(str(r.returncode)+'\n');print(name,r.returncode,flush=True);return r.returncode
unit=['python3','test/run_tests.py','--build-dir',str(tree/'build/serial-opt-gcc13'),'--binary','unit','--jobs','6','--display-jobs','1','--timeout','600','--junit',str(out/'unit-tests.xml'),'--write-inventory',str(out/'unit-inventory.json')]
engine=json.loads((out/'engine-test-command.json').read_text())
golden_build=json.loads((out/'candidate-build-command.json').read_text())[:-2]+['map-generator-golden-test']
def golden():
 result=run('golden-build',golden_build,extra={'GLOB2_SDL3_PREFIX':str(root/'build/serial-audit-deps/prefix')})
 if result:return result
 return run('generator-goldens',[str(tree/'build/serial-opt-gcc13/src/MapGeneratorGoldenTest'),'map-mask-goldens','--require-rows'],extra={'GLOB2_USER_DATA_DIR':str(out/'golden-userdata')})
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
 futures=[pool.submit(run,'unit-tests',unit),pool.submit(run,'engine-tests',engine),pool.submit(run,'candidate-continuation',['python3',str(out/'continue.py'),'candidate'],root),pool.submit(golden)]
 results=[f.result() for f in futures]
assert not any(results),results
print('All selected validation completed',flush=True)
