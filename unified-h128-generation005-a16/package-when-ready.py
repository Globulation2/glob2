import pathlib,json,time,subprocess,os,hashlib,datetime
P=pathlib.Path(__file__).resolve().parent;state=P/'runtime-linux/status.json'
while not state.exists() or json.loads(state.read_text())['status']=='running':time.sleep(5)
assert json.loads(state.read_text())['status']=='passed'
os.nice(19)
with (P/'linux/package.log').open('w') as log:subprocess.run(['python3',str(P/'linux/package.py')],check=True,stdout=log,stderr=subprocess.STDOUT)
archive=P/'linux-runtime.tar.gz'
with (P/'linux/archive.log').open('w') as log:subprocess.run(['tar','-czf',str(archive),'-C',str(P/'runtime-linux-frozen'),'.'],check=True,stdout=log,stderr=subprocess.STDOUT)
(P/'linux/PACKAGE-READY.json').write_text(json.dumps(dict(status='PASS-LOCAL-AWAITING-CROSSPLATFORM',source_revision='4cf3933fee8cfc63d1ddab802589dac6b0ffeb2e',archive=str(archive),sha256=hashlib.file_digest(archive.open('rb'),'sha256').hexdigest(),utc=datetime.datetime.now(datetime.timezone.utc).isoformat()),indent=2)+'\n')
