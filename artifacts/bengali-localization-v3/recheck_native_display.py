from pathlib import Path
import os,json,subprocess
p=Path('artifacts/bengali-localization-v3');d=json.loads((p/'native-focused.json').read_text());env=dict(os.environ,LD_LIBRARY_PATH=str(Path('build/sdl3-ci/prefix/lib').resolve()))
for name in ['DISPLAY','WAYLAND_DISPLAY','XDG_SESSION_TYPE']:env.pop(name,None)
command=['python3','test/run_tests.py','--build-dir','build/linux/client/debug/dev-dev_fast-true-linker-auto','--binary','engine','--filter','OnlineWording/*','--tag','display','--display-jobs','2','--artifacts',str((p/'native-display-tests').resolve()),'--junit',str((p/'native-display-tests.xml').resolve())]
with (p/'native-display-tests.log').open('w') as f:r=subprocess.run(command,env=env,stdout=f,stderr=subprocess.STDOUT)
d.update(command=command,exit_code=r.returncode,reason='Earlier four display cases inherited desktop DISPLAY=:0 and no accessible video display. Re-execute only those four in test runner owned Xvfb/Openbox with DISPLAY/WAYLAND_DISPLAY/XDG_SESSION_TYPE removed. Initial failure and all original passing cases retained.')
(p/'native-display-tests.json').write_text(json.dumps(d,indent=2)+'\n');print(r.returncode)
