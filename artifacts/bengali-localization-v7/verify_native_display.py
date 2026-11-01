from pathlib import Path
import os,json,subprocess,hashlib
root=Path.cwd();k=root/'artifacts/bengali-localization-v7';build=root/'build/linux/client/debug/dev-dev_fast-true-linker-auto';binary=build/'test/glob2-engine-tests';base=json.loads((k/'native-focused.json').read_text());assert base['exit_code']==0
expected=json.loads((k/'expected-producer.json').read_text());assert base['code_identity']==expected['code_identity']
env=dict(os.environ,LD_LIBRARY_PATH=str(root/'build/sdl3-ci/prefix/lib'),SDL_VIDEODRIVER='x11',SDL_AUDIODRIVER='dummy',GLOB2_TEST_DISPLAY='1')
for name in ['WAYLAND_DISPLAY','XDG_SESSION_TYPE']:env.pop(name,None)
cases=[('OnlineWording','Bengali account avatar retains its complete first shaping cluster [display]'),('OnlineWording','an unrated search shows no ratings; a ranked one explains its range [display]'),('OnlineWording','an unserved version blames the side that is out of date [display]'),('OnlineWording','the room reads positively and hides seeds and pairing strings [display]'),('GameGUITouch','compact Bengali tutorial yields to statistics and covers the minimap*')]
results=[]
for i,(suite,case) in enumerate(cases):
 profile=k/f'native-display-profile-{i}';profile.mkdir(exist_ok=True);actual=dict(env,GLOB2_USER_DATA_DIR=str(profile));xml=k/f'native-display-{i}.xml';cmd=['xvfb-run','-a','-s','-screen 0 1280x800x24 -noreset','python3','test/xvfb_session.py',str(binary),'-r=junit','-o='+str(xml),'--no-breaks=true','--test-suite='+suite,'--test-case='+case];actual['GLOB2_TEST_ARTIFACTS']=str(k/'native-display-artifacts')
 with (k/f'native-display-{i}.log').open('w') as f:r=subprocess.run(cmd,env=actual,stdout=f,stderr=subprocess.STDOUT)
 results.append({'suite':suite,'case':case,'command':cmd,'exit_code':r.returncode,'junit':str(xml),'junit_sha256':hashlib.sha256(xml.read_bytes()).hexdigest() if xml.exists() else None});print(i,r.returncode,flush=True)
record={'code_identity':base['code_identity'],'command':['python3','artifacts/bengali-localization-v7/verify_native_display.py'],'exit_code':0 if all(x['exit_code']==0 for x in results) else 1,'results':results,'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'environment':{'SDL_VIDEODRIVER':'x11','GLOB2_TEST_DISPLAY':'1'},'scope':'Four explicit X11 online display cases plus focused actual320x640 Bengali150percent tutorial/statistics pixel/input/state regression; all isolated owned Xvfb/Openbox profiles.'}
(k/'native-display-tests.json').write_text(json.dumps(record,indent=2)+'\n');raise SystemExit(record['exit_code'])
