"""Read actual source/artifact hashes only after explicit stable root handoff."""
from pathlib import Path
import argparse,hashlib,json,subprocess
R=Path.cwd();O=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--root-authority',required=True);a=p.parse_args()
assert a.root_authority=='99082-rc0-explicit-both-variants','Wrong root handoff authority'
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
producer='ea0f763fa84445c6dbe5527c966e1eeafc221bc6';allowed=['platform/apps/web/e2e/i18n.spec.ts']
changes=subprocess.check_output(['git','diff',producer,'--name-only'],text=True).splitlines();assert set(changes)<=set(allowed),changes
old=json.loads((O.parent/'browser-final-v7/input-handoff.json').read_text());names=set(old['source_files'])
tracked=subprocess.check_output(['git','ls-files'],text=True).splitlines();names.update(x for x in tracked if x.startswith(('browser/','scons/','src/','libgag/','libusl/')) or x.startswith('platform/packages/i18n/') or x in ['SConstruct','data/texts.list.txt','data/fonts/sans.ttf','data/fonts/scripts/manifest.json'] or x.startswith('data/texts.'))
source={name:sha(R/name) for name in sorted(names) if (R/name).is_file() and not (R/name).is_symlink()}
B=R/'build/emscripten/client/release';excluded={'obj','tmp','include','music-obj','recording-obj','hive-obj','runtime-assets'}
build={str(q.relative_to(R)):sha(q) for q in sorted(B.rglob('*')) if q.is_file() and not(set(q.relative_to(B).parts)&excluded) and q.suffix not in ['.o','.a','.d']}
assert any(x.endswith('/threaded/index.wasm') for x in build) and any(x.endswith('/index.wasm') for x in build)
assert sha(B/'locales/bg.json')==sha(R/'platform/packages/i18n/locales/bg.json')
h={'authorized_by':'/root','package_authorized':True,'execution_authorized':True,'revision':producer,'actual_head_at_freeze':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'source_sha256':'3d16c1c3828c1a69924a3201f800f28f72b85ab6ba741dff39f637f4f836f42f','source_files':source,'build_files':build,'handoff_authority':a.root_authority,'authorized_source_transition':{'authorized_by':'/root','production_changes_allowed':False,'allowed_files':allowed},'platform_scope':'Linux Chromium real serial/threaded browser game; no physical device claim'}
assert not(O/'input-handoff.json').exists();(O/'input-handoff.json').write_text(json.dumps(h,indent=2)+'\n');print('Frozen',len(source),'source files and',len(build),'release input files')
