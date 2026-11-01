from pathlib import Path
import os,subprocess,json,socket,hashlib
root=Path.cwd();kit=root/'artifacts/bengali-localization-v4';out=kit/('native-interactive-phone-small' if '--small' in __import__('sys').argv else 'native-interactive-phone');out.mkdir(exist_ok=True);profile=out/'profile';profile.mkdir(exist_ok=True)
built=json.loads((kit/'native-app-build.json').read_text());revision=json.loads((kit/'expected-producer.json').read_text())['revision']
assert built['exit_code']==0 and built['code_identity']['revision']==revision and not built['code_identity']['dirty']
expected=json.loads((kit/'expected-producer.json').read_text());assert built['code_identity']==expected['code_identity']
assert hashlib.sha256((root/'data/texts.bn.txt').read_bytes()).hexdigest()==expected['catalog_sha256']['native']
source=kit/'native-interactive/profile/preferences.txt';d=dict(line.split('=',1) for line in source.read_text().splitlines() if '=' in line);assert d['language']=='bn' and d['username']=='বাংলা১'
with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
small='--small' in __import__('sys').argv
d.update(screenWidth='320' if small else '412',screenHeight='640' if small else '839',textSizePercent='150' if small else '100',interfacePresentation='compact',mcpPort=str(port));(profile/'preferences.txt').write_text(''.join(k+'='+v+'\n' for k,v in d.items()))
program=root/'build/linux/client/debug/dev-dev_fast-true-linker-auto/src/glob2'
assert next(x['sha256'] for x in built['executables'] if x['path']==str(program))==hashlib.sha256(program.read_bytes()).hexdigest()
env=dict(os.environ,SDL_VIDEODRIVER='x11',SDL_AUDIODRIVER='dummy',GLOB2_MOBILE_UI='touch-auto',GLOB2_USER_DATA_DIR=str(profile),LD_LIBRARY_PATH=str(root/'build/sdl3-ci/prefix/lib'))
args=['xvfb-run','-a','-s','-screen 0 1280x900x24',str(program),'play','--mute','--renderer','software','--no-fullscreen','--resizable','--compute-threads','2']
record=dict(revision=revision,runtime_checkout_revision=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),command=args,port=port,profile=str(profile),source_saved_profile_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),layout_adjustments=['screenWidth','screenHeight','interfacePresentation','mcpPort','textSizePercent'],language_and_username_copied_unchanged=True,executable_sha256=hashlib.sha256(program.read_bytes()).hexdigest(),scope='Fresh native process loads actual saved Bengali selection/name copied from UI profile; compact presentation on Linux, not Android/iOS. Only layout and task-owned listener port changed.')
record['code_identity']=built['code_identity']
record['producer_build_sha256']=hashlib.sha256((kit/'native-app-build.json').read_bytes()).hexdigest()
record['input_sha256']={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [root/'data/texts.bn.txt',root/'platform/packages/i18n/locales/bn.json',root/'data/fonts/scripts/manifest.json',root/'data/fonts/scripts/NotoSansBengali-Regular.ttf',root/'data/fonts/sans.ttf']}
with (out/'game.log').open('w') as log:
 p=subprocess.Popen(args,env=env,stdout=log,stderr=subprocess.STDOUT);record['wrapper_pid']=p.pid;(out/'run.json').write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n');print('started',p.pid,port,flush=True);record['exit_code']=p.wait()
(out/'run.json').write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n')
