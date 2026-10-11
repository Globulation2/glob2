#!/usr/bin/env bash
set -euo pipefail
export CCACHE=1
scons target=web release=1 client_profile=full -j12 > artifacts/memory-wins/final-browser-build.log 2>&1
scons target=web release=1 client_profile=full -j12 web-tests > artifacts/memory-wins/final-browser-test-build.log 2>&1
python3 - <<'PY'
import os,subprocess,json,pathlib
root=pathlib.Path.cwd();out=root/'artifacts/memory-wins';results=[]
for mode in ['serial','threaded']:
 for project in ['chromium','firefox','webkit']:
  env=dict(os.environ,GLOB2_TEST_URL='http://127.0.0.1:8873',GLOB2_TEST_BUILD_DIR=str(root/'build/emscripten/client/release/client-full-browser-1111'),GLOB2_TEST_ENTRY_PATH=f'/?threads={mode}',GLOB2_TEST_RENDERER='webgl2')
  command=['./node_modules/.bin/playwright','test','tests/asset-loading.spec.js','tests/team-colors.spec.js','tests/image-assets.spec.js','tests/artwork-memory.spec.js','tests/mipmap.spec.js','--project',project,'--reporter','list','--output',str(out/f'final-browser-{mode}-{project}')]
  with (out/f'final-browser-{mode}-{project}.log').open('w') as log:r=subprocess.run(command,cwd=root/'browser',env=env,stdout=log,stderr=subprocess.STDOUT)
  results.append({'mode':mode,'project':project,'exit_code':r.returncode,'command':command,'environment':{k:v for k,v in env.items() if k.startswith('GLOB2_TEST_')}})
  (out/'final-browser-results.json').write_text(json.dumps(results,indent=2))
  print(mode,project,r.returncode,flush=True)
if any(r['exit_code'] for r in results):raise SystemExit(1)
PY
