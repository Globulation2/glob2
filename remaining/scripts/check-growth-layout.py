from pathlib import Path
import shlex,subprocess,json,re
root=Path.cwd();b=root/'artifacts/resource-growth/remaining';d=b/'layout-check';d.mkdir(exist_ok=True)
s=(root/'src/map/Map.h').read_text();(d/'Map-before.h').write_text(s.replace('\tbool loadedLegacyGrowth144 = false;\n',''));(d/'Map-after.h').write_text(s)
lines=(b/'current-master-build.log').read_text().splitlines();line=next(l for l in lines if l.startswith('/usr/bin/ccache g++ -o build/linux/client/release/src/map/Map.o '));results={}
for tag in ['before','after']:
 p=d/(tag+'.cpp');p.write_text(f'#include "Map-{tag}.h"\n#include <cstddef>\nextern "C" const size_t layout[] = {{sizeof(Map), offsetof(Map, resourceStocks), offsetof(Map, materialSourceCounts), offsetof(Map, idleGradientBuffers)}};\n')
 cmd=shlex.split(line);cmd[cmd.index('-o')+1]=str(p.with_suffix('.s'));cmd[cmd.index('-c')]='-S';cmd[-1]=str(p);cmd.insert(2,'-fno-access-control')
 run=subprocess.run(cmd,cwd=root,capture_output=True,text=True);(d/(tag+'.log')).write_text(run.stdout+run.stderr);assert run.returncode==0
 asm=p.with_suffix('.s').read_text();body=asm.split('layout:',1)[1].split('.size',1)[0];values=re.findall(r'\.quad\s+(\d+)',body);results[tag]=values
assert results['before']==results['after'],results
(d/'results.json').write_text(json.dumps({'fields':['sizeof(Map)','resourceStocks','materialSourceCounts','idleGradientBuffers'],'offsets':results,'match':True},indent=2));print(results)
