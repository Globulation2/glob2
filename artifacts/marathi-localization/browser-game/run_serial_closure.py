from pathlib import Path
import functools,importlib.util,json,os,subprocess,threading,time
ROOT=Path(__file__).resolve().parents[3];OUT=Path(__file__).resolve().parent;BUILD=ROOT/'build/emscripten/client/release';DEST=OUT/'serial-ui-closure';DEST.mkdir(exist_ok=False)
assert subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()=='cdd634cebcde401f1266b6739f8ea21094eb4e56'
spec=importlib.util.spec_from_file_location('owned_serial_server',ROOT/'browser/serve.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
f=(DEST/'server.log').open('w')
class H(m.Handler):
 def log_message(self,fmt,*args):f.write(fmt%args+'\n');f.flush()
server=m.ThreadingHTTPServer(('127.0.0.1',0),functools.partial(H,directory=str(BUILD)));threading.Thread(target=server.serve_forever,daemon=True).start();origin='http://127.0.0.1:'+str(server.server_address[1]);results=[]
try:
 for engine,device,config in [('firefox','phone','serial-fixed-tutorial-playwright.config.js'),('webkit','desktop','serial-risk-playwright.config.js')]:
  dest=DEST/engine;dest.mkdir();command=['node',str(ROOT/'platform/node_modules/@playwright/test/cli.js'),'test','--config',str(OUT/config),'--project='+engine,'--grep',f'Marathi game {device}.*serial native','--workers=1','--reporter=list,json','--output',str(dest/'cases')]
  r={'revision':'cdd634cebcde401f1266b6739f8ea21094eb4e56','compile_producer':'e4811dcde89d6393918c36a99d34242ef3be8178','engine':engine,'variant':'serial','command':command,'origin':origin,'state':'running','expected_cases':1,'scope':'Representative current MR serial UI/layout closure; prior actual threaded GUI failures retained as explicit limits.','started_at_utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())}
  (dest/'run.json').write_text(json.dumps(r,indent=2)+'\n')
  with (dest/'run.log').open('w')as output:code=subprocess.run(command,cwd=ROOT,env=dict(os.environ,NODE_PATH=str(ROOT/'platform/node_modules'),GLOB2_TEST_URL=origin,GLOB2_TEST_BUILD_DIR=str(BUILD),PLAYWRIGHT_JSON_OUTPUT_FILE=str(dest/'report.json')),stdout=output,stderr=subprocess.STDOUT).returncode
  r.update(state='passed'if code==0 else'failed',exit_code=code,finished_at_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()));(dest/'run.json').write_text(json.dumps(r,indent=2)+'\n');results.append(r)
finally:
 server.shutdown();server.server_close();f.close();(DEST/'summary.json').write_text(json.dumps({'results':results,'server_closed':True},indent=2)+'\n')
raise SystemExit(0 if all(r['exit_code']==0 for r in results)else 1)
