import importlib.util,json,tempfile
from pathlib import Path
from unittest.mock import patch
b=Path(__file__).resolve().parent;conf=b.parent/'maxima-repairs-confirmation-v21'
spec=importlib.util.spec_from_file_location('candidate_controller',conf/'controller.py');c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
policy,seq=c.load_amendment();p=json.loads((conf/'protocol.json').read_text());assert p['protocol_id']==policy['protocol_id']
for retired,accepted in [(False,True),(True,False),(True,True)]:
 with tempfile.TemporaryDirectory() as tmp:
  root=Path(tmp);base=root/'confirmation';prior=root/'qualification';old=root/'maxima-repairs-confirmation-v19-resumed'
  for d in [base,prior,old,base/'controls/results']:d.mkdir(parents=True,exist_ok=True)
  for path,value in [(base/'protocol.json',p),(prior/'QUALIFICATION_PASSED.json',{'protocol_id':p['protocol_id']}),(prior/'allocations.json',{}),(prior/'qualification.json',{'protocol_id':p['protocol_id']}),(base/'controls/manifest.json',{})]:c.e.atomic(path,value)
  if retired:c.e.atomic(old/'RETIRED.json',{'restart_allowed':False})
  calls=[];looks=[]
  def batch(*args):calls.append(args[2])
  def look(*args):looks.append(args[-1]);return args[-1]==1000
  with patch.object(c,'BASE',base),patch.object(c,'PRIOR',prior),patch.object(c,'load_amendment',return_value=(policy,seq)),patch.object(c,'run_batch',side_effect=batch),patch.object(c,'scheduled_look',side_effect=look),patch.object(c.report,'controls_checkpoint',return_value={'accepted':accepted}):
   if not retired:
    try:c.main()
    except FileNotFoundError:pass
    else:raise AssertionError('must refuse before retirement')
    assert calls==[]
   else:
    c.main();assert calls==([-200,0,500] if accepted else [-200]);assert looks==([500,1000] if accepted else [])
    q=json.loads((prior/'qualification.json').read_text());assert ('positive_control' in q)==accepted
print('PASS: retirement guard, failed-control stop, accepted-control transition, scheduled stop without extra batch')
