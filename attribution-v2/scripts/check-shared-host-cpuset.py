import importlib.util
from pathlib import Path
spec=importlib.util.spec_from_file_location('wrapper','docs/.work/run_with_shared_host_cpuset.py');w=importlib.util.module_from_spec(spec);spec.loader.exec_module(w)
class System:
 def __init__(self):
  self.root={'cgroup.subtree_control':'cpuset cpu','cpuset.cpus.effective':'4-7,20-23','cpuset.mems.effective':'0'}
  self.group={'cpuset.cpus.partition':'root','cpuset.cpus':'0-3,16-19','cpuset.cpus.effective':'0-3,16-19','cpuset.cpus.exclusive':'0-3,16-19','cpuset.cpus.exclusive.effective':'0-3,16-19','cpuset.mems.effective':'0'}
 def read(self,p):return (self.root if p.parent==w.CGROUP_ROOT else self.group)[p.name]
s=System();p=w.Partition(s,{0,1,2,3},{0,1,2,3,16,17,18,19},w.CGROUP_ROOT/'glob2-benchmark-local-check');p.before={'cgroup.subtree_control':'cpuset cpu','cpuset.cpus.effective':'0-31','cpuset.mems.effective':'0'}
p.validate()
checks=1
for key,value in [('cpuset.cpus.partition','member'),('cpuset.cpus.exclusive.effective','0-2,16-19'),('cpuset.mems.effective','1')]:
 old=s.group[key];s.group[key]=value
 try:p.validate();raise AssertionError('must reject '+key)
 except RuntimeError:checks+=1
 s.group[key]=old
s.root['cpuset.cpus.effective']='0-7,20-23'
try:p.validate();raise AssertionError('must reject overlap')
except RuntimeError:checks+=1
print(checks,'local reservation contract checks passed')
