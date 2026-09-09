import json,subprocess,time,shutil,sys
from pathlib import Path
live=Path('/Users/bradley/glob2');src=live/'output/maxima-defense-fixed-controls';base=live/'output/maxima-defense-fixed-confirmation';python='/Users/bradley/glob2/.venv-optimizer/bin/python';config=str(live/'output/maxima-live-controls-v21/ssh-config');sys.path.insert(0,'/Users/bradley/glob2-maxima-mainline-update/tools')
import maxima_win_experiment as e,maxima_win_report as report,maxima_experiment_fleet as fleet
base.mkdir(exist_ok=False);p=json.loads((src/'protocol.json').read_text());switch='farming.farm_protection_enabled'
e.atomic(base/'PLAN.json',{'protocol_id':p['protocol_id'],'stage':'confirmation','switch':switch,'fixed_pairs':1000,'looks':[1000],'alpha':p['hypothesis_alpha'],'hard_tick_limit':100000,'decision':'single final exact paired interval; no outcome-based extension','old_or_pilot_results_pooled':False,'automatic_default_changes':False,'qualification':'wait for corrected-engine controls accepted before dispatch','rationale':'Restart registered bounded confirmation after verified engine memory fix; interrupted old-engine results preserved but excluded'})
for attempt in range(480):
 if (src/'CONTROLLER_ERROR.json').exists():raise RuntimeError('Controls controller failed; investigate')
 done=src/'controls-budget-checkpoint.json'
 if done.exists():
  acceptance=json.loads(done.read_text());assert acceptance['accepted'] and acceptance['all_jobs_complete'];break
 time.sleep(15)
else:raise RuntimeError('Controls still incomplete; inspect without duplicate dispatch')
for name in ['protocol.json','allocations.json','qualification.json']:shutil.copy2(src/name,base/name)
shutil.copytree(src/'evidence',base/'evidence');shutil.copy2(done,base/'evidence/controls-acceptance.json')
g=json.loads((base/'qualification.json').read_text())
for gate,name in [('positive_control','controls-acceptance'),('behavioral','farm-behavior')]:g[gate]={'status':'passed','path':f'evidence/{name}.json','sha256':e.sha(base/f'evidence/{name}.json')}
e.atomic(base/'qualification.json',g)
m=report.comparisons(p,'confirmation',{switch:1000},[switch]);assign=fleet.assign(m,json.loads((base/'allocations.json').read_text()))
for v in assign.values():v['weak']+=v['strong'];v['strong']=[]
for name,v in [('manifest',m),('assignments',assign),('allocation',{'protocol_id':p['protocol_id'],'approved':True,'authorization':'user directed autonomous recovery and continued tournament','fixed_pairs':1000,'slots':48})]:e.atomic(base/(name+'.json'),v)
e.require_gates(base,p,'confirmation')
s=(live/'output/maxima-farm-protection-confirmation-100k/controller.py').read_text().replace('glob2-maxima-mainline-100k-final','glob2-maxima-defense-fix').replace("REMOTE=ROOT/'output/farm-protection-confirmation-100k'","REMOTE=ROOT/'output/farm-protection-confirmation'");(base/'controller.py').write_text(s)
r='/home/bradley/glob2-maxima-defense-fix/output/farm-protection-confirmation';files=[str(f.relative_to(base)) for f in base.rglob('*.json')]
for h in assign:
 subprocess.run(['ssh','-F',config,h,'mkdir '+r],check=True)
 subprocess.run(['rsync','-az','-e','ssh -F '+config,'--files-from=-',str(base)+'/',h+':'+r+'/'],input='\n'.join(files)+'\n',text=True,check=True)
with (base/'controller.log').open('w') as log:subprocess.run([python,str(base/'controller.py')],cwd=live,stdout=log,stderr=subprocess.STDOUT,check=True)
