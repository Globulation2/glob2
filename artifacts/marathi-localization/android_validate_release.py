from pathlib import Path
import sys,os,json,time,subprocess,hashlib,gzip,zipfile,shutil,re
root=Path.cwd();base=root/'artifacts/marathi-localization';out=base/'android-rendering';out.mkdir(exist_ok=True);sdk=Path('/home/bradley/.local/share/glob2/development/toolchains/mobile/Linux-x86_64-8dbf18a4a97410f6b6a92b4a/android-sdk');adb=[str(sdk/'platform-tools/adb'),'-P','15187','-s','emulator-5678'];package='org.globulation2.glob2';env=dict(os.environ);env.pop('ADB_SERVER_SOCKET',None);env.pop('ANDROID_SERIAL',None)
def run(args,**kw):return subprocess.check_output(args,env=env,**kw)
assert run(adb+['shell','getprop','ro.boot.qemu.avd_name'],text=True).strip()=='glob2-api35-x64'
assert run(adb+['shell','getprop','persist.sys.locale'],text=True).strip()=='mr-IN'
proofpath=base/'android-final-producer-provenance.json'
print('Waiting for own verified release APK',flush=True)
while True:
 proof=json.loads(proofpath.read_text()) if proofpath.exists() else {}
 if 'application_build_exit_code' in proof and proof['application_build_exit_code']!=0:raise RuntimeError('Own Android release build failed; inspect finalbuild log')
 if proof.get('apk') and proof.get('state')=='verified final clean official build':break
 time.sleep(5)
apk=Path(proof['apk']['path']);digest=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();assert digest(apk)==proof['apk']['sha256'];report={'unsigned_apk':str(apk),'unsigned_apk_sha256':digest(apk),'final_official_producer_revision':proof['final_revision'],'initial_native_producer_revision':proof['initial_native_producer_revision'],'producer_provenance':'android-final-producer-provenance.json','native_object_reuse':proof['objects'],'head_at_validation':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'adb_server':15187,'serial':'emulator-5678','avd':'glob2-api35-x64','locale':'mr-IN'}
with zipfile.ZipFile(apk) as archive:
 for name,source in [('data/texts.mr.txt',root/'data/texts.mr.txt'),('data/fonts/scripts/NotoSansDevanagari-Regular.ttf',root/'data/fonts/scripts/NotoSansDevanagari-Regular.ttf')]:
  member='assets/glob2-bundle/'+name
  if member not in archive.namelist():member+='.gz';content=gzip.decompress(archive.read(member))
  else:content=archive.read(member)
  assert hashlib.sha256(content).hexdigest()==digest(source),name
  report.setdefault('packaged_assets',{})[member]=digest(source)
aapt=sdk/'build-tools/35.0.0/aapt2'
if not aapt.exists():aapt=next(sdk.glob('build-tools/*/aapt2'))
resources=run([str(aapt),'dump','resources',str(apk)],text=True);(out/'apk-resources.txt').write_text(resources);assert 'खेळाची तयारी सुरू आहे…' in resources and 'preparing_game' in resources
mr_resource_line=next((line for line in resources.splitlines() if re.search(r'\(mr[^)]*\)',line) and 'खेळाची तयारी सुरू आहे…' in line),None)
assert mr_resource_line, 'Reviewed startup target must be assigned to an actual Marathi resource configuration'
assert 'Preparing game…' in resources, 'Default English startup fallback must remain present'
report['compiled_marathi_resource_configuration_line']=mr_resource_line
report['android_resource_verified']='Effective compiled APKresources include Marathi preparing_game with exact reviewed target खेळाची तयारी सुरू आहे…';report['android_source_xml_sha256']=digest(root/'mobile/android/app/src/main/res/values-mr/strings.xml')
for action in ['sign','install']:
 command=['python3','mobile/android.py',action,'--arch','x86_64','--release','--android-sdk',str(sdk)]
 if action=='install':command+=['--serial','emulator-5678','--adb-port','15187']
 with (out/(action+'.log')).open('w') as log:subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
signed=apk.with_name('app-release-development.apk');report['signed_apk_sha256']=digest(signed)
# This AVD and installed application are task-owned; clear only that profile to
# observe first-run asset preparation and the normal preference flow.
run(adb+['shell','pm','clear',package],text=True);run(adb+['shell','am','start','-n',package+'/org.globulation2.glob2.Glob2Activity'],text=True)
report['first_run_captures']=[]
for attempt in range(4):
 time.sleep(2)
 image=run(adb+['exec-out','screencap','-p']);assert image.startswith(b'\x89PNG\r\n\x1a\n');file=out/f'first-run-{attempt}.png';file.write_bytes(image);report['first_run_captures'].append({'path':str(file),'sha256':digest(file)})
report['overlay_visual_confirmation']='Pending actualimage review; compiledresource verified separately.';(out/'apk-and-install.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
command=['python3',str(base/'android_smoke_task.py'),'--serial','emulator-5678','--avd','glob2-api35-x64','--adb-port','15187','--output','build/mobile-smoke-marathi'];print('Running native Marathi startup/settings/lifecycle smoke',flush=True)
with (out/'smoke.log').open('w') as log:result=subprocess.call(command,env=env,stdout=log,stderr=subprocess.STDOUT)
if (root/'build/mobile-smoke-marathi').exists():shutil.copytree(root/'build/mobile-smoke-marathi',out/'lifecycle',dirs_exist_ok=True)
report['lifecycle_exit_code']=result;(out/'apk-and-install.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n');assert result==0,'Android smoke failed; inspect retained lifecycle result/log/images'
print('PASS exact release APKpackagedcatalog/font/compiledMarathiresource/install and native lifecycle',flush=True)
