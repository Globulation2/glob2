"""Resolve once after all role builds/qualification; no workloads."""
from pathlib import Path
import json,hashlib,importlib.util,collections
O=Path(__file__).resolve().parent
assert not(O/'plan.json').exists(),'Immutableplanalreadyresolved'
p=json.loads((O/'plan-template.json').read_text());v=json.loads((O/'resolved-variants.json').read_text())['variants'];assert len(v)>=2 and len({x['role']for x in v})==len(v)
resolved=json.loads((O/'resolved-variants.json').read_text());excluded=resolved.get('excluded_roles',[]);assert set(p['required_designs'])=={x['role']for x in v}|{x['role']for x in excluded};assert all(x.get('reason')and x.get('qualification_failure_receipt')for x in excluded);p['excluded_roles']=excluded
spec=importlib.util.spec_from_file_location('campaign',O/'runner.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
p['declared_commands']={};p['actual_order_inventory']={}
for regime in ['serial','waves']:
 p['declared_commands'][regime]=[];p['actual_order_inventory'][regime]=[];width=1 if regime=='serial'else 4
 for j,name in enumerate(['Playground','Oazis']):
  for off in range(0,32,width):
   order=m.order_for(v,off//width+j*(32//width));p['actual_order_inventory'][regime].append({'map':name,'wave':off//width,'roles':[x['role']for x in order]})
   for role in order:
    for index in range(off,off+width):
     mask=list(range(32))if regime=='serial'else m.MASKS[index-off];budget='auto'if regime=='serial'else 6
     # Helper command is frozen, source-bound from earlier qualification.
     spec2=importlib.util.spec_from_file_location('helpers',p['shared_launcher']);h=importlib.util.module_from_spec(spec2);spec2.loader.exec_module(h)
     p['declared_commands'][regime].append({'map':name,'seed':p['seeds'][index],'role':role['role'],'command':h.command(role,name,budget,mask),'allocation':mask,'compute':budget,'profile':str((O/regime/name/(str(p['seeds'][index])+'-'+role['role'])/'profile').resolve())})
p['pairwise_order_counts']={}
for regime,inventory in p['actual_order_inventory'].items():
 p['pairwise_order_counts'][regime]={}
 for name in ['Playground','Oazis']:
  orders=[x['roles']for x in inventory if x['map']==name];counts={}
  for a in [x['role']for x in v]:
   for b in [x['role']for x in v]:
    if a==b:continue
    before=sum(order.index(a)<order.index(b)for order in orders);assert before==len(orders)//2;counts[a+' before '+b]=before
  for role in [x['role']for x in v]:assert sum(order.index(role)for order in orders)/len(orders)==(len(v)-1)/2
  p['pairwise_order_counts'][regime][name]=counts
p.update(status='resolved_frozen',roles=[x['role']for x in v],runs_per_regime=64*len(v),total_runs=128*len(v))
(O/'plan.json').write_text(json.dumps(p,indent=2)+'\n')
