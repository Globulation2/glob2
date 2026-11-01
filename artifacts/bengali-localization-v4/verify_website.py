from pathlib import Path
import json,subprocess,os,socket,hashlib,importlib.util
root=Path.cwd();kit=root/'artifacts/bengali-localization-v4';expected='fda5e076eafc103b59b18136c137abdb6262d981'
spec=importlib.util.spec_from_file_location('bp',root/'test/build_provenance.py');bp=importlib.util.module_from_spec(spec);spec.loader.exec_module(bp);identity=bp.source_identity(root);assert identity['revision']==expected and not identity['dirty']
node='/home/bradley/.local/share/glob2/development/toolchains/emscripten/Linux-x86_64-88a621bc131cd4954413aa39/node/24.19.0_64bit/bin'
env=dict(os.environ,PATH=node+os.pathsep+os.environ['PATH'],TEST_DATABASE_URL='postgres://glob2:glob2@127.0.0.1:55434/postgres',SCREENSHOT_DIR=str(kit/'web-screens'))
with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
projects=[]
for browser in ['chromium','firefox','webkit']:
 for label,w,h,scale,touch in [('desktop',1280,860,1,False),('phone',412,839,2,True)]:projects.append({'name':browser+'-'+label,'use':{'browserName':browser,'viewport':{'width':w,'height':h},'deviceScaleFactor':scale,'hasTouch':touch}})
config={'testDir':str(root/'platform/apps/web/e2e'),'testMatch':'i18n.spec.ts','outputDir':str(kit/'web-results'),'timeout':120000,'workers':1,'retries':0,'reporter':[['list'],['json',{'outputFile':str(kit/'web-e2e-results.json')}]],'use':{'baseURL':f'http://127.0.0.1:{port}','trace':'retain-on-failure'},'projects':projects,'webServer':{'command':'node e2e/server.ts','cwd':str(root/'platform/apps/web'),'url':f'http://127.0.0.1:{port}/api/v1/instance','timeout':120000,'reuseExistingServer':False,'env':{'PORT':str(port)}}}
(kit/'web.config.cjs').write_text('const {defineConfig}=require('+json.dumps(str(root/'platform/node_modules/@playwright/test'))+');\nmodule.exports=defineConfig('+json.dumps(config,indent=2)+');\n')
def run(name,command):
 assert bp.source_identity(root)==identity
 with (kit/(name+'.log')).open('w') as f:r=subprocess.run(command,env=env,stdout=f,stderr=subprocess.STDOUT)
 assert bp.source_identity(root)==identity
 (kit/(name+'.json')).write_text(json.dumps({'code_identity':identity,'command':command,'exit_code':r.returncode,'scope':'Production website build and actual Bengali i18n browser workflows against an isolated seeded local API; six desktop/phone browser projects, no physical device claim.'},indent=2)+'\n');print(name,r.returncode,flush=True)
 if r.returncode:raise SystemExit(r.returncode)
run('web-build',['npm','--prefix','platform','run','build','--workspace','@glob2/web'])
run('web-e2e',[str(root/'platform/node_modules/.bin/playwright'),'test','--config',str(kit/'web.config.cjs'),'--grep','bn|Bengali'])
