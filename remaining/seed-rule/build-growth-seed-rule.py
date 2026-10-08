from pathlib import Path
import shlex,subprocess,json,hashlib
root=Path.cwd();out=root/'artifacts/resource-growth/remaining/seed-rule'
logs=[root/'artifacts/resource-growth/remaining/player-free-build-2.log',root/'artifacts/resource-growth/remaining/delay-ablation/build.log']
lines='\n'.join(p.read_text() for p in logs).splitlines()
def command(prefix):return shlex.split(next(l for l in reversed(lines) if l.startswith(prefix)))
source=(root/'src/map/ResourceGrowth.cpp').read_text()
source=source.replace('const auto first = out.proposals.size();', '''if (target.resource.type == NO_RES_TYPE)
        {
            out.proposals.push_back({Uint32(i), source.type, 255, 1});
            return;
        }''')
a=source.index('\t\t// If no material draw succeeds');b=source.index('\n\t};',a);source=source[:a]+source[b:]
a=source.index('void apply(Map &map,');b=source.index('\nSimulationSnapshot::Requirements Pipeline::requirements()',a)
source=source[:a]+(root/'docs/.work/seed-rule-apply.cpp').read_text()+source[b:]
(out/'ResourceGrowth.cpp').write_text(source)
obj='build/linux/client/release/src/map/ResourceGrowth.o'
compile=command('/usr/bin/ccache g++ -o '+obj+' ');compile[compile.index('-o')+1]=str(out/'candidate.o');compile[-1]=str(out/'ResourceGrowth.cpp');compile.insert(compile.index('-c'),'-I'+str(root/'src/map'))
harness=command('/usr/bin/ccache g++ -o build/linux/client/release/test/engine-src_map_ResourceGrowthBenchmark.o ');harness[harness.index('-o')+1]=str(out/'harness.o');harness[-1]=str(root/'docs/.work/seed-rule-harness.cpp');harness.insert(harness.index('-c'),'-I'+str(root/'src/map'))
link=command('g++ -o build/linux/client/release/test/glob2-engine-tests ');link=[str(out/'harness.o') if a=='build/linux/client/release/test/engine-src_map_ResourceGrowthBenchmark.o' else a for a in link];link[2]=str(out/'baseline')
candidate=[str(out/'candidate.o') if a==obj else a for a in link];candidate[2]=str(out/'candidate')
with (out/'build.log').open('w') as log:
 for cmd in [harness,compile,link,candidate]:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True)
(out/'commands.json').write_text(json.dumps([harness,compile,link,candidate],indent=2)+'\n')
(out/'build-identity.json').write_text(json.dumps({'commit':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'hashes':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [out/'baseline',out/'candidate',out/'ResourceGrowth.cpp',root/'docs/.work/seed-rule-harness.cpp']},'note':'Ignored prototype: material255 seed marker is NOT supported by shipping save/replay formats; no adoption or performance claim.'},indent=2)+'\n')
