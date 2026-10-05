import os,pathlib,subprocess,time
base=pathlib.Path.cwd(); dest=base/'artifacts/threaded-session/direct-before';dest.mkdir(exist_ok=True)
for x in ['profile','home','work']: (dest/x).mkdir(exist_ok=True)
(dest/'profile/preferences.txt').write_text('rememberUnit=1\n')
e=dict(os.environ);e.update(HOME=str(dest/'home'),USERPROFILE=str(dest/'home'),GLOB2_USER_DATA_DIR=str(dest/'profile'),SDL_AUDIODRIVER='dummy',SDL_VIDEODRIVER='dummy',SDL_RENDER_DRIVER='software',GLOB2_TEST_FULLSCREEN='0',LD_PRELOAD=str(base/'artifacts/threaded-session/lower-worker-priority.so'))
c=['taskset','-c',str(min(os.sched_getaffinity(0))),str(base/'build-software-terrain/test/glob2-engine-tests'),'-r=junit','-o='+str(dest/'junit.xml'),'--no-breaks=true','-ts=EngineSession','-tc=incremental sessions*']
start=time.monotonic()
with (dest/'output.log').open('w') as f:r=subprocess.run(c,cwd=dest/'work',env=e,stdout=f,stderr=subprocess.STDOUT,timeout=120)
print(r.returncode,time.monotonic()-start);print((dest/'output.log').read_text()[-2000:])
