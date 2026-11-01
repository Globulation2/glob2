"""Current frozen-runtime refresh: six golden cases and three representative MR UI cases."""
from pathlib import Path
import functools,hashlib,importlib.util,json,os,subprocess,threading,time
ROOT=Path(__file__).resolve().parents[3];OUT=Path(__file__).resolve().parent
expected=os.environ['GLOB2_EXPECTED_QA_REVISION']
ATTEMPT=os.environ.get('GLOB2_QA_ATTEMPT','initial')
RUNOUT=OUT if ATTEMPT=='initial' else OUT/ATTEMPT
RUNOUT.mkdir(exist_ok=True)
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()==expected
assert not subprocess.check_output(['git','status','--porcelain'],cwd=ROOT,text=True).strip()
frozen=json.loads((OUT/'frozen-runtime-inputs.json').read_text())
for name,digest in frozen['sha256_by_path'].items():
 assert hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==digest,('Unexpected runtime/source change',name)
# Execute the existing package-audit section only, which verifies actual parts,
# packaged catalog/font bytes and provenance; its test runner is not executed.
section=(OUT/'run_qa.py').read_text().split("spec=importlib.util.spec_from_file_location",1)[0]
exec(compile(section,str(OUT/'run_qa.py'),'exec'),{'__file__':str(OUT/'run_qa.py')})
BUILD=ROOT/'build/emscripten/client/release'
spec=importlib.util.spec_from_file_location('owned_current_server',ROOT/'browser/serve.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
log=(RUNOUT/'minimal-server.log').open('w')
class Handler(module.Handler):
 def log_message(self,fmt,*args):log.write(fmt%args+'\n');log.flush()
server=module.ThreadingHTTPServer(('127.0.0.1',0),functools.partial(Handler,directory=str(BUILD)))
threading.Thread(target=server.serve_forever,daemon=True).start();origin='http://127.0.0.1:'+str(server.server_address[1]);results=[]
try:
 for engine,device,variant in [('chromium','desktop','serial'),('firefox','phone','threaded'),('webkit','desktop','threaded')]:
  if os.environ.get('GLOB2_QA_ENGINE') and os.environ['GLOB2_QA_ENGINE']!=engine:continue
  for kind in ['golden','risk-ui']:
   folder=RUNOUT/'minimal-qa'/(engine+'-'+kind);folder.mkdir(parents=True,exist_ok=False)
   command=['node',str(ROOT/'platform/node_modules/@playwright/test/cli.js'),'test']
   if kind=='golden':command+=['--config',str(ROOT/'browser/playwright.config.js'),'determinism.spec.js','--grep','committed match record']
   else:command+=['--config',str(OUT/'risk-playwright.config.js'),'--grep',f'Marathi game {device}.*{variant} native']
   command+=['--project='+engine,'--workers=1','--reporter=list,json','--output',str(folder/'cases')]
   info={'validation_revision':expected,'compile_producer_revision':frozen['revision'],'kind':kind,'engine':engine,'expected_cases':2 if kind=='golden' else 1,'command':command,'origin':origin,'state':'running','started_at_utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())}
   (folder/'run.json').write_text(json.dumps(info,indent=2)+'\n')
   env=dict(os.environ,NODE_PATH=str(ROOT/'platform/node_modules'),GLOB2_TEST_URL=origin,GLOB2_TEST_BUILD_DIR=str(BUILD),PLAYWRIGHT_JSON_OUTPUT_FILE=str(folder/'report.json'))
   with (folder/'run.log').open('w') as output:code=subprocess.run(command,cwd=ROOT,env=env,stdout=output,stderr=subprocess.STDOUT).returncode
   info.update(exit_code=code,state='passed' if code==0 else 'failed',finished_at_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()));(folder/'run.json').write_text(json.dumps(info,indent=2)+'\n');results.append(info)
   if kind=='golden':
    for variantName,relative in [('serial',''),('threaded','verify-threaded')]:
     base=ROOT/'artifacts/browser-determinism/wasm'/relative
     for filename in ['verify-match.checksums.txt','verify-match.verdict.json','verify-match.log']:
      data=(base/filename).read_bytes();assert len(data)<4*1024**2
      (folder/(variantName+'-'+filename)).write_bytes(data)
      if filename=='verify-match.checksums.txt':
       native=ROOT/'artifacts/marathi-localization/integration-ffabe/native/native-match-verify/checksums.txt'
       assert data==native.read_bytes(),('WASM/native complete per-tick mismatch',engine,variantName)
       assert len(data.splitlines())==843
       comparison={'engine':engine,'variant':variantName,'trace_sha256':hashlib.sha256(data).hexdigest(),'trace_lines':843,'tick_samples':842,'native_path':str(native.relative_to(ROOT)),'native_and_committed_golden_byte_equal':data==native.read_bytes()==(ROOT/'test/fixtures/multiplayer/FourSquares1.verify-trace.txt').read_bytes()}
       (folder/(variantName+'-native-comparison.json')).write_text(json.dumps(comparison,indent=2)+'\n')
   if code:raise RuntimeError('Actual current '+engine+' '+kind+' failed; see retained report/log')
finally:
 server.shutdown();server.server_close();log.close()
 (RUNOUT/'minimal-qa-summary.json').write_text(json.dumps({'validation_revision':expected,'compile_producer_revision':frozen['revision'],'results':results,'server_closed':True,'scope':'6 current full per-tick golden matches plus3 representative actual MR UI cases; prior351 full12 carry is separate. No current WASM MR-specific unit or expanded scripting-corpus claim.'},indent=2)+'\n')
