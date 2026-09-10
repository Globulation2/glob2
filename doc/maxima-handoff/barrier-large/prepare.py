import json,shutil,sys
from pathlib import Path
b=Path(__file__).resolve().parent;live=Path('/Users/bradley/glob2');src=live/'output/maxima-conversion-fixed-controls'
shutil.copytree(live/'output/maxima-farming-sub-switches-confirmation/runtime',b/'runtime');shutil.copy2(live/'output/maxima-live-controls-v21/runtime-policy.py',b/'runtime-policy.py')
sys.path.insert(0,str(b/'runtime'));import maxima_win_experiment as e
for n in ['protocol.json','qualification.json','allocations.json']:shutil.copy2(src/n,b/n)
shutil.copytree(src/'evidence',b/'evidence');shutil.copy2(b/'controls-acceptance.json',b/'evidence/controls-acceptance.json')
p=json.loads((b/'protocol.json').read_text());g=json.loads((b/'qualification.json').read_text());a=json.loads((b/'evidence/farm-access.json').read_text());key='farming.barrier_topology_enabled'
assert a['passed'] and a['protocol_id']==p['protocol_id'] and key in a['validated_switches']
for gate,name in [('behavioral','farm-access'),('positive_control','controls-acceptance')]:g[gate]={'status':'passed','path':f'evidence/{name}.json','sha256':e.sha(b/'evidence'/f'{name}.json')}
e.atomic(b/'qualification.json',g)
pilot=json.loads((live/'output/maxima-farming-sub-switches-pilot/pilot-budget-checkpoint.json').read_text());budget=next(x for x in pilot['budgets'] if x['switch']==key);assert budget['sizing']['required_pairs']==21000
e.atomic(b/'evidence/pilot-sizing.json',budget)
e.atomic(b/'PLAN.json',{'protocol_id':p['protocol_id'],'switch':key,'fixed_pairs':21000,'scenario_indices':[2000,22999],'batch_pairs':500,'batches':42,'looks':[21000],'alpha':p['hypothesis_alpha'],'purpose':'detect two-percentage-point ON-minus-OFF effects with at least90% estimated power under pilot variance bounds; significance not guaranteed','sizing_source':'evidence/pilot-sizing.json','reference':'all other farming sub-switches and master ON; focal barrier switch only ON/OFF','hard_tick_limit':100000,'old_or_pilot_outcomes_pooled':False,'outcome_based_extension':False,'automatic_default_changes':False,'batch_policy':'operational batches only; no per-batch statistical analysis or significance stopping','failure_policy':'preserve all receipts and failed identities; no silent retries or full-run discard. Investigate new faults, use conservative missing-outcome bounds if needed and label any departure.','authorization':'user explicitly requests larger barrier-only experiment for statistical certainty'})
e.atomic(b/'allocation.json',{'protocol_id':p['protocol_id'],'approved':True,'fixed_pairs':21000,'authorization':'explicit user request for larger barrier-only test'})
e.require_gates(b,p,'confirmation')
e.atomic(b/'runtime-hashes.json',{f.name:e.sha(f) for f in (b/'runtime').glob('*.py')})
print('Registered21000 pairs in42 operational batches; controls and behavior gates passed')
