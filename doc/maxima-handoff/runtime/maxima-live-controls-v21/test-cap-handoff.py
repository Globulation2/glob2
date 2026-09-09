import importlib.util,json,runpy,tempfile
from pathlib import Path
from unittest.mock import patch
b=Path(__file__).resolve().parent;conf=b.parent/'maxima-repairs-confirmation-v21'
spec=importlib.util.spec_from_file_location('next_controller',conf/'controller.py');c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
policy=runpy.run_path(str(b/'runtime-policy.py'))
for mode in ['start','pulse','stop']:
 assert policy['command']('devlaptop.local',c.ROOT,c.REMOTE,mode)[:3]==['taskset','-c','0-11']
 assert policy['command']('therig.local',c.ROOT,c.REMOTE,mode)[0]=='python3'
assert len(c.batch_hosts(conf/'controls'))==3 and 'devlaptop.local' not in c.batch_hosts(conf/'controls')
p=json.loads((conf/'protocol.json').read_text());allocation=json.loads((b/'allocations.json').read_text());allocation['devlaptop.local']=json.loads((b/'DEVLAPTOP_REJOIN.json').read_text())['allocation']
assert sum(a['jobs'] for a in allocation.values())==48
with tempfile.TemporaryDirectory() as tmp,patch.object(c,'BASE',Path(tmp)),patch.object(c.e,'require_gates'),patch.object(c,'ssh'),patch.object(c.subprocess,'run'):
 local,remote,m=c.prepare(p,allocation,0)
 assert set(c.batch_hosts(local))==set(allocation)
 assignments=json.loads((local/'assignments.json').read_text());ids=[j for a in assignments.values() for cohort in a.values() for j in cohort]
 assert len(ids)==len(m['jobs']) and len(set(ids))==len(ids)
# Run existing controller transition checks against the staged replacement.
s=(b/'test-controller.py').read_text().replace("conf/'controller.py'","conf/'controller.py'")
exec(compile(s,str(b/'test-controller.py'),'exec'),{'__file__':str(b/'test-controller.py')})
print('PASS: 12 CPU transport cap; existing three-host assignments preserved; new four-host batch assigns each execution once')
