from pathlib import Path
import json,time,os,subprocess,hashlib,xml.etree.ElementTree as ET
root=Path.cwd();base=root/'artifacts/marathi-localization/integration-bc8/native'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
while True:
 p=base/'display-retry-command.json'
 if p.exists():
  request=json.loads(p.read_text())
  if 'exit_code' in request:
   assert request['exit_code']==0,'Display retry failed; preserve diagnostics'
   break
 time.sleep(1)
first=ET.parse(base/'test-results/junit.xml').getroot();retry=ET.parse(base/'test-results-display-retry/junit.xml').getroot();assert not any(list(retry.iter(t)) for t in ('failure','error','skipped'))
def passed(report):return [{'suite':x.attrib['classname'],'case':x.attrib['name']} for x in report.iter('testcase') if not any(x.find(t) is not None for t in ('failure','error','skipped'))]
a=passed(first);b=passed(retry);assert len(a)==18 and len(b)==3;cases=a+b;assert len({(x['suite'],x['case']) for x in cases})==21
info=json.loads((base/'tools-ready.json').read_text());commands=json.loads((base/'focused-and-adapter-build-commands.json').read_text());assert sha(base/'focused-tests')==commands['focused_binary_sha256']==request['binary_sha256'];env=dict(os.environ);env['LD_LIBRARY_PATH']=str(root/'artifacts/marathi-localization/native-deps/lib')+':'+env.get('LD_LIBRARY_PATH','')
with (base/'focused-producer.log').open('w') as log:subprocess.run([str(base/'focused-tests'),'-ltc'],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
producer=json.loads(next(x.split('GLOB2_TEST_PROVENANCE ',1)[1] for x in (base/'focused-producer.log').read_text().splitlines() if x.startswith('GLOB2_TEST_PROVENANCE ')));assert producer['revision']==info['revision'] and not producer['dirty']
record={'revision':info['revision'],'producer':producer,'binary_sha256':sha(base/'focused-tests'),'passed':21,'remaining_failures':0,'skipped':0,'cases':cases,'initial_attempt':{'passed':18,'failed_setup':3,'junit':'test-results/junit.xml','junit_sha256':sha(base/'test-results/junit.xml'),'log':'focused-run.log','log_sha256':sha(base/'focused-run.log')},'display_retry':{'passed':3,'failed':0,'junit':'test-results-display-retry/junit.xml','junit_sha256':sha(base/'test-results-display-retry/junit.xml'),'log':'focused-display-retry.log','log_sha256':sha(base/'focused-display-retry.log'),'command_and_environment':'display-retry-command.json','reason':'Original three display cases failed SDL_Init before fixture assertions. Retry used official per-case owned Xvfb/Openbox with canonical SDL3 X11 driver and X11 session selection; all three actual cases passed.'},'scope':'9 FontKerning +9 SystemLanguage +2 GameGUITouch +1 GUIInteractionCoverage statistics case; passes combined from retained first18 and only3failed-case retries. No full aggregate/native cross-platform claim.','limits':'Registry UI cases use their existing controlled fixtures; separate rendering captures use actual local game history without injected values.'};(base/'focused-results.json').write_text(json.dumps(record,indent=2)+'\n');print('All21 scoped cases now passed with failure/retry provenance',flush=True)
for command,logname in [(['python3',str(base/'run_extra_captures.py'),'--official',info['official'],'--adapter',info['adapter']],'extra-capture-supervisor.log'),(['python3',str(base.parent/'native_qa_runner.py'),'--binary',info['official'],'--output',str(base.parent/'native-render-clean64')],'full-capture-supervisor.log')]:
 with (base/logname).open('w') as log:r=subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT)
 assert r.returncode==0,'Capture failed; preserve logs and partial evidence'
print('Current extra and full native captures complete',flush=True)
