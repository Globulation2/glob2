from pathlib import Path
import json,subprocess,os,hashlib
root=Path.cwd();base=root/'artifacts/marathi-localization/integration-bc8/native';proof=json.loads((base/'focused-results.json').read_text());assert proof['passed']==21 and proof['remaining_failures']==0;assert hashlib.sha256((base/'focused-tests').read_bytes()).hexdigest()==proof['binary_sha256'];info=json.loads((base/'tools-ready.json').read_text());env=dict(os.environ)
for command,logname in [(['python3',str(base/'run_extra_captures.py'),'--official',info['official'],'--adapter',info['adapter']],'extra-capture-supervisor.log'),(['python3',str(base.parent/'native_qa_runner.py'),'--binary',info['official'],'--output',str(base.parent/'native-render-clean64')],'full-capture-supervisor.log')]:
 with (base/logname).open('w') as log:r=subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT)
 assert r.returncode==0,'Capture failure; preserve diagnostics'
print('Both extra and all official capture jobs finished',flush=True)
