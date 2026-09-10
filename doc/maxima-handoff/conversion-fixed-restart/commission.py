import json,sys,subprocess,time,shutil,os
from pathlib import Path
live=Path('/Users/bradley/glob2');record=Path(__file__).resolve().parent;b=live/'output/maxima-conversion-fixed-controls';root='/home/bradley/glob2-maxima-conversion-fixed';cfg=str(live/'output/maxima-live-controls-v21/ssh-config');python=str(live/'.venv-optimizer/bin/python')
for _ in range(480):
 if (record/'FLEET_PASS.json').exists():break
 time.sleep(15)
else:raise RuntimeError('Fleet qualification incomplete; investigate')
files={'protocol.json':'protocol.json','routing.json':'evidence/routing.json','determinism.json':'evidence/determinism.json','extended-determinism.json':'evidence/extended-determinism.json','no-orders/PASS.json':'evidence/no-orders.json','tick-limit/PASS.json':'evidence/tick-limit.json','farm-behavior/PASS.json':'evidence/farm-behavior.json','farm-access/PASS.json':'evidence/farm-access.json'}
for src,dest in files.items():subprocess.run(['scp','-F',cfg,'therig.local:'+root+'/output/qualification/'+src,str(b/dest)],check=True)
shutil.copy2(live/'output/maxima-defense-fixed-controls/evidence/statistical-sensitivity.json',b/'evidence/statistical-sensitivity.json')
s=(live/'output/maxima-defense-fixed-controls/prepare.py').read_text().replace("'tools/maxima_experiment_fleet.py',",'').replace('output/maxima-defense-fix/FLEET_PASS.json','output/maxima-conversion-fixed-restart/FLEET_PASS.json').replace('glob2-maxima-defense-fix','glob2-maxima-conversion-fixed').replace("remote=Path('/home/bradley/glob2-maxima-conversion-fixed/output/fresh-controls')","remote=Path('/home/bradley/glob2-maxima-conversion-fixed/output/fresh-controls')")
(b/'prepare.py').write_text(s)
s=(live/'output/maxima-defense-fixed-controls/controller.py').read_text().replace('glob2-maxima-defense-fix','glob2-maxima-conversion-fixed');(b/'controller.py').write_text(s)
subprocess.run([python,str(b/'prepare.py')],check=True,cwd=live)
with (b/'controller.log').open('w') as log:subprocess.run([python,str(b/'controller.py')],stdout=log,stderr=subprocess.STDOUT,check=True,cwd=live)
accept=json.loads((b/'controls-budget-checkpoint.json').read_text());assert accept['accepted'] and accept['all_jobs_complete']
# Strengthen the successor's behavior evidence; controls already finished.
sys.path.insert(0,'/Users/bradley/glob2-maxima-mainline-update/tools');import maxima_win_experiment as e
shutil.copy2(b/'controls-budget-checkpoint.json',b/'evidence/controls-acceptance.json')
validated=[]
for n in ['farm-behavior','farm-access']:
 v=json.loads((b/'evidence'/f'{n}.json').read_text());assert v['passed'];validated+=v['validated_switches']
e.atomic(b/'evidence/all-farming-behavior.json',{'protocol_id':accept['report']['protocol_id'],'passed':True,'validated_switches':sorted(set(validated)),'sources':{n:e.sha(b/'evidence'/f'{n}.json') for n in ['farm-behavior','farm-access']}})
g=json.loads((b/'qualification.json').read_text())
for gate,name in [('behavioral','all-farming-behavior'),('positive_control','controls-acceptance')]:g[gate]={'status':'passed','path':f'evidence/{name}.json','sha256':e.sha(b/'evidence'/f'{name}.json')}
e.atomic(b/'qualification.json',g)
# Same bounded budget for all eight, separate protocol/scenarios and no old outcomes.
nextbase=live/'output/maxima-conversion-fixed-confirmation';nextbase.mkdir(exist_ok=False)
s=(live/'output/maxima-farming-sub-switches-confirmation/prepare.py').read_text()
s=s.replace("src=live/'output/maxima-farming-sub-switches-pilot'","src=live/'output/maxima-conversion-fixed-controls'")
s=s.replace("(src/'PLAN.json')","(live/'output/maxima-farming-sub-switches-pilot/PLAN.json')")
s=s.replace("src/'pilot-budget-checkpoint.json'","live/'output/maxima-farming-sub-switches-pilot/pilot-budget-checkpoint.json'")
s=s.replace("(src/'pilot-budget-checkpoint.json')","(live/'output/maxima-farming-sub-switches-pilot/pilot-budget-checkpoint.json')")
s=s.replace("s=(src/'controller.py').read_text()","s=(live/'output/maxima-farming-sub-switches-pilot/controller.py').read_text()")
s=s.replace('glob2-maxima-defense-fix','glob2-maxima-conversion-fixed')
# The template controller replacement above loads an old-root file, adapt it explicitly.
s=s.replace("(b/'controller.py').write_text(s)","s=s.replace('glob2-maxima-defense-fix','glob2-maxima-conversion-fixed')\n(b/'controller.py').write_text(s)")
(nextbase/'prepare.py').write_text(s)
subprocess.run([python,str(nextbase/'prepare.py')],check=True,cwd=live)
with (nextbase/'controller.log').open('w') as log:subprocess.run([python,str(nextbase/'controller.py')],stdout=log,stderr=subprocess.STDOUT,check=True,cwd=live)
