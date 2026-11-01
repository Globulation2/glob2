import json,os,subprocess,sys,tempfile,time
from pathlib import Path
root=Path.cwd(); out=root/'artifacts/wood-reserve'; case=int(sys.argv[1])
names=['sustained stock feeding reports capability estimates and historical strategy targets','observed hunger expands feeding for an opening colony mix beyond nominal forecasts']
binary=root/'build/linux/client/debug/dev-dev_fast-true-linker-auto/test/glob2-engine-tests'
args=[str(binary),'--test-suite=Maxima.Economy','--test-case='+names[case],'--reporters=junit','--out='+str(out/f'final-feeding-{case}.xml')]
start=time.monotonic(); resultCode=124
with tempfile.TemporaryDirectory(prefix='glob2-wood-feeding-') as profile:
 env=dict(os.environ,GLOB2_USER_DATA_DIR=profile,GLOB2_TEST_SOURCE_ROOT=str(root),GLOB2_TEST_ARTIFACTS_ROOT=str(out/f'final-feeding-{case}-artifacts'),SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
 env.pop('GLOB2_TEST_UPDATE_FIXTURES',None)
 with (out/f'final-feeding-{case}.log').open('w') as log:
  try: resultCode=subprocess.run(args,cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=600).returncode
  except subprocess.TimeoutExpired: log.write('\nFocused feeding regression exceeded 600 seconds.\n')
receipt={'command':args,'timeout_seconds':600,'elapsed_seconds':round(time.monotonic()-start,3),'exit_code':resultCode}
(out/f'final-feeding-{case}-command.json').write_text(json.dumps(receipt,indent=2)+'\n')
sys.exit(resultCode)
