from pathlib import Path
import json,subprocess,os,time,hashlib,xml.etree.ElementTree as ET
root=Path.cwd();base=root/'artifacts/marathi-localization/integration-refresh/native';ready=base/'tools-ready.json'
while not ready.exists():
 if (base/'tools-finish.log').exists() and 'Traceback (most recent call last)' in (base/'tools-finish.log').read_text():raise RuntimeError('Build finishing failed; retain diagnostic log')
 time.sleep(2)
info=json.loads(ready.read_text());env=dict(os.environ);env['LD_LIBRARY_PATH']=str(root/'artifacts/marathi-localization/native-deps/lib')+':'+env.get('LD_LIBRARY_PATH','');env.pop('DISPLAY',None);env.pop('WAYLAND_DISPLAY',None)
folder=base/'focused-build/test';folder.mkdir(parents=True,exist_ok=True);link=folder/'glob2-engine-tests';link.unlink(missing_ok=True);link.symlink_to(base/'focused-tests')
command=['python3','test/run_tests.py','--build-dir',str(base/'focused-build'),'--binary','engine','--filter','FontKerning/*','--filter','SystemLanguage/*','--filter','GameGUITouch/live statistics hide post-game probability and wrap correctly*','--filter','GameGUITouch/tutorial wrapping preserves shaped clusters and refreshes font metrics*','--artifacts',str(base/'test-results'),'--junit',str(base/'test-results/junit.xml'),'--timeout','300','--display-jobs','1','-j2','--verbose'];print('Running current focused font/language/HUD registry cases',flush=True)
with (base/'focused-run.log').open('w') as log:r=subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT)
assert r.returncode==0,'Focused cases failed; inspect retained logs/JUnit'
report=ET.parse(base/'test-results/junit.xml').getroot();cases=[{'suite':x.attrib.get('classname'),'case':x.attrib.get('name')} for x in report.iter('testcase')];assert len(cases)==17;assert not list(report.iter('failure')) and not list(report.iter('error')) and not list(report.iter('skipped'))
with (base/'focused-producer.log').open('w') as log:subprocess.run([str(base/'focused-tests'),'-ltc'],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
line=next(x.split('GLOB2_TEST_PROVENANCE ',1)[1] for x in (base/'focused-producer.log').read_text().splitlines() if x.startswith('GLOB2_TEST_PROVENANCE '));producer=json.loads(line);assert producer['revision']==info['revision'] and producer['dirty']==False
proof={'revision':info['revision'],'producer':producer,'binary_sha256':hashlib.sha256((base/'focused-tests').read_bytes()).hexdigest(),'command':command,'passed':17,'failed':0,'skipped':0,'cases':cases,'scope':'9 FontKerning +6 SystemLanguage +2 relevant registered GameGUITouch cases only; no full native engine aggregate claim.','limits':'HUD registry cases use their existing controlled fixtures. The separate results capture wrapper uses real automatic engine probability samples, without injected values.'};(base/'focused-results.json').write_text(json.dumps(proof,indent=2)+'\n')
print('Focused17cases passed; starting actual integration captures',flush=True)
command=['python3',str(base/'run_integration_captures.py'),'--official',info['official'],'--adapter',info['adapter']]
with (base/'capture-supervisor.log').open('w') as log:r=subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT)
assert r.returncode==0,'Integration capture failure; inspect logs and preserve attempt';print('All native integration checks and captures finished',flush=True)
