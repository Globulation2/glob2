from pathlib import Path
import subprocess,json,os,hashlib,socket,time
root=Path.cwd();out=root/'artifacts/marathi-localization/integration-ffabe/online';out.mkdir(exist_ok=True)
cid='b271ca6f7883015c62917c34d5b530698a18325814cd18d124c2db6e8f0229af'
info=json.loads(subprocess.check_output(['docker','inspect','glob2-marathi-postgres']))[0];assert info['Id']==cid
sock=socket.socket();assert sock.connect_ex(('127.0.0.1',4587))!=0,'Port4587alreadyoccupied; do not touch it';sock.close()
if not info['State']['Running']:subprocess.run(['docker','start',cid],check=True,stdout=subprocess.DEVNULL)
live=json.loads(subprocess.check_output(['docker','inspect',cid]))[0]
ports=live['NetworkSettings']['Ports']['5432/tcp'];assert len(ports)==1 and ports[0]['HostIp']=='127.0.0.1';dbport=int(ports[0]['HostPort'])
deadline=time.monotonic()+45
while True:
    ready=subprocess.run(['docker','exec',cid,'pg_isready','-U','glob2'],capture_output=True,timeout=3)
    if ready.returncode==0:break
    if time.monotonic()>deadline:raise RuntimeError('Owned Postgres did not become ready; no foreign DB used')
    time.sleep(0.5)
node='/usr/lib/chatgpt/resources/cua_node/bin/node';env=dict(os.environ,PATH='/usr/lib/chatgpt/resources/cua_node/bin:'+os.environ['PATH'],PORT='4587',TEST_DATABASE_URL=f'postgres://glob2:glob2@127.0.0.1:{dbport}/postgres',GLOB2_WEB_CLIENT_DIR=str(out/'no-game-build'))
with (out/'server.log').open('wb') as log:proc=subprocess.Popen([node,'e2e/server.ts'],cwd=root/'platform/apps/web',env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
files={str(p.relative_to(root/'platform/apps/web/dist')):hashlib.sha256(p.read_bytes()).hexdigest() for p in (root/'platform/apps/web/dist').rglob('*') if p.is_file()}
v={'pid':proc.pid,'cwd':str(root/'platform/apps/web'),'argv':[node,'e2e/server.ts'],'revision':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'node_version':subprocess.check_output([node,'--version'],text=True).strip(),'url':'http://127.0.0.1:4587','owned_postgres_container':cid,'database_host_port':dbport,'database_scope':'Task-owned isolatedPostgres, freshsynthetic E2E database; no realaccounts or externalmessages. Environment values not dumped.','server_env_keys':['PORT','TEST_DATABASE_URL','GLOB2_WEB_CLIENT_DIR','PATH'],'dist_sha256':files,'platform_git_inventory_sha256':hashlib.sha256(subprocess.check_output(['git','ls-tree','-r','HEAD','--','platform'])).hexdigest(),'previous_attempt':'startup-attempt1-db-port.log: retainedECONNREFUSED because Docker assigned a new dynamic hostport after restart; live mapping nowinspected and readiness checked beforelaunch. No product/test assertions ran in thatattempt.','scope':'Freshcurrentproduction website build;3freshMRhomecasesacross3engines/bothsizes; source/valuesunchanged12oldflowscarriedseparately. Browser-gameWASM servedelsewhere; no currentWASM claim here.'};(out/'provenance.json').write_text(json.dumps(v,indent=2)+'\n');print(json.dumps({'pid':proc.pid,'url':v['url'],'dist_files':len(files)}))
