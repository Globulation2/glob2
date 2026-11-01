from pathlib import Path
import json,hashlib,xml.etree.ElementTree as E,collections
p=Path(__file__).resolve().parent;sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest();read=lambda f:json.loads(f.read_text())
initial=p/'native-tests.xml';cases=E.parse(initial).findall('.//testcase');assert len(cases)==86
failed={c.attrib['name']:c for c in cases if c.find('failure') is not None or c.find('error') is not None};assert len(failed)==4
assert read(p/'native-display-x11.json')['exit_code']==0
rechecked=[]
for f in sorted(p.glob('native-display-x11-*.xml')):
 rows=E.parse(f).findall('.//testcase');assert len(rows)==1;row=rows[0];assert row.attrib['name'] in failed and row.find('failure') is None and row.find('error') is None
 rechecked.append({'name':row.attrib['name'],'path':f.name,'sha256':sha(f)})
assert {r['name'] for r in rechecked}==set(failed)
result={'code_identity':read(p/'native-focused.json')['code_identity'],'actual_total_cases':len(cases),'initial_passed_cases':len(cases)-len(failed),'initial_display_initialization_failures':len(failed),'explicit_x11_display_rechecks_passed':len(rechecked),'remaining_failures':0,'suites':dict(collections.Counter(c.attrib.get('classname') for c in cases)),'rechecked_cases':rechecked,'initial_junit':{'path':initial.name,'sha256':sha(initial)},'display_receipt':{'path':'native-display-x11.json','sha256':sha(p/'native-display-x11.json')},'scope':'Actual 86 focused cases at clean7f. Initial four display initialization failures remain recorded; each exact case passed in owned Xvfb/Openbox with explicit X11. No failure hidden and no unexecuted suite inferred.'}
(p/'native-tests-summary.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
