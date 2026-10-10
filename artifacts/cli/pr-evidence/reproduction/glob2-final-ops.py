from pathlib import Path
import os,subprocess,json
root=Path.cwd();binary=root/'build/linux/client/debug/dev-dev_fast-true-linker-auto/src/glob2';base=root/'artifacts/cli/glob2-baseline';out=root/'artifacts/cli/diagnostic-tools';out.mkdir(exist_ok=True)
env=dict(os.environ,GLOB2_USER_DATA_DIR=str(out/'profile'),SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
for name,old,new in [('resources',['-dump-resources','maps/FourSquares1.map.gz'],['dev','dump-resources','maps/FourSquares1.map.gz']),('wheat',['-dump-wheat','maps/FourSquares1.map.gz','0'],['dev','dump-wheat','maps/FourSquares1.map.gz','--team','0']),('tiled',['-dump-tiled','maps/FourSquares1.map.gz','2','2','1','0'],['dev','dump-tiled','maps/FourSquares1.map.gz','--repeat-x','2','--repeat-y','2','--colonies','1','--swarms','0'])]:
 outputs=[]
 for label,exe,args in [('old',base,old),('new',binary,new)]:
  p=subprocess.run([str(exe),*args],cwd=root,env=env,capture_output=True,text=True,timeout=180)
  (out/(name+'-'+label+'.log')).write_text(p.stdout+p.stderr);assert p.returncode==0,(name,label,p.returncode,p.stderr);outputs.append(p.stdout)
 assert outputs[0]==outputs[1],name
print('Diagnostic exports match the original binary.')
