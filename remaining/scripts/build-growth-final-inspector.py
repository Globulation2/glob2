from pathlib import Path
import shlex,subprocess,json
root=Path.cwd();b=root/'artifacts/resource-growth/remaining'
lines=(b/'latest-integration-build.log').read_text().splitlines();name='src/map/ResourceGrowthBenchmark.cpp';obj='build/linux/client/release/test/engine-src_map_ResourceGrowthBenchmark.o'
s=(root/name).read_text();extra=(root/'docs/.work/remaining-component.cpp').read_text();extra=extra[extra.index('TEST_CASE("inspect remaining'):];s+='\n#include <Engine.h>\n'+extra
p=b/'final-inspector.cpp';p.write_text(s)
cmd=shlex.split(next(l for l in reversed(lines) if l.startswith('/usr/bin/ccache g++ -o '+obj+' ')));cmd[cmd.index('-o')+1]=str(p.with_suffix('.o'));cmd[-1]=str(p);cmd.insert(cmd.index('-c'),'-I'+str(root/'src/map'))
link=shlex.split(next(l for l in reversed(lines) if l.startswith('g++ -o build/linux/client/release/test/glob2-engine-tests ')));link[2]=str(b/'final-inspector');link=[str(p.with_suffix('.o')) if a==obj else a for a in link]
with (b/'final-inspector-build.log').open('w') as f:
 for c in [cmd,link]:subprocess.run(c,stdout=f,stderr=subprocess.STDOUT,check=True)
(b/'final-inspector-build-commands.json').write_text(json.dumps([cmd,link],indent=2))
