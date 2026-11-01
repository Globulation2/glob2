from pathlib import Path
import json,xml.etree.ElementTree as ET
path=Path(__file__).with_name('focused_pipeline.py');exec(path.read_text().split('p=argparse.ArgumentParser();')[0])
a=ET.parse(BASE/'focused-headless-results/junit.xml').getroot();head=list(a.iter('testcase'));failed=[c for c in head if list(c.iter('failure'))];assert len(head)==66 and len(failed)==1 and not list(a.iter('error')) and not list(a.iter('skipped'))
known=failed[0];assert known.attrib['classname']=='JavaScriptCompatibility'
assert [f.attrib['message'] for f in known.iter('failure')]==['187 == 186','191 == 186','73 == 72']
passed=[{'suite':c.attrib['classname'],'case':c.attrib['name']} for c in head if c is not known]
assert sum(c['suite']=='LegacyScriptCoverage' for c in passed)==24
assert sum(c['suite']=='FontKerning' for c in passed)==9
assert sum(c['suite']=='SystemLanguage' for c in passed)==11
cmd=test_command(FILTERS,'focused-display-results')+['--tag','display'];cmd[cmd.index('-j2')]='-j1';run('focused-display-verify',cmd,1800,video='x11')
d=ET.parse(BASE/'focused-display-results/junit.xml').getroot();assert not any(list(d.iter(k)) for k in ['failure','error','skipped']);dc=list(d.iter('testcase'));assert len(dc)==4
passed +=[{'suite':c.attrib['classname'],'case':c.attrib['name']} for c in dc]
assert json.loads((BASE/'relay-build-command.json').read_text())['exit_code']==0
relay=ROOT/'build/linux/relay/debug/dev-dev_fast-true-linker-auto/src/glob2-relay-tests';run('relay-admission',[str(relay),'--test-suite=RelayAdmission','--reporters=junit','--out='+str(BASE/'relay-admission.xml')],300)
r=ET.parse(BASE/'relay-admission.xml').getroot();assert not any(list(r.iter(k)) for k in ['failure','error','skipped']);rc=list(r.iter('testcase'));assert any('Marathi catalog registration separates revision 111 and 112' in c.attrib['name'] for c in rc)
run('sim-revision-check',['python3','test/check_sim_revision.py','--base','ffabe1b9ef4e2a9986b1d6321c356124480b8a2d'],300)
write('registry-verification-complete.json',{'compiled_registry_revision':'e4811dcde89d6393918c36a99d34242ef3be8178','fixture_commit':git('rev-parse','HEAD'),'registry_cases_passed':len(passed),'passed_cases':passed,'headless_passed':65,'display_passed':4,'failed_inherited_cases':[{'suite':known.attrib['classname'],'name':known.attrib['name'],'failed_assertions':[x.attrib['message'] for x in known.iter('failure')],'proof':'replay-boundary-inherited.json'}],'relay_admission_cases_passed':len(rc),'relay_cases':[c.attrib for c in rc],'new_product_failures':0,'scope':'69 focused official registry cases passed;1 inherited case retains3 stale version-constant failures, no other errors/skips. No full aggregate claim. Actual dynamic replay acceptance separately retained with success assertions.'})
print('Required focused verification complete with inherited3-constant baseline failure explicit.',flush=True)
