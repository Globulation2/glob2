from pathlib import Path
import gzip,json,hashlib,subprocess,os,xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[4];B=Path(__file__).resolve().parent
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert json.loads((B/'focused-link-command.json').read_text())['exit_code']==0
assert json.loads((B/'focused-object-build-command.json').read_text())['exit_code']==0
assert subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()=='e4811dcde89d6393918c36a99d34242ef3be8178'
assert not subprocess.check_output(['git','status','--porcelain'],text=True).strip()
s=gzip.open(B/'focused-inventory.log.gz','rt').read();assert 'the committed match record verifies to the committed checksum trace' in s
producer=json.loads(next(x.split('GLOB2_TEST_PROVENANCE ',1)[1] for x in s.splitlines() if x.startswith('GLOB2_TEST_PROVENANCE ')))
assert producer['revision']=='e4811dcde89d6393918c36a99d34242ef3be8178' and producer['dirty']==False
for name in ['golden-update','golden-fixture-mutation.json']:
 p=B/name
 if p.exists():p.rename(B/(name+'-zero-case-attempt'))
files=['test/fixtures/multiplayer/FourSquares1.g2mr','test/fixtures/multiplayer/FourSquares1.verify-trace.txt'];before={p:sha(ROOT/p) for p in files}
cmd=['python3','test/run_tests.py','--build-dir',str(B/'focused-build'),'--binary','engine','--filter','TurnEngineHarness/the committed*','--update-fixtures','--artifacts',str(B/'golden-update'),'--junit',str(B/'golden-update/junit.xml'),'--timeout','600','-j1','--verbose']
env=dict(os.environ,SDL_VIDEO_DRIVER='dummy',SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy');env['LD_LIBRARY_PATH']=str(ROOT/'artifacts/marathi-localization/native-deps/lib')+':'+env.get('LD_LIBRARY_PATH','')
for k in ('DISPLAY','WAYLAND_DISPLAY'):env.pop(k,None)
record={'producer':producer,'actual_binary':str(B/'focused-engine-tests'),'actual_binary_sha256':sha(B/'focused-engine-tests'),'official_object_build_completion':'focused-object-build-command.json','official_link_completion':'focused-link-command.json','before':before,'command':cmd,'scope':'Actual unchanged official committed-match registry case, no substitute generator/checksum implementation.'}
with (B/'actual-golden-update.log').open('w') as f:r=subprocess.run(cmd,cwd=ROOT,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=900)
record['exit_code']=r.returncode;assert r.returncode==0,'Actual golden case failed'
a=ET.parse(B/'golden-update/junit.xml').getroot();cases=list(a.iter('testcase'));assert len(cases)==1 and not any(list(a.iter(x)) for x in ['failure','error','skipped']),[(x.attrib) for x in cases]
record.update(passed_cases=[x.attrib for x in cases],after={p:sha(ROOT/p) for p in files},root_fixture_commit_required=True)
changed=subprocess.check_output(['git','diff','--name-only','HEAD'],text=True).splitlines();assert set(changed)<=set(files)
record['actual_changed_paths']=changed
(B/'golden-fixture-mutation.json').write_text(json.dumps(record,indent=2)+'\n')
print('Exactly ONE actual committed golden test PASSED; fixtures ready for root inspection/commit.',flush=True)
