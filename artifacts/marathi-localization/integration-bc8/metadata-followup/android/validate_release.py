from pathlib import Path
import sys,os,json,time,subprocess,hashlib,gzip,zipfile,shutil,re
root=Path.cwd();base=root/'artifacts/marathi-localization';out=base/'integration-bc8/metadata-followup/android/rendering';out.mkdir(parents=True,exist_ok=True);sdk=Path('/home/bradley/.local/share/glob2/development/toolchains/mobile/Linux-x86_64-8dbf18a4a97410f6b6a92b4a/android-sdk');adb=[str(sdk/'platform-tools/adb'),'-P','15187','-s','emulator-5678'];package='org.globulation2.glob2';env=dict(os.environ);env.pop('ADB_SERVER_SOCKET',None);env.pop('ANDROID_SERIAL',None)
sys.path.insert(0,str(root/'mobile'))
import developer_apk
java_env=developer_apk.java_environment(root)
env['JAVA_HOME']=java_env['JAVA_HOME']
env['PATH']=str(Path(env['JAVA_HOME'])/'bin')+os.pathsep+env['PATH']
def run(args,**kw):return subprocess.check_output(args,env=env,**kw)
assert run(adb+['shell','getprop','ro.boot.qemu.avd_name'],text=True).strip()=='glob2-api35-x64'
assert run(adb+['shell','getprop','persist.sys.locale'],text=True).strip()=='mr-IN'
proofpath=base/'integration-bc8/metadata-followup/android/producer-provenance.json'
print('Waiting for own verified release APK',flush=True)
while True:
 proof=json.loads(proofpath.read_text()) if proofpath.exists() else {}
 if 'application_build_exit_code' in proof and proof['application_build_exit_code']!=0:raise RuntimeError('Own Android release build failed; inspect finalbuild log')
 if proof.get('apk') and proof.get('state')=='verified final clean official build':break
 time.sleep(5)
apk=Path(proof['apk']['path']);digest=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();assert digest(apk)==proof['apk']['sha256'];report={'unsigned_apk':str(apk),'unsigned_apk_sha256':digest(apk),'final_official_producer_revision':proof['final_revision'],'initial_native_producer_revision':proof['initial_native_producer_revision'],'producer_provenance':'integration-bc8/metadata-followup/android/producer-provenance.json','native_object_reuse':proof['objects'],'head_at_validation':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'adb_server':15187,'serial':'emulator-5678','avd':'glob2-api35-x64','locale':'mr-IN','java_home':env['JAVA_HOME'],'java_path_prefix':str(Path(env['JAVA_HOME'])/'bin')}
with zipfile.ZipFile(apk) as archive:
 for name,source in [('data/texts.incomplete.txt',root/'data/texts.incomplete.txt'),('data/texts.mr.txt',root/'data/texts.mr.txt'),('data/fonts/scripts/NotoSansDevanagari-Regular.ttf',root/'data/fonts/scripts/NotoSansDevanagari-Regular.ttf')]:
  member='assets/glob2-bundle/'+name
  if member not in archive.namelist():member+='.gz';content=gzip.decompress(archive.read(member))
  else:content=archive.read(member)
  assert hashlib.sha256(content).hexdigest()==digest(source),name
  report.setdefault('packaged_assets',{})[member]=digest(source)
with zipfile.ZipFile(apk) as archive:
 metadata=archive.read('assets/glob2-bundle/data/texts.incomplete.txt').decode()
 assert 'data/texts.mr.txt' in metadata.splitlines() and '*data/texts.mr.txt' not in metadata.splitlines()
 report['packaged_marathi_completion_line']='data/texts.mr.txt'
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
installed_path=run(adb+['shell','pm','path',package],text=True).strip();assert installed_path.startswith('package:') and '\n' not in installed_path
installed_bytes=run(adb+['exec-out','cat',installed_path[len('package:'):]]);installed_sha=hashlib.sha256(installed_bytes).hexdigest();assert installed_sha==report['signed_apk_sha256'];report['installed_apk_sha256']=installed_sha;report['installed_apk_path']=installed_path[len('package:'):]
assert report['head_at_validation']==proof['final_revision'] and not subprocess.check_output(['git','status','--porcelain'],text=True).strip()
# This AVD and installed application are task-owned; clear only that profile to
# observe first-run asset preparation and the normal preference flow.
run(adb+['shell','pm','clear',package],text=True);run(adb+['shell','am','start','-n',package+'/org.globulation2.glob2.Glob2Activity'],text=True)
report['first_run_captures']=[]
capture_start=time.monotonic();capture_deadline=capture_start+6.0;capture_bytes=0;capture_cap=12*1024*1024
report['first_run_capture_budget']={'maximum_frames':10,'target_interval_seconds':0.5,'byte_cap':capture_cap,'maximum_capture_span_seconds':6.0,'actual_capture_bytes':0,'not_stored_due_to_cap':[],'capture_errors':[],'not_attempted_due_to_deadline':0}
for attempt in range(10):
 # First screenshot is immediate after am start; later frames target a bounded
 # half-second sequence. Never pause or manufacture the transient overlay.
 delay=capture_start+attempt*0.5-time.monotonic()
 if delay>0:time.sleep(delay)
 remaining=capture_deadline-time.monotonic()
 if remaining<=0:
  report['first_run_capture_budget']['not_attempted_due_to_deadline']=10-attempt
  break
 try:
  image=run(adb+['exec-out','screencap','-p'],timeout=remaining)
  assert image.startswith(b'\x89PNG\r\n\x1a\n')
 except (subprocess.TimeoutExpired,subprocess.CalledProcessError,AssertionError) as error:
  report['first_run_capture_budget']['capture_errors'].append({'attempt':attempt,'error':str(error)[:500]})
  break
 elapsed=time.monotonic()-capture_start
 if capture_bytes+len(image)>capture_cap:
  report['first_run_capture_budget']['not_stored_due_to_cap'].append({'attempt':attempt,'bytes':len(image),'elapsed_seconds':elapsed})
  continue
 file=out/f'first-run-{attempt:02d}.png';file.write_bytes(image);capture_bytes+=len(image)
 report['first_run_captures'].append({'path':str(file),'sha256':digest(file),'bytes':len(image),'elapsed_seconds':elapsed})
report['first_run_capture_budget']['actual_capture_bytes']=capture_bytes
report['first_run_capture_budget']['actual_span_seconds']=time.monotonic()-capture_start
report['overlay_visual_confirmation']='Pending actualimage review; compiledresource verified separately.';(out/'apk-and-install.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
command=['python3',str(base/'android_smoke_task.py'),'--serial','emulator-5678','--avd','glob2-api35-x64','--adb-port','15187','--output','build/mobile-smoke-marathi-metadata'];print('Running native Marathi startup/settings/lifecycle smoke',flush=True)
with (out/'smoke.log').open('w') as log:result=subprocess.call(command,env=env,stdout=log,stderr=subprocess.STDOUT)
if (root/'build/mobile-smoke-marathi-metadata').exists():shutil.copytree(root/'build/mobile-smoke-marathi-metadata',out/'lifecycle',dirs_exist_ok=True)
report['lifecycle_exit_code']=result;(out/'apk-and-install.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n');assert result==0,'Android smoke failed; inspect retained lifecycle result/log/images'
print('PASS exact release APKpackagedcatalog/font/compiledMarathiresource/install and native lifecycle',flush=True)
