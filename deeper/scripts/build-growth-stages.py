from pathlib import Path
import subprocess,shlex,json,sys,re
root=Path.cwd();r=root/'artifacts/resource-growth/deeper';mode=sys.argv[1]
cwd=root/'artifacts/resource-growth/baseline-src' if mode=='legacy' else root
log=root/'artifacts/resource-growth'/('baseline-build.log' if mode=='legacy' else 'candidate-build-final.log')
lines=log.read_text().splitlines()
if mode!='legacy': lines += (r/'layout-build.log').read_text().splitlines()
overrides={};commands=[]
files={'src/engine/sim/snapshot/SnapshotStore.cpp':[('Handle Store::captureBoundary(',0)],'src/map/MapStep.cpp':[('void Map::growResources(',1)],'src/map/gradient/SnapshotGradient.cpp':[('void seed(',5),('void propagate(',6)],'src/ai/engine/AIPipeline.cpp':[],'src/app/cli/Headless.cpp':[]}
if mode!='legacy':files['src/map/ResourceGrowth.cpp']=[('void calculate(',2),('void apply(',3),('void Pipeline::prepare(',4)]
for name,functions in files.items():
 s=(cwd/name).read_text()
 for prefix,slot in functions:
  pos=s.index(prefix);brace=s.index('\n{',pos)+2;s=s[:brace]+f'\n StageProbe::Scope stageProbe({slot});'+s[brace:]
 if name.endswith('AIPipeline.cpp'):
  needle='std::vector<ResourceEnrollmentRequest> enrollments;';assert needle in s;s=s.replace(needle,'StageProbe::Scope stageProbe(7); '+needle)
 if name.endswith('Headless.cpp'):
  s=s.replace('const auto runStart =','StageProbe::begin();\n        const auto runStart =',1)
  s=s.replace('const auto runEnd = std::chrono::steady_clock::now();','const auto runEnd = std::chrono::steady_clock::now();\n        StageProbe::finish();',1)
 p=r/(mode+'-'+Path(name).name);p.write_text('#include "StageProbe.h"\n'+s)
 objectname='build/linux/client/release/'+name[:-4]+'.o'
 line=next(l for l in reversed(lines) if l.startswith('/usr/bin/ccache g++ -o '+objectname+' '))
 cmd=shlex.split(line);obj=r/(mode+'-'+Path(name).stem+'.o');cmd[cmd.index('-o')+1]=str(obj);cmd[-1]=str(p)
 # Sources outside their original directory still need their local headers.
 cmd.insert(cmd.index('-c'),'-I'+str((cwd/name).parent))
 with (r/(mode+'-stages-build.log')).open('a') as f:subprocess.run(cmd,cwd=cwd,stdout=f,stderr=subprocess.STDOUT,check=True)
 overrides[objectname]=str(obj);commands.append(cmd)
link=json.loads((root/'artifacts/resource-growth/profiling'/(('legacy' if mode=='legacy' else 'candidate')+'-relink-command.json')).read_text());link[2]=str(r/(mode+'-stages'));link=[overrides.get(a,a) for a in link]
with (r/(mode+'-stages-build.log')).open('a') as f:subprocess.run(link,cwd=cwd,stdout=f,stderr=subprocess.STDOUT,check=True)
(r/(mode+'-stages-commands.json')).write_text(json.dumps({'compile':commands,'link':link,'cwd':str(cwd)},indent=2));print(mode,'CPU stage probe ready')
