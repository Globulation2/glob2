import json,sys,shutil,subprocess
from pathlib import Path
from unittest.mock import patch
live=Path('/Users/bradley/glob2');sys.path.insert(0,'/Users/bradley/glob2-maxima-mainline-update/tools')
import maxima_win_experiment as e,maxima_win_report as report,maxima_experiment_fleet as fleet
b=Path(__file__).resolve().parent;src=live/'output/maxima-farming-sub-switches-pilot'
assert json.loads((src/'STATUS.json').read_text())['status']=='stage_complete'
for n in ['protocol.json','qualification.json','allocations.json']:shutil.copy2(src/n,b/n)
shutil.copytree(src/'evidence',b/'evidence');shutil.copy2(src/'pilot-budget-checkpoint.json',b/'evidence/pilot-sizing.json')
p=json.loads((b/'protocol.json').read_text());keys=json.loads((src/'PLAN.json').read_text())['switches'];assert len(keys)==8
validated=json.loads((b/'evidence/all-farming-behavior.json').read_text())['validated_switches'];assert set(keys)<=set(validated)
plan={'protocol_id':p['protocol_id'],'stage':'confirmation','switches':keys,'fixed_pairs_per_switch':1000,'scenario_indices':[1000,1999],'looks':[1000],'alpha_per_switch':p['hypothesis_alpha'],'hard_tick_limit':100000,'purpose':'bounded confirmation of larger effects for every remaining switch; not 90% power for two-point effects','rationale':'Same bounded 1000-pair allocation as completed farm-protection confirmation, applied to all eight regardless of pilot direction','pilot_sizing_pairs':{v['switch']:v['sizing']['required_pairs'] for v in json.loads((src/'pilot-budget-checkpoint.json').read_text())['budgets']},'shared_baseline':'identical ON executions shared across eight comparisons; cross-switch estimates correlated','old_outcomes_pooled':False,'pilot_outcomes_pooled':False,'outcome_based_extension':False,'automatic_default_changes':False,'authorization':'standing user instruction to independently test all farming sub-switches and keep advancing'}
e.atomic(b/'PLAN.json',plan)
original=e.scenario
with patch.object(e,'scenario',side_effect=lambda protocol,stage,index,balanced=False:original(protocol,stage,index+1000,balanced)):
 m=report.comparisons(p,'confirmation',{k:1000 for k in keys},validated)
old=json.loads((live/'output/maxima-defense-fixed-confirmation/manifest.json').read_text());oldids={j['execution_id'] for j in old['jobs']};assert not oldids.intersection(j['execution_id'] for j in m['jobs'])
assert len({r['scenario_id'] for r in m['comparisons']})==1000
alloc=json.loads((b/'allocations.json').read_text());a=fleet.assign(m,alloc)
for v in a.values():v['weak']+=v['strong'];v['strong']=[]
for n,v in [('manifest',m),('assignments',a),('allocation',{'protocol_id':p['protocol_id'],'approved':True,'fixed_pairs':{k:1000 for k in keys},'authorization':plan['authorization']})]:e.atomic(b/(n+'.json'),v)
e.require_gates(b,p,'confirmation')
# Preserve controller modules locally so concurrent branch cleanup cannot change this run.
runtime=b/'runtime';runtime.mkdir()
for f in Path('/Users/bradley/glob2-maxima-mainline-update/tools').glob('maxima*.py'):shutil.copy2(f,runtime/f.name)
s=(src/'controller.py').read_text().replace("'/Users/bradley/glob2-maxima-mainline-update/tools'",repr(str(runtime))).replace("REMOTE=ROOT/'output/farming-sub-switches-pilot'","REMOTE=ROOT/'output/farming-sub-switches-confirmation'").replace('All eight farming pilots complete; sizing checkpoint ready','All eight fixed confirmations complete; final report ready')
(b/'controller.py').write_text(s)
e.atomic(b/'runtime-hashes.json',{f.name:e.sha(f) for f in runtime.glob('*.py')})
remote='/home/bradley/glob2-maxima-defense-fix/output/farming-sub-switches-confirmation';cfg=str(live/'output/maxima-live-controls-v21/ssh-config');files=[str(f.relative_to(b)) for f in b.rglob('*.json')]
for h in alloc:
 subprocess.run(['ssh','-F',cfg,h,'mkdir '+remote],check=True)
 subprocess.run(['rsync','-az','-e','ssh -F '+cfg,'--files-from=-',str(b)+'/',h+':'+remote+'/'],input='\n'.join(files)+'\n',text=True,check=True)
print(len(m['comparisons']),'pairs;',len(m['jobs']),'unique games')
