import json,subprocess,time,shutil
from pathlib import Path
live=Path('/Users/bradley/glob2');base=live/'output/maxima-defense-fixed-controls';record=live/'output/maxima-defense-fix';config=str(live/'output/maxima-live-controls-v21/ssh-config');root='/home/bradley/glob2-maxima-defense-fix'
files={'output/qualification/protocol.json':'protocol.json','output/qualification/routing.json':'evidence/routing.json','output/qualification/determinism.json':'evidence/determinism.json','output/qualification/extended-determinism.json':'evidence/extended-determinism.json','output/qualification/no-orders/PASS.json':'evidence/no-orders.json','output/qualification/tick-limit/PASS.json':'evidence/tick-limit.json','output/qualification/farm-behavior/PASS.json':'evidence/farm-behavior.json','output/defense-regression/PASS.json':'evidence/defense-regression.json','output/long-crash-regressions/PASS.json':'evidence/long-crash-regressions.json'}
for attempt in range(80):
 code='from pathlib import Path;import json;p=Path('+repr(root)+');print(json.dumps({n:(p/n).exists() for n in '+repr(list(files))+'}))'
 r=subprocess.run(['ssh','-F',config,'therig.local','python3 -'],input=code,text=True,capture_output=True,check=True)
 available=json.loads(r.stdout);(record/'COMMISSION_STATUS.json').write_text(json.dumps({'stage':'awaiting_verification','proofs':available,'fleet_pass':(record/'FLEET_PASS.json').exists()},indent=2)+'\n')
 if all(available.values()) and (record/'FLEET_PASS.json').exists():break
 time.sleep(15)
else:raise RuntimeError('Qualification not complete; inspect individual logs')
for src,dest in files.items():subprocess.run(['scp','-F',config,'therig.local:'+root+'/'+src,str(base/dest)],check=True)
for name in ['defense-regression','long-crash-regressions']:assert json.loads((base/'evidence'/f'{name}.json').read_text())['passed']
shutil.copy2(live/'output/maxima-mainline-controls-100k/evidence/statistical-sensitivity.json',base/'evidence/statistical-sensitivity.json')
python='/Users/bradley/glob2/.venv-optimizer/bin/python'
subprocess.run([python,str(base/'prepare.py')],cwd=live,check=True)
(record/'COMMISSION_STATUS.json').write_text(json.dumps({'stage':'fresh_controls_starting','proofs_passed':True},indent=2)+'\n')
with (base/'controller.log').open('w') as log:subprocess.run([python,str(base/'controller.py')],cwd=live,stdout=log,stderr=subprocess.STDOUT,check=True)
