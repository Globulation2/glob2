"""Run the Marathi browser game suite against this checkout's final release only."""
from pathlib import Path
import functools,gzip,hashlib,importlib.util,json,os,re,subprocess,sys,threading,time
ROOT=Path(__file__).resolve().parents[3]
OUT=Path(__file__).resolve().parent
SCOPE=os.environ.get('GLOB2_QA_SCOPE','full')
assert SCOPE in ('full','smoke'),SCOPE
WORKERS=int(os.environ.get('GLOB2_QA_WORKERS','1'))
assert WORKERS in (1,2),WORKERS
SMOKE_DEVICE=os.environ.get('GLOB2_QA_SMOKE_DEVICE','desktop')
assert SMOKE_DEVICE in ('desktop','phone','both'),SMOKE_DEVICE
SMOKE_ENGINE=os.environ.get('GLOB2_QA_SMOKE_ENGINE','chromium')
assert SMOKE_ENGINE in ('chromium','firefox','webkit'),SMOKE_ENGINE
SUFFIX=('-'+SMOKE_ENGINE+'-'+SMOKE_DEVICE) if SMOKE_ENGINE!='chromium' or SMOKE_DEVICE=='both' else ('-phone' if SMOKE_DEVICE=='phone' else '')
QA_RECORD='qa-smoke'+SUFFIX+'.json' if SCOPE=='smoke' else 'qa-run.json'
QA_LOG='playwright-smoke'+SUFFIX+'.log' if SCOPE=='smoke' else 'playwright.log'
QA_REPORT='playwright-smoke'+SUFFIX+'-report.json' if SCOPE=='smoke' else 'playwright-report.json'
BUILD=ROOT/'build/emscripten/client/release'
def sha(path):
 h=hashlib.sha256()
 with Path(path).open('rb') as f:
  while data:=f.read(1024*1024):h.update(data)
 return h.hexdigest()
def write(path,value):path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n')
record=json.loads((OUT/'release-build.json').read_text())
assert record['state']=='complete' and record['exit_code']==0,record
revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
changed=subprocess.check_output(['git','diff','--name-only',record['revision'],revision],cwd=ROOT,text=True).splitlines()
assert set(changed)<= {'test/fixtures/multiplayer/FourSquares1.g2mr','test/fixtures/multiplayer/FourSquares1.verify-trace.txt'},changed
assert not subprocess.check_output(['git','status','--porcelain'],cwd=ROOT,text=True).strip()
options=json.loads((BUILD/'options.json').read_text());identity=json.loads((BUILD/'identity.json').read_text())
assert options['release']=='1' and options['web_variant']=='both'
assert identity['mode']=='release'
manifest_path=BUILD/'asset-manifest.js'
manifest=json.loads(manifest_path.read_text().split('Module["glob2AssetManifest"] ??= ',1)[1].strip().rstrip(';'))
write(OUT/'asset-manifest.json',manifest)
files=['index.html','index.js','index.wasm','threaded/index.js','threaded/index.wasm','loader.js','i18n.js','locales/mr.json','fonts/NotoSansDevanagari-Regular.ttf','asset-manifest.js','recording-runtime.js','recording-runtime.wasm']
package={'revision':record['revision'],'packaging_revision':record['revision'],'compile_producer_revision':record.get('compile_producer_revision',record['revision']),'runtime_source_carry_proof':record.get('runtime_source_carry_proof'),'validation_revision':revision,'golden_fixture_followup_paths':changed,
         'production_carry_proof':('Build and validation revisions are identical.' if not changed else 'Exact git diff packaging_revision..validation_revision changes only permitted golden test fixtures; all tracked production/compiler-input, catalog, font and registration files remain identical.'),
         'clean_source':True,'identity':identity,'options':options,'files':{},'parts':[],'resources':[]}
for name in files:
 path=BUILD/name;assert path.is_file(),path
 package['files'][name]={'bytes':path.stat().st_size,'sha256':sha(path)}
assert package['files']['locales/mr.json']['sha256']==sha(ROOT/'platform/packages/i18n/locales/mr.json')
assert package['files']['fonts/NotoSansDevanagari-Regular.ttf']['sha256']==sha(ROOT/'data/fonts/scripts/NotoSansDevanagari-Regular.ttf')
wanted={'/data/texts.mr.txt','/data/texts.list.txt','/data/texts.incomplete.txt','/data/fonts/scripts/NotoSansDevanagari-Regular.ttf'}
for group in manifest['packages']:
 for part in group['parts']:
  path=BUILD/part['url'];actual=sha(path);assert path.stat().st_size==part['size'];assert actual[:16] in path.name
  package['parts'].append({'package':group['name'],'url':part['url'],'bytes':part['size'],'sha256':actual})
  for name,start,end in part['files']:
   if name not in wanted:continue
   with path.open('rb') as stream:stream.seek(start);data=stream.read(end-start)
   digest=hashlib.sha256(data).hexdigest();source=ROOT/name.lstrip('/');matches=digest==sha(source)
   package['resources'].append({'path':name,'package':group['name'],'part':part['url'],'offset':start,'bytes':end-start,'sha256':digest,'matches_source':matches})
for name in wanted:assert any(r['path']==name and r['matches_source'] for r in package['resources']),name
write(OUT/'release-package.json',package)
# The official build wrapper already streams its bounded log directly to gzip.
spec=importlib.util.spec_from_file_location('local_browser_server',ROOT/'browser/serve.py');server_module=importlib.util.module_from_spec(spec);spec.loader.exec_module(server_module)
log=(OUT/'server.log').open('w')
class Handler(server_module.Handler):
 def log_message(self,format,*args):log.write('%s %s\n'%(self.log_date_time_string(),format%args));log.flush()
server=server_module.ThreadingHTTPServer(('127.0.0.1',0),functools.partial(Handler,directory=str(BUILD)))
origin='http://127.0.0.1:'+str(server.server_address[1])
thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
command=['node',str(ROOT/'platform/node_modules/@playwright/test/cli.js'),'test','--config',str(ROOT/'browser/playwright.config.js'),str(ROOT/'browser/tests/marathi.spec.js'),'--workers='+str(WORKERS),'--max-failures=1','--reporter=list,json','--output',str(ROOT/'build/browser-marathi-test-results')]
if SCOPE=='smoke': command += ['--project='+SMOKE_ENGINE,'--grep','Marathi game '+('.*' if SMOKE_DEVICE=='both' else SMOKE_DEVICE)+'.*serial native']
env=dict(os.environ,NODE_PATH=str(ROOT/'platform/node_modules'),GLOB2_TEST_URL=origin,GLOB2_TEST_BUILD_DIR=str(BUILD),PLAYWRIGHT_JSON_OUTPUT_FILE=str(OUT/QA_REPORT))
qa={'revision':record['revision'],'packaging_revision':record['revision'],'compile_producer_revision':record.get('compile_producer_revision',record['revision']),'validation_revision':revision,'golden_fixture_followup_paths':changed,
    'command':command,'scope':SCOPE,'requested_workers':WORKERS,'smoke_device':SMOKE_DEVICE if SCOPE=='smoke' else None,'smoke_engine':SMOKE_ENGINE if SCOPE=='smoke' else None,'expected_cases':(2 if SMOKE_DEVICE=='both' else 1) if SCOPE=='smoke' else 12,'test_source_sha256':sha(ROOT/'browser/tests/marathi.spec.js'),'origin':origin,'server_owner_pid':os.getpid(),'served_directory':str(BUILD),'release_package_sha256':sha(OUT/'release-package.json'),'started_at_utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),'state':'running'}
write(OUT/QA_RECORD,qa);print('Own server',origin,'; Marathi suite starting, scope '+SCOPE,flush=True)
try:
 with (OUT/QA_LOG).open('w') as output:
  result=subprocess.run(command,cwd=ROOT,env=env,stdout=output,stderr=subprocess.STDOUT)
 inventory=re.search(r'Running (\d+) tests using (\d+) worker', (OUT/QA_LOG).read_text())
 if inventory: qa.update(observed_case_inventory_count=int(inventory[1]), observed_worker_count=int(inventory[2]))
 qa.update(exit_code=result.returncode,state='passed' if result.returncode==0 else 'failed',finished_at_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()));write(OUT/QA_RECORD,qa)
 print('Marathi suite exit',result.returncode,flush=True)
finally:
 server.shutdown();server.server_close();log.close()
raise SystemExit(result.returncode)
