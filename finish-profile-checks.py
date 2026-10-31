from pathlib import Path
import concurrent.futures,subprocess,time
root=Path(__file__).resolve().parent

def finish(codec):
 log=root/(codec+'-final-full-build.log')
 while True:
  text=log.read_text()
  if 'scons: ***' in text: raise RuntimeError(codec+' build failed')
  if 'scons: done building targets.' in text: break
  time.sleep(10)
 print(codec+' full build passed; running engine/CLI checks',flush=True)
 subprocess.run(['python3',str(root/'run-official-profile.py'),codec,'engine'],check=True)
 if codec=='h264':
  with (root/'final-cli-reference.log').open('w') as output:
   subprocess.run(['python3','tools/cli_reference.py','--binary','build/linux/client/debug/dev-dev_fast-true-linker-auto/src/glob2','--check'],stdout=output,stderr=subprocess.STDOUT,check=True)
  print('Actual default client CLI reference passed',flush=True)
 print(codec+' final integration passed',flush=True)

with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
 futures=[pool.submit(finish,c) for c in ('h264','vp9','av1')]
 for future in concurrent.futures.as_completed(futures):future.result()
