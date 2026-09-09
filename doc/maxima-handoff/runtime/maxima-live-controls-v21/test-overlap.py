import importlib.util,json,tempfile
from pathlib import Path
from unittest.mock import patch
b=Path(__file__).resolve().parent;cpath=b.parent/'maxima-repairs-confirmation-v21'
spec=importlib.util.spec_from_file_location('overlap',cpath/'controller.py');c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
with tempfile.TemporaryDirectory() as tmp:
 local=Path(tmp);(local/'assignments.json').write_text(json.dumps({'idle':{'weak':['a'],'strong':[]},'busy':{'weak':['b'],'strong':[]}}))
 probes={'idle':{'stop':None,'jobs':[{'id':'a','status':'complete'}]},'busy':{'stop':None,'jobs':[{'id':'b','status':'running'}]}}
 calls=[]
 def remote(h,root,out,mode,*args):
  calls.append((h,mode));return {'stop':None,'jobs':[],'summary':{}}
 with patch.object(c,'prepare',return_value=(local,local,{})) as prepare,patch.object(c.fleet,'remote',side_effect=remote):
  for start in [-200,500,1500,3500,7500,13500,20500]:
   assert c.advance_idle_hosts({}, {},start,local,probes)=={}
  assert not calls and not prepare.called
  c.advance_idle_hosts({}, {},0,local,probes)
  assert all(h=='idle' for h,mode in calls) and ('idle','start') in calls
  assert prepare.call_args.args[-1]==500
 with patch.object(c,'prepare',return_value=(local,local,{})),patch.object(c.fleet,'remote',return_value={'stop':None,'jobs':[{'id':'new','status':'running'}],'summary':{'running':1}}) as remote:
  c.advance_idle_hosts({}, {},0,local,probes);assert remote.call_count==1 # no restart or duplicate dispatch
 probes['idle']['jobs']=[]
 with patch.object(c,'prepare') as prepare:assert c.advance_idle_hosts({}, {},0,local,probes)=={};prepare.assert_not_called()
 probes['idle']['jobs']=[{'id':'a','status':'complete'}]
 with patch.object(c,'prepare',return_value=(local,local,{})),patch.object(c.fleet,'remote',return_value={'stop':{'reason':'failure'},'jobs':[],'summary':{}}):
  try:c.advance_idle_hosts({}, {},0,local,probes)
  except RuntimeError:pass
  else:raise AssertionError('must stop on prefetched failure')
s=(b/'test-controller.py').read_text().replace("conf/'controller.py'", "conf/'controller.py'");exec(compile(s,'transition-test','exec'),{'__file__':str(b/'test-controller.py')})
print('PASS: no overlap across controls or any registered look; busy hosts excluded; completed assignments required; active next jobs not restarted; faults stop dispatch')
