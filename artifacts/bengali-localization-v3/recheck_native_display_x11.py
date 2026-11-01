from pathlib import Path
import os,json,subprocess,tempfile,hashlib
p=Path('artifacts/bengali-localization-v3');build=Path('build/linux/client/debug/dev-dev_fast-true-linker-auto');binary=(build/'test/glob2-engine-tests').resolve();receipt=json.loads((p/'native-focused.json').read_text());env=dict(os.environ,LD_LIBRARY_PATH=str(Path('build/sdl3-ci/prefix/lib').resolve()),SDL_VIDEODRIVER='x11',SDL_AUDIODRIVER='dummy',GLOB2_TEST_DISPLAY='1')
for name in ['WAYLAND_DISPLAY','XDG_SESSION_TYPE']:env.pop(name,None)
cases=['Bengali account avatar retains its complete first shaping cluster [display]','an unrated search shows no ratings; a ranked one explains its range [display]','an unserved version blames the side that is out of date [display]','the room reads positively and hides seeds and pairing strings [display]'];results=[]
for index,case in enumerate(cases):
 profile=p/f'native-display-x11-profile-{index}';profile.mkdir(exist_ok=True);actual=dict(env,GLOB2_USER_DATA_DIR=str(profile.resolve()));xml=(p/f'native-display-x11-{index}.xml').resolve();command=['xvfb-run','-a','-s','-screen 0 1280x800x24 -noreset','python3','test/xvfb_session.py',str(binary),'-r=junit','-o='+str(xml),'--no-breaks=true','--test-suite=OnlineWording','--test-case='+case]
 with (p/f'native-display-x11-{index}.log').open('w') as f:r=subprocess.run(command,env=actual,stdout=f,stderr=subprocess.STDOUT)
 results.append({'case':case,'command':command,'exit_code':r.returncode,'junit':str(xml),'junit_sha256':hashlib.sha256(xml.read_bytes()).hexdigest() if xml.exists() else None});print(index,r.returncode,flush=True)
receipt.update(exit_code=0 if all(x['exit_code']==0 for x in results) else 1,results=results,environment={'SDL_VIDEODRIVER':'x11','GLOB2_TEST_DISPLAY':'1','SDL_AUDIODRIVER':'dummy'},binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),reason='Display-only recheck with explicit X11 because runner removes SDL_VIDEODRIVER, allowing unavailable video auto-selection. Owned Xvfb/Openbox and disposable profiles, no product edits; prior failures retained.')
(p/'native-display-x11.json').write_text(json.dumps(receipt,indent=2)+'\n')
