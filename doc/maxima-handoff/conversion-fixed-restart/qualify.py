import concurrent.futures,json,subprocess,sys,os,shutil
from pathlib import Path
root=Path('/home/bradley/glob2-maxima-conversion-fixed');old=Path('/home/bradley/glob2-maxima-defense-fix');diag=Path('/home/bradley/glob2-unit-conversion-diagnostic')
if root.exists():assert not any(root.iterdir()), 'refuse to overwrite populated root'
else:root.mkdir()
shutil.copytree(old,root,dirs_exist_ok=True,ignore=shutil.ignore_patterns('output','.git','*.log','fleet.tar.gz'))
shutil.copy2(diag/'Misc.fixed.cpp',root/'src/building/Misc.cpp');shutil.copy2(diag/'Misc.fixed.o',root/'build-portable/src/building/Misc.o');shutil.copy2(diag/'glob2.fixed',root/'build-portable/src/glob2')
os.chdir(root);sys.path.insert(0,str(root/'tools'));import maxima_win_experiment as e
out=root/'output/qualification';p=e.prepare(root/'build-portable/src/glob2',out);assert p['hard_tick_limit']==100000
manifest=json.loads((old/'output/farming-sub-switches-confirmation/manifest.json').read_text());(out/'known-long.json').write_text(json.dumps(manifest['jobs'][0]))
commands={
 'routing':['taskset','-c','0','python3','tools/qualify_maxima_win_experiment.py',str(out)],
 'no-orders':['taskset','-c','1','python3','tools/qualify_maxima_no_orders.py',str(out),'--output',str(out/'no-orders')],
 'tick-limit':['taskset','-c','2','python3','tools/qualify_maxima_tick_limit.py',str(out),'--known-long-case',str(out/'known-long.json'),'--cpu','2'],
 'farm-behavior':['python3','tools/qualify_maxima_farm_maintenance_behavior.py',str(out),'--output',str(out/'farm-behavior'),'--build','build-portable','--cpu','3'],
 'farm-access':['python3','tools/qualify_maxima_farm_access_behavior.py',str(out),'--output',str(out/'farm-access'),'--build','build-portable','--cpu','4']}
def run(item):
 name,cmd=item
 with (out/(name+'.log')).open('w') as log:r=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT)
 e.atomic(out/(name+'-exit.json'),{'exit':r.returncode});return name,r.returncode
with concurrent.futures.ThreadPoolExecutor(max_workers=5) as pool:results=dict(pool.map(run,commands.items()))
e.atomic(out/'QUALIFICATION_STATUS.json',{'protocol_id':p['protocol_id'],'passed':all(v==0 for v in results.values()),'results':results,'next':'four-host continuation and fresh positive controls required before eight-switch confirmation'})
assert all(v==0 for v in results.values()),results
