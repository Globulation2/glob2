from pathlib import Path
import shlex,subprocess,json
root=Path.cwd();out=root/'artifacts/resource-growth/remaining';cwd=root/'artifacts/resource-growth/baseline-src'
s=(cwd/'src/map/ResourceGrowthFixtures.cpp').read_text().replace('"saturated", 512','"saturated", 256').replace('{"multi", 512}','{"fragmented", 512}').replace('scenario == std::string("multi")','scenario == std::string("fragmented")')
s=s.replace('const int stride = scenario == std::string("sparse") ? 8 : 2;', 'const int stride = scenario == std::string("sparse") ? 8 : 2;\n        MersenneTwister layout(951);')
s=s.replace('map.setResource(x, y, id, 0);','''if (scenario == std::string("fragmented") && layout()%4) continue;
                map.setResource(x, y, id, 0);''')
p=out/'ResourceGrowthFixtures.cpp';p.write_text(s)
lines=(root/'artifacts/resource-growth/fixture-generator-build.log').read_text().splitlines();objname='build/linux/client/release/test/engine-src_map_ResourceGrowthFixtures.o'
cmd=shlex.split(next(l for l in lines if l.startswith('/usr/bin/ccache g++ -o '+objname+' ')));cmd[cmd.index('-o')+1]=str(p.with_suffix('.o'));cmd[-1]=str(p)
link=shlex.split(next(l for l in reversed(lines) if l.startswith('g++ -o build/linux/client/release/test/glob2-engine-tests ')));link[2]=str(out/'fixture-generator');link=[str(p.with_suffix('.o')) if a==objname else a for a in link]
with (out/'fixtures-build.log').open('w') as log:
 for c in [cmd,link]:subprocess.run(c,cwd=cwd,stdout=log,stderr=subprocess.STDOUT,check=True)
(out/'fixture-build-commands.json').write_text(json.dumps([cmd,link],indent=2))
