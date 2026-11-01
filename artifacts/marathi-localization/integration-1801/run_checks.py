from pathlib import Path
import concurrent.futures,subprocess,json,hashlib,time
root=Path.cwd();out=root/'artifacts/marathi-localization/integration-1801';node='/usr/lib/chatgpt/resources/cua_node/bin/node'
jobs=[('platform-contracts',[node,'packages/i18n/scripts/check.ts'],root/'platform'),('platform-ui',[node,'node_modules/vitest/vitest.mjs','run','packages/i18n/test','apps/web/test/financesLocale.test.tsx','apps/web/test/musicUploadLocale.test.tsx','apps/web/test/skin-material-locale.test.tsx','apps/web/test/studioCreditLocale.test.tsx','apps/web/test/ais.test.tsx','--maxWorkers=2'],root/'platform'),('python-tools',['python3','-m','unittest','test.test_google_draft','test.test_cloud_draft','test.test_translation_program'],root)]
def run(job):
 name,cmd,cwd=job;t=time.monotonic();log=out/(name+'.log')
 with log.open('wb') as f:
  try:r=subprocess.run(cmd,cwd=cwd,stdout=f,stderr=subprocess.STDOUT,timeout=300);code=r.returncode
  except subprocess.TimeoutExpired:code='timeout300s'
 d={'command':cmd,'cwd':str(cwd.relative_to(root)), 'exit_code':code,'elapsed_seconds':time.monotonic()-t,'validation_revision':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'log_sha256':hashlib.sha256(log.read_bytes()).hexdigest()};(out/(name+'-result.json')).write_text(json.dumps(d,indent=2)+'\n');print(name,code,flush=True)
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as e:list(e.map(run,jobs))
