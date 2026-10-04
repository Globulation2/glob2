import os,subprocess,tempfile,time,json
from pathlib import Path
root=Path.cwd();evidence=root/'artifacts/ci-repair';runtime=evidence/'x11-c4-linux-runtime/extracted'
binary=runtime/'build/linux/client/release/test/glob2-engine-tests'
case='telemetry full-game timings and desktop phone captures [display:1280x800] [artifacts]'
results=[]
for phase,helper in [('before',evidence/'xvfb-session-before.py'),('after',root/'test/xvfb_session.py')]:
 for n in range(12):
  with tempfile.TemporaryDirectory(prefix='glob2-map-repeat-') as d:
   env=dict(os.environ,HOME=d,SDL_AUDIODRIVER='dummy',SDL_VIDEODRIVER='x11',GLOB2_USER_DATA_DIR=d,LD_LIBRARY_PATH=str(runtime/'build/sdl3-ci/prefix/lib'),GLOB2_TEST_ARTIFACTS_ROOT=str(evidence/f'x11-ci-runtime-{phase}-{n}'))
   command=['xvfb-run','-a','-s','-screen 0 1280x800x24 -noreset','python3',str(helper),str(binary),'--no-breaks=true','--test-case='+case,'--test-suite=AITelemetryUI']
   start=time.monotonic()
   with (evidence/f'x11-ci-runtime-{phase}-{n}.log').open('w') as log:
    try:r=subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=25);code=r.returncode
    except subprocess.TimeoutExpired:code='timeout'
   result={'phase':phase,'repeat':n,'exitCode':code,'seconds':time.monotonic()-start};results.append(result);print(json.dumps(result),flush=True)
   (evidence/'x11-ci-runtime-repeats.json').write_text(json.dumps(results,indent=2)+'\n')
   if code:raise SystemExit(1)
