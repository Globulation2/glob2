from pathlib import Path
import subprocess,json
out=Path('artifacts/hazard-integrated/gcc11-baseline');out.mkdir(parents=True,exist_ok=True)
source=out/'probe.cpp';source.write_text('#include "map/TerrainExperiments.h"\nint main() { return int(TERRAIN_EXPERIMENTS.size()); }\n')
includes=['src','src/game','src/map','src/resource','libgag/include']
external=['-I/tmp/glob2-terrain-sdl-patched/prefix/include','-I/tmp/glob2-sdl3/prefix/include']
flags=['-std=gnu++20',*[f'-I{p}' for p in includes],*external]
deps=subprocess.check_output(['g++-11',*flags,'-MM',str(source)],text=True).replace('\\\n',' ').split()[1:]
files=[f for f in deps if f.startswith(('src/','libgag/'))]
for f in files:
 p=out/'master'/f;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(subprocess.check_output(['git','show','origin/master:'+f]))
results={}
for label,inc in [('current',Path('.')),('master',out/'master')]:
 cmd=['g++-11','-std=gnu++20',*[f'-I{inc/p}' for p in includes],*external,'-fsyntax-only',str(source)]
 r=subprocess.run(cmd,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
 (out/(label+'.log')).write_text(r.stdout);results[label]={'command':cmd,'exit':r.returncode,'same_error':'TERRAIN_GROUP_EXPERIMENTS' in r.stdout and 'constant expression' in r.stdout}
results['identical_dependency_files']={f:Path(f).read_bytes()==(out/'master'/f).read_bytes() for f in files}
(out/'result.json').write_text(json.dumps(results,indent=2)+'\n');print(json.dumps(results,indent=2))
