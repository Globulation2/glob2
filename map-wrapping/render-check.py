from pathlib import Path
import subprocess,os,time,json
root=Path.cwd();out=root/'artifacts/map-wrapping';tree=Path('/home/bradley/.codex/worktrees/serial-opt-gcc13/glob2')
while not (out/'candidate-build.exit').exists():time.sleep(2)
assert (out/'candidate-build.exit').read_text().strip()=='0'
cmd=['python3','test/run_tests.py','--build-dir',str(tree/'build/serial-opt-gcc13'),'--binary','engine','--filter','TorusRender/*','--filter','EditorTerrainPaint/*','--jobs','2','--display-jobs','1','--timeout','300','--junit',str(out/'render-tests.xml')]
(out/'render-test-command.json').write_text(json.dumps(cmd,indent=2))
env=os.environ.copy()
for k in ('WAYLAND_DISPLAY','XDG_SESSION_TYPE','DISPLAY'):env.pop(k,None)
env['SDL_VIDEO_DRIVER']='x11'
with (out/'render-tests.log').open('w') as f:r=subprocess.run(cmd,cwd=tree,env=env,stdout=f,stderr=subprocess.STDOUT)
(out/'render-tests.exit').write_text(str(r.returncode)+'\n');print(r.returncode,flush=True)
