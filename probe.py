import importlib.util,sys
from pathlib import Path
spec=importlib.util.spec_from_file_location('stack_smoke',Path('test/deployment/platform_stack_smoke.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class Probe:
 def __init__(self,args):
  assert args.sim_version=='fixture-sim'
  assert args.attach=='fixture-project'
  assert args.jobs==4
  self.results={}
 def run(self):return True
m.Smoke=Probe
sys.argv=['platform_stack_smoke.py','--attach','fixture-project','--sim-version','fixture-sim','--jobs','4']
assert m.main()==0
print('PASS: attach, sim-version and jobs parse through main; no deployment invoked')
