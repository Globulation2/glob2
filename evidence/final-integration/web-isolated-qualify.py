import os,json,subprocess,datetime,shutil
from pathlib import Path
O=Path(__file__).resolve().parent;cwd=Path('/home/bradley/.codex/worktrees/engine-optimization/glob2');env=os.environ.copy()
removed=[k for k in list(env) if k in ['EMSDK','EM_CONFIG','GLOB2_DEV_MODE'] or k.startswith('GLOB2_TEST')]
for k in removed:env.pop(k,None)
cmd=['npx','--no-install','playwright','test','tests/threading.spec.js','tests/determinism.spec.js','--project=firefox','--grep','threaded runtime executes shared compute','--reporter=json']
r={'state':'running','command':cmd,'cwd':str(cwd/'browser'),'removed_env_keys':removed,'started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()};(O/'web-isolated-qualify.json').write_text(json.dumps(r,indent=2)+'\n')
with(O/'web-isolated-playwright-results.json').open('w')as f, (O/'web-isolated-qualify.log').open('w')as e:p=subprocess.run(cmd,cwd=cwd/'browser',env=env,stdout=f,stderr=e)
r.update(state='passed'if p.returncode==0 else'failed',returncode=p.returncode,finished_utc=datetime.datetime.now(datetime.timezone.utc).isoformat());(O/'web-isolated-qualify.json').write_text(json.dumps(r,indent=2)+'\n')
# Preserve separate browser logs/traces without heavy trace copies.
for q in (cwd/'artifacts/browser-determinism/worker-lifecycle').glob('*-threaded.log'):shutil.copyfile(q,O/('web-isolated-'+q.name))
for q in (cwd/'artifacts/browser-determinism/wasm/verify-threaded').glob('*'):
 if q.is_file():shutil.copyfile(q,O/('web-isolated-'+q.name))
raise SystemExit(p.returncode)
