import json,sys,shutil,subprocess
from pathlib import Path
live=Path('/Users/bradley/glob2');sys.path.insert(0,'/Users/bradley/glob2-maxima-mainline-update/tools')
import maxima_win_experiment as e,maxima_win_report as report,maxima_experiment_fleet as fleet
b=Path(__file__).resolve().parent;src=live/'output/maxima-defense-fixed-confirmation'
for name in ['protocol.json','qualification.json','allocations.json']:shutil.copy2(src/name,b/name)
shutil.copytree(src/'evidence',b/'evidence')
p=json.loads((b/'protocol.json').read_text());a=json.loads((b/'farm-access.json').read_text());assert a['passed'] and a['protocol_id']==p['protocol_id']
shutil.copy2(b/'farm-access.json',b/'evidence/farm-access.json')
f=json.loads((b/'evidence/farm-behavior.json').read_text());validated=set(a['validated_switches']+f['validated_switches'])
allkeys=sorted(k for k in p['switches'] if k.startswith('farming.') and k!='farming.enabled')
keys=[k for k in allkeys if k!='farming.farm_protection_enabled'];assert len(keys)==8 and set(keys)<=validated
for defaults in p['defaults'].values():assert all(defaults[k] for k in allkeys) and defaults['farming.enabled']
e.atomic(b/'evidence/all-farming-behavior.json',{'protocol_id':p['protocol_id'],'passed':True,'validated_switches':sorted(validated),'sources':{n:e.sha(b/'evidence'/n) for n in ['farm-access.json','farm-behavior.json']}})
g=json.loads((b/'qualification.json').read_text());g['behavioral']={'status':'passed','path':'evidence/all-farming-behavior.json','sha256':e.sha(b/'evidence/all-farming-behavior.json')};e.atomic(b/'qualification.json',g)
e.atomic(b/'PLAN.json',{'protocol_id':p['protocol_id'],'stage':'pilot','switches':keys,'pairs_per_switch':100,'all_nine_sub_switches':allkeys,'completed_confirmation':'farming.farm_protection_enabled: output/maxima-defense-fixed-confirmation/CONFIRMATION_RESULT.json','reference':'all farming switches ON; change only focal switch in OFF arm','shared_baseline':'identical ON execution reused across comparisons; estimates are correlated across switches','hard_tick_limit':100000,'analysis':'descriptive sizing pilots only; no significance-based stopping or default changes','next':'separately preregister confirmation budgets for every remaining switch after sizing; do not drop switches based on pilot direction','old_outcomes_pooled':False})
m=report.comparisons(p,'pilot',{k:100 for k in keys},validated);alloc=json.loads((b/'allocations.json').read_text());assign=fleet.assign(m,alloc)
for v in assign.values():v['weak']+=v['strong'];v['strong']=[]
for n,v in [('manifest',m),('assignments',assign)]:e.atomic(b/(n+'.json'),v)
e.require_gates(b,p,'pilot')
s=(src/'controller.py').read_text().replace("REMOTE=ROOT/'output/farm-protection-confirmation'","REMOTE=ROOT/'output/farming-sub-switches-pilot'")
s=s[:s.index(" if json.loads((BASE/'STATUS.json')")]+''' if json.loads((BASE/'STATUS.json').read_text()).get('status')=='stage_complete':
  for h in e.HOSTS:fleet.remote(h,ROOT,REMOTE,'stop',value={'reason':'All eight farming pilots complete; sizing checkpoint ready','active_engines':'none'})
except Exception as error:
 e.atomic(BASE/'CONTROLLER_ERROR.json',{'time':time.time(),'error':str(error)});raise
'''
(b/'controller.py').write_text(s)
r='/home/bradley/glob2-maxima-defense-fix/output/farming-sub-switches-pilot';config=str(live/'output/maxima-live-controls-v21/ssh-config');files=[str(x.relative_to(b)) for x in b.rglob('*.json')]
for h in alloc:
 subprocess.run(['ssh','-F',config,h,'mkdir '+r],check=True)
 subprocess.run(['rsync','-az','-e','ssh -F '+config,'--files-from=-',str(b)+'/',h+':'+r+'/'],input='\n'.join(files)+'\n',text=True,check=True)
print(len(m['comparisons']),'comparisons;',len(m['jobs']),'games prepared')
