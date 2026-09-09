import json,sys,shutil,subprocess,os,runpy
from pathlib import Path
ROOT=Path('/Users/bradley/glob2-maxima-mainline-update');sys.path.insert(0,str(ROOT/'tools'))
import maxima_win_experiment as e, maxima_win_report as report,maxima_experiment_fleet as fleet
b=Path(__file__).resolve().parent;p=json.loads((b/'protocol.json').read_text());assert p['hard_tick_limit']==100000
for name in ['tools/maxima_win_statistics.py','tools/maxima_win_report.py','tools/maxima_experiment_fleet.py','tools/maxima_win_experiment.py']:assert e.sha(ROOT/name)==p['runtime_and_analysis'][name]
shutil.copy2('/Users/bradley/glob2/output/maxima-mainline-transition/FLEET_PASS.json',b/'evidence/fleet.json')
r=json.loads((b/'evidence/routing.json').read_text());assert len(r)==40 and all(v['status']=='passed' for v in r)
assert all(json.loads((b/'evidence/determinism.json').read_text()).values())
for name in ['no-orders','tick-limit','farm-behavior']:
 v=json.loads((b/f'evidence/{name}.json').read_text());assert v['passed'] and v['protocol_id']==p['protocol_id']
v=json.loads((b/'evidence/fleet.json').read_text());assert v['passed'] and len(v['hosts'])==4 and all(x['protocol_id']==p['protocol_id'] for x in v['hosts'].values())
assert json.loads((b/'evidence/statistical-sensitivity.json').read_text())['error_control_passed']
gates={'protocol_id':p['protocol_id']}
for gate,name in {'routing':'routing','opponent_identity':'routing','player_isolation':'no-orders','save_load':'no-orders','behavioral':'no-orders','reproducibility_across_hosts':'fleet','statistical_sensitivity':'statistical-sensitivity','no_orders':'no-orders','population_scoring':'tick-limit'}.items():
 path='evidence/'+name+'.json';gates[gate]={'status':'passed','path':path,'sha256':e.sha(b/path)}
e.atomic(b/'qualification.json',gates)
e.atomic(b/'PLAN.json',{'protocol_id':p['protocol_id'],'stage':'controls','pairs':200,'hard_tick_limit':100000,'treatment':'full focal AI off versus baseline','no_inference_about_switches':True,'acceptance':'all receipts/repeats valid and baseline-minus-disabled 95% lower bound > 0','next_switch_candidate':'farming.farm_protection_enabled','automatic_next_stage':False,'old_outcomes_pooled':False})
allocation=json.loads(Path('/Users/bradley/glob2/output/maxima-live-controls-v21/allocations.json').read_text())
m=report.comparisons(p,'controls',{'no_orders':200},[],{e.NO_ORDERS:True});assign=fleet.assign(m,allocation)
for a in assign.values():a['weak']+=a['strong'];a['strong']=[]
for name,v in [('manifest',m),('assignments',assign),('allocations',allocation)]:e.atomic(b/(name+'.json'),v)
e.require_gates(b,p,'controls')
remote=Path('/home/bradley/glob2-maxima-mainline-100k-final/output/fresh-controls-100k');config='/Users/bradley/glob2/output/maxima-live-controls-v21/ssh-config'
files=['protocol.json','qualification.json','PLAN.json','manifest.json','assignments.json','allocations.json']+[str(f.relative_to(b)) for f in (b/'evidence').glob('*.json')]
for host in allocation:
 subprocess.run(['ssh','-F',config,host,'mkdir -p '+str(remote)],check=True)
 subprocess.run(['rsync','-az','-e','ssh -F '+config,'--files-from=-',str(b)+'/',host+':'+str(remote)+'/'],input='\n'.join(files)+'\n',text=True,check=True)
print('Prepared',len(m['comparisons']),'pairs',len(m['jobs']),'executions; all gates validated')
