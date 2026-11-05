import os,json,subprocess,gzip,time
from pathlib import Path
R=Path.cwd();B=R/'artifacts/ci-recovery';D=B/'terrain-variant-clock';W=B/'android-index-repair';rev=subprocess.check_output(['git','rev-parse','HEAD'],cwd=W,text=True).strip();env=dict(os.environ,GLOB2_DEV_MODE='isolated',GLOB2_SDL3_PREFIX=str(R/'build/sdl3-ci/prefix'),GLOB2_RECORDING_PREFIX=str(B/'deps/exit-effect-recording'),GLOB2_GLSLANG=str(R/'build/glslang-tools/StandAlone/glslang'),LD_LIBRARY_PATH=str(R/'build/sdl3-ci/prefix/lib'),PYTHONDONTWRITEBYTECODE='1');env['PATH']=str(B/'node/node_modules/.bin')+':'+str(R/'build/sdl3-ci/prefix/bin')+':'+env['PATH'];env.pop('DISPLAY',None)


import traceback,sys,xml.etree.ElementTree as ET
state=dict(pid=os.getpid(),revision=rev,phase='building expanded weighted-variant contract',results=[],note='Test-only phase-normalized stability/reachability. Staticfamiliesretain100tickcomparison; animatedfamiliescheckallphasesand100cycles,plusdeclaredstridewithoutspatialrerolls. No materialskip/clockdisable/runtimechange. Originalbaselinefailurealreadyretained. Separateidleandroidbuild/privateprefix, otherexactd888consumersunchanged.')
def save():(D/'status.json').write_text(json.dumps(state,indent=2)+'\n')
def failure_hook(kind,value,tb):
 state.update(phase='stopped; inspect weighted-variant validation',error=repr(value));save();traceback.print_exception(kind,value,tb)
sys.excepthook=failure_hook
def run(name,cmd):
 state.update(phase=name,command=cmd);save();start=time.monotonic()
 with gzip.open(D/(name+'.log.gz'),'wb')as out:
  q=subprocess.Popen(cmd,cwd=W,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);state['child_pid']=q.pid;save()
  while chunk:=q.stdout.read1(65536):out.write(chunk);out.flush()
  code=q.wait()
 state['results'].append(dict(name=name,exit_code=code,seconds=round(time.monotonic()-start,1)));save();return code
save()
assert run('build',['python3','tools/dev_build.py','--build=build/ci-exit-effect-release','dev_fast=0','release=1','server=0','-j2','build/ci-exit-effect-release/test/glob2-engine-tests'])==0
assert run('weighted-and-animation',['python3','test/run_tests.py','--build-dir','build/ci-exit-effect-release','--binary','engine','--filter','TerrainMaterials/weighted choices*','--filter','TerrainMaterials/*animat*','--filter','TerrainMaterials/*clock*','--fullscreen','--jobs','1','--junit',str(D/'fixed.xml'),'--artifacts',str(D/'fixed')])==0
r=ET.parse(D/'fixed.xml').getroot();assert any('weighted choices'in c.get('name','')for c in r.iter('testcase'))
state['phase']='finished; review baselinefailureandall-animation-phasechecks beforepublishing';save()
