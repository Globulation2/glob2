"""Golden-only official registry link once its actual object is complete.
Does not race SCons outputs: linker writes a distinct ignored binary only.
"""
from pathlib import Path
import gzip,hashlib,json,os,shlex,subprocess,time
ROOT=Path(__file__).resolve().parents[4];B=Path(__file__).resolve().parent
info=json.loads((B/'frozen-inputs.json').read_text(encoding='utf-8'));assert info['revision']=='e4811dcde89d6393918c36a99d34242ef3be8178'
build=ROOT/'build/linux/client/debug/dev-dev_fast-true-linker-auto/test';target=build/'engine-src_net_turn_TurnEngineHarness.o'
start=json.loads((B/'official-build-command.json').read_text(encoding='utf-8'))['started_unix']
deadline=time.monotonic()+1800
while not target.exists() or target.stat().st_mtime<start:
 if time.monotonic()>deadline:raise TimeoutError('Actual TurnEngineHarness object not completed; no fixture change')
 time.sleep(2)
# SCons/ccache writes the finished target atomically. No second compilation occurs.
assert not subprocess.check_output(['git','diff','--name-only','HEAD'],cwd=ROOT,text=True).strip()
lines=gzip.open(B/'official-build.log.gz','rt',encoding='utf-8').read().splitlines()
link=shlex.split(next(x for x in lines if x.startswith('g++ -o ') and '/TranslationPreviewHarness ' in x));idx=next(i for i,x in enumerate(link) if x.endswith('/program-tools_TranslationPreviewHarness.o'))
objects=[build/x for x in ['engine-support_TestMain.o','engine-support_Glob2Test.o','engine-support_GlobalContainerSlot.o','engine-support_EngineFixtures.o','engine-support_LegacyResourceTypes.o','engine-src_net_turn_TurnEngineHarness.o']];assert all(x.exists() for x in objects)
link[link.index('-o')+1]=str(B/'golden-only-tests');link[idx:idx+1]=map(str,objects)
env=dict(os.environ);env.update(json.loads((ROOT/'artifacts/marathi-localization/integration-bc8/native/build-env.json').read_text(encoding='utf-8')));env.update(SDL_VIDEO_DRIVER='dummy',SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy');env['LD_LIBRARY_PATH']=str(ROOT/'artifacts/marathi-localization/native-deps/lib')+':'+env.get('LD_LIBRARY_PATH','')
for key in ('DISPLAY','WAYLAND_DISPLAY'):env.pop(key,None)
with (B/'golden-only-link.log').open('w',encoding='utf-8') as f:subprocess.run(link,cwd=ROOT,env=env,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=600)
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
slot=B/'golden-only-build/test';slot.mkdir(parents=True,exist_ok=True);(slot/'glob2-engine-tests').symlink_to(B/'golden-only-tests')
goldens=['test/fixtures/multiplayer/FourSquares1.g2mr','test/fixtures/multiplayer/FourSquares1.verify-trace.txt'];before={p:sha(ROOT/p) for p in goldens}
cmd=['python3','test/run_tests.py','--build-dir',str(B/'golden-only-build'),'--binary','engine','--filter','TurnEngineHarness/the committed*','--update-fixtures','--artifacts',str(B/'golden-update'),'--junit',str(B/'golden-update/junit.xml'),'--timeout','600','-j1','--verbose']
record={'compiled_revision':info['revision'],'link_command':link,'official_registered_objects':{str(x):sha(x) for x in objects},'binary_sha256':sha(B/'golden-only-tests'),'command':cmd,'before':before,'source_input_snapshot_sha256':sha(B/'frozen-inputs.json'),'scope':'Exact official registered TurnEngineHarness case and support linked to completed official production objects; no alternative checksum or fixture implementation.'}
(B/'early-golden-provenance.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
with (B/'early-golden-update.log').open('w',encoding='utf-8') as f:r=subprocess.run(cmd,cwd=ROOT,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=900)
record.update(exit_code=r.returncode,after={p:sha(ROOT/p) for p in goldens},root_fixture_commit_required=True)
(B/'golden-fixture-mutation.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
assert r.returncode==0,'Golden update failed; preserve evidence'
changed=subprocess.check_output(['git','diff','--name-only','HEAD'],cwd=ROOT,text=True).splitlines();assert set(changed)<=set(goldens)
print('Actual golden update complete; root inspect/commit now. No commit performed.',flush=True)
