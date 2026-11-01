from pathlib import Path
from xml.etree import ElementTree as ET
import json,hashlib,importlib.util
root=Path(__file__).resolve().parents[2];kit=Path(__file__).resolve().parent
load=lambda p:json.loads(p.read_text());sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
s=importlib.util.spec_from_file_location('bp',root/'test/build_provenance.py');bp=importlib.util.module_from_spec(s);s.loader.exec_module(bp);current=bp.source_identity(root)
engine=load(kit/'native-engine-identity/executable.json');unit=load(kit/'native-unit-identity/executable.json');carry=load(kit/'production-input-carry-forward.json')
assert not bp.build_issues(engine['producer'],current)
assert unit['producer']['revision']==carry['old_revision']
assert unit['producer']['sourceTreeSha256']==carry['old_clean_source_tree_sha256']
assert carry['current_code_identity']==current and carry['unit_test_sources_identical']
for identity in [engine,unit]:assert sha(Path(identity['path']))==identity['sha256']
old=list(ET.parse(kit/'native-tests.xml').getroot().iter('testcase'))
used=[x for x in old if x.get('classname') in ['UILayout','TextMetrics']]
fresh=list(ET.parse(kit/'native-language-tests.xml').getroot().iter('testcase'))+list(ET.parse(kit/'native-font-final.xml').getroot().iter('testcase'))
assert len(used)==35 and len(fresh)==17 and len(used+fresh)==52
assert all(x.find('failure') is None and x.find('error') is None and x.find('skipped') is None for x in used+fresh)
names=lambda cases:[{'suite':x.get('classname'),'name':x.get('name')} for x in cases]
files=['native-tests.xml','native-focused.log','native-language-tests.xml','native-language-final.log','native-font-final.xml','native-font-final.log','native-engine-identity/executable.json','native-engine-identity/build-provenance.json','native-unit-identity/executable.json','native-unit-identity/build-provenance.json','production-input-carry-forward.json','adversarial-production-carry-forward.json']
record={'current_code_identity':current,'passed_cases':52,'failed_cases':0,'skipped_cases':0,'retained_prior_passed_cases':names(used),'prior_tested_revision':unit['producer']['revision'],'fresh_current_revision_cases':names(fresh),'provenance_note':'35 unchanged UILayout/TextMetrics cases retain their actual31ef execution; all17FontKerning/SystemLanguage cases freshly executed using the rebuilt36c2 engine binary. Unit test sources and every production input are byte-identical under the independent carry-forward proof. Original failed startup fixture run is preserved and not counted passed.','profile':'Linux GCC13.4.0 full-feature fast development build; not a shipping binary/performance claim.','evidence':[{'path':name,'sha256':sha(kit/name)} for name in files]}
(kit/'native-validation-summary.json').write_text(json.dumps(record,indent=2)+'\n');print('52 actual passed cases bound to exact executable/source identities.')
