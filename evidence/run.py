import os,json,subprocess,gzip,time
from pathlib import Path
R=Path.cwd();B=R/'artifacts/ci-recovery';D=B/'preview-failure-diagnostics';W=B/'android-index-repair';rev=subprocess.check_output(['git','rev-parse','HEAD'],cwd=W,text=True).strip();env=dict(os.environ,GLOB2_DEV_MODE='isolated',GLOB2_SDL3_PREFIX=str(R/'build/sdl3-ci/prefix'),GLOB2_RECORDING_PREFIX=str(R/'build/sdl3-ci/prefix/recording'),GLOB2_GLSLANG=str(R/'build/glslang-tools/StandAlone/glslang'),LD_LIBRARY_PATH=str(R/'build/sdl3-ci/prefix/lib'),PYTHONDONTWRITEBYTECODE='1');env['PATH']=str(B/'node/node_modules/.bin')+':'+str(R/'build/sdl3-ci/prefix/bin')+':'+env['PATH'];env.pop('DISPLAY',None)

state=dict(pid=os.getpid(),revision=rev,phase='building test-only diagnostics',results=[],note='Only6INFO lines added aroundunchangedpreviewassertion. No generator/runtime/gate/timing change. Oldandroid-indexbuildoutputinactiveand reusedthroughSConsdependencyvalidation; current207native/Wasmhelpersunchanged.')
def save():(D/'status.json').write_text(json.dumps(state,indent=2)+'\n')
def run(name,cmd):
 state.update(phase=name,command=cmd);save();start=time.monotonic()
 with gzip.open(D/(name+'.log.gz'),'wb')as out:
  q=subprocess.Popen(cmd,cwd=W,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);state['child_pid']=q.pid;save()
  while chunk:=q.stdout.read1(65536):out.write(chunk);out.flush()
  code=q.wait()
 state['results'].append(dict(name=name,exit_code=code,seconds=round(time.monotonic()-start,1)));save();return code
save()
assert run('build',['python3','tools/dev_build.py','--build=build/android-index-release','dev_fast=0','release=1','server=0','-j2','tests'])==0
for attempt in range(1,4):
 code=run('targeted-'+str(attempt),['python3','test/run_tests.py','--build-dir','build/android-index-release','--filter','CustomGameSetup/custom game screens*','--jobs','1','--fullscreen','--timeout','900','--junit',str(D/('attempt-'+str(attempt)+'.xml')),'--artifacts',str(D/('attempt-'+str(attempt)))])
 if code:break
state['phase']='finished; inspectdiagnosticlandscape/seed/statewithoutweakeningoriginalassertion';save()
