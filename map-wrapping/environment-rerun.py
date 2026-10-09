from pathlib import Path
import subprocess,os,json,shutil,hashlib
root=Path.cwd();out=root/'artifacts/map-wrapping';tree=Path('/home/bradley/.codex/worktrees/serial-opt-gcc13/glob2')
overlay=out/'current-asset-overlay';(overlay/'data/terrain').mkdir(parents=True,exist_ok=True)
shutil.copy2(tree/'data/terrain/tileset.json',overlay/'data/terrain/tileset.json')
cmd=['python3','test/run_tests.py','--build-dir',str(tree/'build/serial-opt-gcc13'),'--binary','engine','--filter','EngineSession/incremental sessions*','--filter','TerrainMaterials/contextual contours*','--filter','TerrainMaterials/contextual interiors*','--jobs','1','--timeout','600','--junit',str(out/'environment-rerun.xml')]
(out/'environment-rerun-command.json').write_text(json.dumps(cmd,indent=2))
env=os.environ.copy()
for k in ('WAYLAND_DISPLAY','XDG_SESSION_TYPE','DISPLAY','SDL_VIDEO_DRIVER','SDL_VIDEODRIVER'):env.pop(k,None)
env['GLOB2_ASSET_DIR']=str(overlay)
(out/'environment-rerun-config.json').write_text(json.dumps({'GLOB2_ASSET_DIR':str(overlay),'removed_environment':['WAYLAND_DISPLAY','XDG_SESSION_TYPE','DISPLAY','SDL_VIDEO_DRIVER','SDL_VIDEODRIVER'],'catalog_source_sha256':hashlib.sha256((overlay/'data/terrain/tileset.json').read_bytes()).hexdigest(),'catalog_old_runtime_sha256':hashlib.sha256((tree/'build/serial-opt-gcc13/runtime-assets/data/terrain/tileset.json').read_bytes()).hexdigest()},indent=2))
with (out/'environment-rerun.log').open('w') as f:r=subprocess.run(cmd,cwd=tree,env=env,stdout=f,stderr=subprocess.STDOUT)
(out/'environment-rerun.exit').write_text(str(r.returncode)+'\n');print(r.returncode,flush=True)
