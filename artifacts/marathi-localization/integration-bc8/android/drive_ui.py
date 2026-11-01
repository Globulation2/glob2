"""Task-local real-input Android UI capture helper; only owned emulator5678/ADB15187."""
from pathlib import Path
import sys,json,subprocess,time,re,hashlib,struct,os
root=Path.cwd();base=root/'artifacts/marathi-localization/integration-bc8/android';out=base/'ui-qa';out.mkdir(exist_ok=True);sdk=Path('/home/bradley/.local/share/glob2/development/toolchains/mobile/Linux-x86_64-8dbf18a4a97410f6b6a92b4a/android-sdk');adb=[str(sdk/'platform-tools/adb'),'-P','15187','-s','emulator-5678'];package='org.globulation2.glob2'
env=dict(os.environ);env.pop('ADB_SERVER_SOCKET',None);env.pop('ANDROID_SERIAL',None)
def run(args,binary=False):return subprocess.check_output(adb+args,text=not binary,timeout=60,env=env)
assert run(['shell','getprop','ro.boot.qemu.avd_name']).strip()=='glob2-api35-x64';assert run(['shell','getprop','persist.sys.locale']).strip()=='mr-IN'
proof=json.loads((base/'rendering/apk-and-install.json').read_text());assert proof['final_official_producer_revision']=='64aeda1300e3fcb81b074aef1a9a685498edcf63' and proof['installed_apk_sha256']==proof['signed_apk_sha256'];assert subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()==proof['final_official_producer_revision']
recordpath=out/'actions.json';record=json.loads(recordpath.read_text()) if recordpath.exists() else {'source_revision':proof['final_official_producer_revision'],'installed_apk_sha256':proof['installed_apk_sha256'],'actions':[],'capture_cap_bytes':16*1024**2};action,*args=sys.argv[1:];entry={'action':action,'args':args,'monotonic':time.monotonic()}
if action=='launch':entry['output']=run(['shell','am','start','-W','-n',package+'/org.globulation2.glob2.Glob2Activity'])
elif action=='clear-log':run(['logcat','-c'])
elif action=='key':run(['shell','input','keyevent']+args)
elif action=='tap':assert len(args)==2 and all(re.fullmatch(r'\d+',a) for a in args);run(['shell','input','tap']+args)
elif action=='swipe':assert len(args)==5 and all(re.fullmatch(r'\d+',a) for a in args);assert 100<=int(args[-1])<=1000;run(['shell','input','swipe']+args)
elif action=='wait':
 deadline=time.monotonic()+180
 while time.monotonic()<deadline:
  log=run(['logcat','-d','-t','1000'])
  if re.search(args[0],log):break
  time.sleep(1)
 else:raise RuntimeError('Actual screen/log readiness timeout')
 (out/('wait-'+str(len(record['actions']))+'.log')).write_text(log)
elif action=='capture':
 name=args[0];assert re.fullmatch(r'[a-z0-9-]{1,64}',name);destination=out/(name+'.png');assert not destination.exists(),'Never replace an oldattempt frame';time.sleep(1);data=run(['exec-out','screencap','-p'],binary=True);assert data.startswith(b'\x89PNG\r\n\x1a\n');total=sum(p.stat().st_size for p in out.glob('*.png'));assert total+len(data)<=record['capture_cap_bytes'],'No capture stored beyond explicit16MiB budget';destination.write_bytes(data);entry.update({'path':str(destination),'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data),'pixels':struct.unpack('>II',data[16:24])})
elif action=='rotate':
 assert args[0] in ('0','1');record.setdefault('rotation_before',{k:run(['shell','settings','get','system',k]).strip() for k in ['accelerometer_rotation','user_rotation']});run(['shell','settings','put','system','accelerometer_rotation','0']);run(['shell','settings','put','system','user_rotation',args[0]]);time.sleep(2)
elif action=='restore':
 for k,v in record.get('rotation_before',{}).items():run(['shell','settings','put','system',k,v])
 record['rotation_restored']=True
elif action=='stop':run(['shell','am','force-stop',package]);record['own_app_force_stopped']=True
elif action=='logs':(out/'final-logcat.txt').write_text(run(['logcat','-d','-t','1000']))
else:raise ValueError('Unknown explicit QAaction')
record['actions'].append(entry);recordpath.write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(entry))
