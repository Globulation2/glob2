import json,subprocess,shlex
from pathlib import Path
root=Path.cwd();out=root/'artifacts/gradient-cpu-audit'
commands=json.loads((out/'build-link-commands.json').read_text());commands['candidate']=commands['fixed']
# The before archive has its own matching objects. The after archive retains
# production-fix objects; compile matching CellRules for each probe layout.
for v in ('before','master','fixed','candidate'):
 archive=root/('artifacts/gradient-cpu-ab/before' if v=='before' else 'artifacts/gradient-cpu-ab/after')
 overlay=archive/'src' if v in ('before','candidate') else out/(v+'-source')/'src'
 compile_cwd=archive if v in ('before','candidate') else overlay.parent
 includes=[('-I'+str(archive/x[2:])) if x.startswith('-Ibuild/') else x for x in json.loads((out/'compile-probe-flags.json').read_text())]
 flags=['g++','-std=c++20','-O3','-g1','-DNDEBUG',*(['-DAUDIT_BEFORE_BACKENDS'] if v=='before' else []),*includes]
 probe=out/(v+'-lifecycle.o');cmd=[*flags,'-c',str(out/'lifecycle-probe.cpp'),'-o',str(probe)]
 subprocess.run(cmd,cwd=compile_cwd,check=True)
 link=commands[v].copy();link[link.index('-o')+1]=str(out/(v+'-lifecycle'))
 link=[x for x in link if x!='-s' and not x.endswith('src/app/Glob2.o')]
 if v!='before':
  rules=out/(v+'-CellRules.o');subprocess.run([*flags,'-c',str(overlay/'map/CellRules.cpp'),'-o',str(rules)],cwd=compile_cwd,check=True)
  link=[str(rules) if x.endswith('src/map/CellRules.o') else x for x in link]
  storage=out/(v+'-SnapshotStorage.o')
  subprocess.run([*flags,'-c',str(overlay/'engine/sim/snapshot/SnapshotStorage.cpp'),'-o',str(storage)],cwd=compile_cwd,check=True)
  link=[str(storage) if x.endswith('src/engine/sim/snapshot/SnapshotStorage.o') else x for x in link]
 link.insert(1,str(probe));subprocess.run(link,cwd=archive,check=True)
 print(v,'built',flush=True)
