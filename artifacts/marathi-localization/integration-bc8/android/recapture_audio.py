from pathlib import Path
import subprocess,json,time,hashlib,os
root=Path.cwd();b=root/'artifacts/marathi-localization/integration-bc8/android';out=b/'focused-preferences';out.mkdir(exist_ok=True);a=['/home/bradley/.local/share/glob2/development/toolchains/mobile/Linux-x86_64-8dbf18a4a97410f6b6a92b4a/android-sdk/platform-tools/adb','-P','15187','-s','emulator-5678'];env=dict(os.environ);env.pop('ADB_SERVER_SOCKET',None);env.pop('ANDROID_SERIAL',None)
run=lambda args,**kw:subprocess.check_output(a+args,env=env,**kw)
assert run(['shell','getprop','ro.boot.qemu.avd_name'],text=True).strip()=='glob2-api35-x64';assert run(['shell','getprop','persist.sys.locale'],text=True).strip()=='mr-IN';proof=json.loads((b/'rendering/apk-and-install.json').read_text());assert proof['final_official_producer_revision']=='64aeda1300e3fcb81b074aef1a9a685498edcf63';assert proof['lifecycle_exit_code']==0
record={'source_revision':proof['final_official_producer_revision'],'installed_apk_sha256':proof['installed_apk_sha256'],'reason':'Original lifecycle AudioPreferenceScreen filename captured wordmark before first paint. Retain that timing miss and all passed lifecycle results; this focused native-preference capture does not repeat lifecycle or modify production code.','original_missed_capture':'rendering/lifecycle/first-run-AudioPreferenceScreen.png','original_missed_capture_sha256':hashlib.sha256((b/'rendering/lifecycle/first-run-AudioPreferenceScreen.png').read_bytes()).hexdigest(),'profile_scope':'pmclear only task-owned app on validatedowned emulator; no tracked/source profile edits','actions':[]}
for args in [['shell','am','force-stop','org.globulation2.glob2'],['shell','pm','clear','org.globulation2.glob2'],['logcat','-c'],['shell','am','start','-n','org.globulation2.glob2/org.globulation2.glob2.Glob2Activity']]:record['actions'].append({'command':args,'stdout':run(args,text=True)})
deadline=time.monotonic()+180
while time.monotonic()<deadline:
 log=run(['logcat','-d','-t','1000'],text=True)
 if 'AudioPreferenceScreen' in log:break
 time.sleep(1)
else:raise RuntimeError('Actualfresh AudioPreference screen readiness timeout')
(out/'ready-log.txt').write_text(log);record['captures']=[];time.sleep(2)
for n in range(3):
 data=run(['exec-out','screencap','-p']);assert data.startswith(b'\x89PNG\r\n\x1a\n');path=out/f'audio-after-ready-{n}.png';assert not path.exists();path.write_bytes(data);record['captures'].append({'path':str(path),'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()});time.sleep(.5)
assert sum(v['bytes'] for v in record['captures'])<12*1024**2;record['scope']='Actual persistent nativeAudio screen afterboundedfirstpaintsettle, awaiting actualvisualreview beforeinput. Java transientoverlay was not paused/held; separateinitialcapturealreadyobserved actualMarathioverlay.';(out/'provenance.json').write_text(json.dumps(record,indent=2)+'\n');print('FocusedfreshnativeAudio captured; awaiting actualimage review.')
