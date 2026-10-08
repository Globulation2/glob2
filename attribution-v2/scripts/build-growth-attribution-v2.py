from pathlib import Path
import json,shlex,subprocess
root=Path.cwd();out=root/'artifacts/resource-growth/attribution-v2'
logs=['build.log','final-build.log','integrated-build.log','revision-build.log']
lines=sum(((root/'artifacts/resource-growth/simple'/n).read_text().splitlines() for n in logs),[])
commands=[]
def compile_source(name,text,tag):
 objname=('build/linux/client/release/test/engine-src_map_ResourceGrowthBenchmark.o' if name.endswith('Benchmark.cpp') else 'build/linux/client/release/'+name[:-4]+'.o')
 cmd=shlex.split(next(l for l in reversed(lines) if l.startswith('/usr/bin/ccache g++ -o '+objname+' ')))
 src=out/(tag+'-'+Path(name).name);src.write_text(text);obj=src.with_suffix('.o')
 cmd[cmd.index('-o')+1]=str(obj);cmd[-1]=str(src);cmd.insert(cmd.index('-c'),'-I'+str((root/name).parent))
 with (out/'build.log').open('a') as f: subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True)
 commands.append(cmd);return objname,str(obj)
def link_binary(target,tag,overrides):
 cmd=shlex.split(next(l for l in reversed(lines) if l.startswith('g++ -o '+target+' ')))
 cmd[2]=str(out/tag);cmd=[overrides.get(a,a) for a in cmd]
 with (out/'build.log').open('a') as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True)
 commands.append(cmd)
src=(root/'src/map/ResourceGrowth.cpp').read_text()
# Diagnostic removes statistics only; map mutations and growth metrics remain.
no_stats=src.replace('recordDelta(map, p, oldType == NO_RES_TYPE);','/* diagnostic: omit growth statistics */')
link_binary('build/linux/client/release/src/glob2','no-statistics',dict([compile_source('src/map/ResourceGrowth.cpp',no_stats,'no-statistics')]))
# Count-only kernel consumes the same input/randomness but writes no proposal records.
a=src.index('void calculate(');b=src.index('\nvoid apply(',a)
count=src[a:b].replace('void calculate(', 'std::size_t calculateCount(')
count=count.replace('out.proposals.clear();','std::size_t count = 0;')
count=count.replace('if (v.resourceGrowthDisabled)\n\t\treturn;','if (v.resourceGrowthDisabled)\n\t\treturn 0;')
count=count.replace('out.proposals.size()', 'count')
count=count.replace('out.proposals.push_back({Uint32(i), source.type, Uint8(m), 1});','++count;')
count=count.replace('out.proposals.push_back(\n\t\t\t\t{Uint32(i), source.type, Uint8(materialIndex(p.primaryMaterial)), 1});','++count;')
count=count.replace('out.capacityGrew = out.proposals.capacity() != initialCapacity;','out.capacityGrew = false;\n\treturn count;')
assert 'push_back' not in count
modified=src[:b]+'\n'+count+src[b:]
bench=(root/'src/map/ResourceGrowthBenchmark.cpp').read_text()
bench+='''
namespace ResourceGrowth { std::size_t calculateCount(const MapState::View &, MersenneTwister &, Batch &); }
TEST_CASE("proposal write ablation [benchmark][resources]") {
 glob2test::HeadlessGlobals globals;
 Json report={{"samples",Json::array()}};
 for(int shift:{7,8,9}) for(const std::string scenario:{"sparse","dense","saturated","blocked","multi"}) {
  glob2test::HeadlessGame world({.wDec=shift,.hDec=shift,.header=true,.seed=713});
  auto &map=world.game.map;setup(map,scenario);map.resourceGrowthField();
  auto snapshot=world.game.snapshotStore().captureBoundary(world.game,ResourceGrowth::Pipeline::requirements());
  auto view=snapshot.view();ResourceGrowth::Batch emitted,counted;emitted.proposals.reserve(map.cellCount());
  for(unsigned seed=1;seed<=128;++seed) {
   MersenneTwister a(seed),b(seed);ResourceGrowth::calculate(view,a,emitted);
   auto n=ResourceGrowth::calculateCount(view,b,counted);
   REQUIRE(n==emitted.proposals.size());REQUIRE(counted.sampled==emitted.sampled);REQUIRE(a()==b());
  }
  for(int rep=-1;rep<10;++rep) for(int order=0;order<2;++order) {
   const bool countOnly=(rep+1+order)%2; std::vector<MersenneTwister> rngs;
   for(unsigned seed=1;seed<=1024;++seed) rngs.emplace_back(seed);
   MersenneTwister warm(713);ResourceGrowth::calculate(view,warm,emitted);
   Uint64 proposals=0,sampled=0;auto start=Clock::now();
   for(auto &rng:rngs) {
    if(countOnly){proposals+=ResourceGrowth::calculateCount(view,rng,counted);sampled+=counted.sampled;}
    else{ResourceGrowth::calculate(view,rng,emitted);proposals+=emitted.proposals.size();sampled+=emitted.sampled;}
   }
   auto elapsed=ns(start);
   report["samples"].push_back({{"size",1<<shift},{"scenario",scenario},{"repeat",rep},{"count_only",countOnly},{"elapsed_ns",elapsed},{"proposals",proposals},{"sampled",sampled}});
  }
 }
 std::ofstream output(std::getenv("GLOB2_WRITE_ABLATION_OUTPUT"));output<<report.dump(2);REQUIRE(output.good());
}
'''
objects=dict([compile_source('src/map/ResourceGrowth.cpp',modified,'count'),compile_source('src/map/ResourceGrowthBenchmark.cpp',bench,'count')])
link_binary('build/linux/client/release/test/glob2-engine-tests','write-ablation-tests',objects)
# Exclusive per-thread CPU times, only coarse scopes to avoid per-proposal clock overhead.
(out/'StageProbe.h').write_text((root/'artifacts/resource-growth/deeper/StageProbe.h').read_text())
files={'src/engine/sim/snapshot/SnapshotStore.cpp':[('Handle Store::captureBoundary(',0)],'src/map/MapStep.cpp':[('void Map::growResources(',1)],'src/map/gradient/SnapshotGradient.cpp':[('void seed(',5),('void propagate(',6)],'src/ai/engine/AIPipeline.cpp':[],'src/app/cli/Headless.cpp':[],'src/map/ResourceGrowth.cpp':[('void calculate(',2),('void apply(',3),('void Pipeline::prepare(',4)]}
objects={}
for name,functions in files.items():
 text=(root/name).read_text()
 for prefix,slot in functions:
  pos=text.index(prefix);brace=text.index('\n{',pos)+2;text=text[:brace]+f'\n StageProbe::Scope stageProbe({slot});'+text[brace:]
 if name.endswith('AIPipeline.cpp'):
  needle='std::vector<ResourceEnrollmentRequest> enrollments;';assert needle in text;text=text.replace(needle,'StageProbe::Scope stageProbe(7); '+needle)
 if name.endswith('Headless.cpp'):
  text=text.replace('const auto runStart =','StageProbe::begin();\n        const auto runStart =',1)
  text=text.replace('const auto runEnd = std::chrono::steady_clock::now();','const auto runEnd = std::chrono::steady_clock::now();\n        StageProbe::finish();',1)
 objects.update([compile_source(name,'#include "StageProbe.h"\n'+text,'stages')])
link_binary('build/linux/client/release/src/glob2','stages',objects)
(out/'build-commands.json').write_text(json.dumps(commands,indent=2))
print('Built statistics ablation, count-only kernel, and coarse CPU stage probe',flush=True)
