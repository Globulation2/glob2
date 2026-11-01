"""Actual final native UI, new ignored profile; no SCons or source mutation."""
from pathlib import Path
import subprocess,os,json,time,socket,hashlib,sys
root=Path.cwd();kit=root/'artifacts/bengali-localization-v4';out=kit/'native-interactive';out.mkdir(exist_ok=True)
built=kit/'native-app-build.json'
if not built.exists():raise SystemExit('Final build evidence not ready; no old executable launched')
build=json.loads(built.read_text());revision=json.loads((kit/'expected-producer.json').read_text())['revision']
assert build['exit_code']==0 and build['revision']==revision and build['code_identity']['revision']==revision and not build['code_identity']['dirty']
expected=json.loads((kit/'expected-producer.json').read_text())
assert build['code_identity']==expected['code_identity']
assert hashlib.sha256((root/'data/texts.bn.txt').read_bytes()).hexdigest()==expected['catalog_sha256']['native']
assert hashlib.sha256((root/'platform/packages/i18n/locales/bn.json').read_bytes()).hexdigest()==expected['catalog_sha256']['online']
runtime_checkout_revision=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()
runtime_worktree_status=subprocess.check_output(['git','status','--porcelain'],text=True)
profile=out/'profile';profile.mkdir(exist_ok=True);restart='--restart' in sys.argv
if restart:
 assert (profile/'preferences.txt').exists()
 prefs=(profile/'preferences.txt').read_text();port=int(next(x.split('=',1)[1] for x in prefs.splitlines() if x.startswith('mcpPort=')))
else:
 assert not (out/'run.json').exists(),'Preserve evidence; fresh output already used'
 with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
 (profile/'preferences.txt').write_text(f'version=2\nusername=বাংলা১\nlanguage=bn\ntextSizePercent=100\nmute=1\naudioPreferenceChosen=1\nthumbSideChosen=1\nintroductionOffered=1\nmcpEnabled=1\nmcpPort={port}\n')
program=root/'build/linux/client/debug/dev-dev_fast-true-linker-auto/src/glob2'
assert next(x['sha256'] for x in build['executables'] if x['path']==str(program))==hashlib.sha256(program.read_bytes()).hexdigest()
env=dict(os.environ,SDL_VIDEODRIVER='x11',SDL_AUDIODRIVER='dummy',GLOB2_USER_DATA_DIR=str(profile),LD_LIBRARY_PATH=str(root/'build/sdl3-ci/prefix/lib'))
args=['xvfb-run','-a','-s','-screen 0 1280x900x24',str(program),'play','--mute','--renderer','software','--no-fullscreen','--resizable','--window-size','1280x860','--compute-threads','2']
record=dict(revision=revision,producer_build_evidence=str(built.relative_to(root)),producer_build_sha256=hashlib.sha256(built.read_bytes()).hexdigest(),command=args,port=port,profile=str(profile),executable_sha256=hashlib.sha256(program.read_bytes()).hexdigest(),restart=restart,scope='Actual desktop graphical game under Xvfb, local loopback MCP input/capture only; new seeded Bengali profile. No external players/messages, mobile or IME claim.')
record['input_sha256']={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [root/'data/texts.bn.txt',root/'platform/packages/i18n/locales/bn.json',root/'data/fonts/scripts/manifest.json',root/'data/fonts/scripts/NotoSansBengali-Regular.ttf',root/'data/fonts/sans.ttf']}
record['code_identity']=build['code_identity']
record['runtime_checkout_revision']=runtime_checkout_revision
record['runtime_worktree_status']=runtime_worktree_status
record['producer_note']='Producer identity is the immutable clean build marker and matching executable hash; runtime checkout context is recorded separately.'
runfile=out/('restart-run.json' if restart else 'initial-run.json')
with (out/('restart-game.log' if restart else 'game.log')).open('w') as log:
 p=subprocess.Popen(args,env=env,stdout=log,stderr=subprocess.STDOUT)
 record['wrapper_pid']=p.pid;runfile.write_text(json.dumps(record,indent=2)+'\n');(out/'run.json').write_text(json.dumps(record,indent=2)+'\n');print('started',p.pid,port,flush=True)
 record['exit_code']=p.wait()
runfile.write_text(json.dumps(record,indent=2)+'\n');(out/'run.json').write_text(json.dumps(record,indent=2)+'\n');print('exit',record['exit_code'],flush=True)
