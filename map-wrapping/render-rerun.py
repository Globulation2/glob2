from pathlib import Path
import subprocess,os,json
out=Path.cwd()/'artifacts/map-wrapping';tree=Path('/home/bradley/.codex/worktrees/serial-opt-gcc13/glob2')
cmd=['python3','test/run_tests.py','--build-dir',str(tree/'build/serial-opt-gcc13'),'--binary','engine','--filter','TorusRender/game rendering*','--jobs','1','--timeout','300','--junit',str(out/'render-rerun.xml')]
(out/'render-rerun-command.json').write_text(json.dumps(cmd,indent=2))
env=os.environ.copy()
for k in ('WAYLAND_DISPLAY','XDG_SESSION_TYPE','DISPLAY','SDL_VIDEO_DRIVER','SDL_VIDEODRIVER'):env.pop(k,None)
with (out/'render-rerun.log').open('w') as f:r=subprocess.run(cmd,cwd=tree,env=env,stdout=f,stderr=subprocess.STDOUT)
(out/'render-rerun.exit').write_text(str(r.returncode)+'\n');print(r.returncode)
