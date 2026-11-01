"""Ignored bounded post-producer QA; keeps each real outcome and cleans own host."""
from pathlib import Path
import json,os,socket,subprocess,time
root=Path.cwd();art=root/'artifacts/bengali-localization-v4';statepath=art/'browser-build-state.json';deadline=time.monotonic()+4*3600
while True:
 state=json.loads(statepath.read_text())
 if 'exitStatus' in state:break
 assert time.monotonic()<deadline,'Producer observation deadline'
 time.sleep(5)
assert state['exitStatus']==0 and state['sourceStable'],state
with (art/'browser-seal.log').open('w') as out:subprocess.run(['python3',str(art/'seal-final-browser.py')],stdout=out,stderr=subprocess.STDOUT,check=True)
producer=art/'browser-final-production-provenance.json';env=dict(os.environ,NODE_PATH=str(root/'platform/node_modules'),GLOB2_BROWSER_PRODUCER_RECORD=str(producer));env.pop('GLOB2_BENGALI_CASES',None)
evidence=art/'browser-runtime/attempt-001';assert not evidence.exists();results={'producer':str(producer),'fiveCaseEvidence':str(evidence)}
with (art/'browser-runtime-five.log').open('w') as out:results['fiveCaseExitStatus']=subprocess.call(['node',str(art/'check-browser-startup.cjs'),str(root/'build/emscripten/client/debug/dev-dev_fast-true-linker-auto'),str(evidence)],env=env,stdout=out,stderr=subprocess.STDOUT)
(art/'browser-qa-status.json').write_text(json.dumps(results,indent=2)+'\n')
s=socket.socket();s.bind(('127.0.0.1',0));port=s.getsockname()[1];s.close();hostlog=(art/'tracked-responsive-server.log').open('w');host=subprocess.Popen(['python3','browser/serve.py',str(port),'--directory','build/emscripten/client/debug/dev-dev_fast-true-linker-auto'],stdout=hostlog,stderr=subprocess.STDOUT)
try:
 import urllib.request
 for _ in range(100):
  try:
   with urllib.request.urlopen('http://127.0.0.1:'+str(port),timeout=2) as r:assert r.status==200
   break
  except Exception:time.sleep(.1)
 else:raise RuntimeError('Tracked regression host unavailable')
 env.update(GLOB2_TEST_URL='http://127.0.0.1:'+str(port),GLOB2_TEST_ENTRY_PATH='/index.html?threads=serial',GLOB2_TEST_RENDERER='webgl2',GLOB2_CHROMIUM_ANGLE='swiftshader')
 command=['node',str(root/'platform/node_modules/@playwright/test/cli.js'),'test','--config=browser/playwright.config.js','responsive-presentation.spec.js','--project=chromium','--grep=antialiased Bengali','--timeout=300000','--workers=1','--reporter=json','--output='+str(art/'tracked-responsive-output')];results['trackedCommand']=command
 with (art/'tracked-responsive-report.json').open('w') as out:results['trackedResponsiveExitStatus']=subprocess.call(command,env=env,stdout=out,stderr=subprocess.STDOUT)
finally:
 host.terminate();host.wait(timeout=30);hostlog.close()
results['ownedQaServerExited']=True;(art/'browser-qa-status.json').write_text(json.dumps(results,indent=2)+'\n');print(json.dumps(results),flush=True)
