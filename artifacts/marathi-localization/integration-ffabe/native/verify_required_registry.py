from pathlib import Path
import json,xml.etree.ElementTree as ET,time
path=Path(__file__).with_name('focused_pipeline.py');exec(path.read_text().split('p=argparse.ArgumentParser();')[0])
assert git('rev-parse','HEAD')=='cdd634cebcde401f1266b6739f8ea21094eb4e56' and not git('status','--porcelain')
assert git('diff','--name-only','e4811dcde89d6393918c36a99d34242ef3be8178','HEAD').splitlines()==['test/fixtures/multiplayer/FourSquares1.g2mr']
assert json.loads((BASE/'focused-link-command.json').read_text())['exit_code']==0
# Required registered checks, isolated through the official runner.
headless=test_command(FILTERS,'focused-headless-results')+['--exclude-tag','display'];headless[headless.index('-j2')]='-j1'
run('focused-headless-verify',headless,3600,video='dummy')
display=test_command(FILTERS,'focused-display-results')+['--tag','display'];display[display.index('-j2')]='-j1'
run('focused-display-verify',display,1800,video='x11')
counts={};passed=[]
for folder in ('focused-headless-results','focused-display-results'):
 a=ET.parse(BASE/folder/'junit.xml').getroot();assert not any(list(a.iter(k)) for k in ('failure','error','skipped'))
 cases=list(a.iter('testcase'));assert cases;counts[folder]=len(cases);passed +=[{'suite':x.attrib.get('classname'),'case':x.attrib.get('name')} for x in cases]
sgsl=[x for x in passed if x['suite']=='LegacyScriptCoverage'];assert len(sgsl)==24
assert sum(x['suite']=='FontKerning' for x in passed)==9
assert sum(x['suite']=='SystemLanguage' for x in passed)==11
for needle in ('Marathi SGSL language tokens','adding Marathi preserves','Malay SGSL language tokens','adding Malay preserves','Marathi registration rejects earlier match identities','Current simulation rejects earlier replays'):
 assert any(x['case'].startswith(needle) for x in passed),needle
write('focused-case-counts.json',{'counts':counts,'passed_cases':passed,'sgsl24':True,'font9':True,'system_language11':True})
# Relay role is an independent official builder; consume only after its exit0.
end=time.monotonic()+1800
while not (BASE/'relay-build-command.json').exists():
 assert time.monotonic()<end;time.sleep(2)
assert json.loads((BASE/'relay-build-command.json').read_text())['exit_code']==0
relay=ROOT/'build/linux/relay/debug/dev-dev_fast-true-linker-auto/src/glob2-relay-tests'
run('relay-admission',[str(relay),'--test-suite=RelayAdmission','--reporters=junit','--out='+str(BASE/'relay-admission.xml')],300)
a=ET.parse(BASE/'relay-admission.xml').getroot();assert not any(list(a.iter(k)) for k in ('failure','error','skipped'));assert any('Marathi catalog registration separates revision 111 and 112' in x.attrib.get('name','') for x in a.iter('testcase'))
run('sim-revision-check',['python3','test/check_sim_revision.py','--base','ffabe1b9ef4e2a9986b1d6321c356124480b8a2d'],300)
write('registry-verification-complete.json',{'compiled_registry_revision':'e4811dcde89d6393918c36a99d34242ef3be8178','fixture_commit':git('rev-parse','HEAD'),'passed_cases':passed,'count':len(passed),'relay_admission_case_count':len(list(a.iter('testcase'))),'scope':'Actual required focused official registry selection; no full engine/unit aggregate claim.'})
print('Required registry cases complete.',flush=True)
