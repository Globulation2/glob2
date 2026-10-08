from pathlib import Path
import json
# Reuse exact current compiler/link arguments; never replace production objects.
source=Path('docs/.work/build-growth-attribution-v2.py').read_text()
exec(source[:source.index("src=(root/'src/map/ResourceGrowth.cpp')")])
src=(out/'count-ResourceGrowth.cpp').read_text()
a=src.index('std::size_t calculateCount(');b=src.index('\nvoid apply(',a)
count=src[a:b].replace('calculateCount(', 'calculateCountOutlined(').replace('auto propose = [&](size_t i, const Resource &source)','auto propose = [&](size_t i, const Resource &source) __attribute__((noinline))')
src=src[:b]+count+src[b:]
bench=(out/'count-ResourceGrowthBenchmark.cpp').read_text()
bench=bench.replace('std::size_t calculateCount(', 'std::size_t calculateCountOutlined(').replace('ResourceGrowth::calculateCount(', 'ResourceGrowth::calculateCountOutlined(').replace('proposal write ablation', 'outlined proposal write ablation')
bench+='''
TEST_CASE("incremental growth snapshot capture [benchmark][resources]") {
 glob2test::HeadlessGlobals globals;Json report={{"samples",Json::array()}};
 using namespace SimulationSnapshot;
 auto existing=bit(Component::Catalogs)|bit(Component::Terrain)|bit(Component::Resources)|bit(Component::Occupancy)|bit(Component::Areas);
 auto growth=ResourceGrowth::Pipeline::requirements();
 for(int shift:{8,9}) for(int cadence:{1,4,32}) for(int rep=-1;rep<10;++rep) for(int order=0;order<2;++order) {
  bool includeGrowth=(rep+1+order)%2;
  glob2test::HeadlessGame world({.wDec=shift,.hDec=shift,.header=true,.seed=713});
  auto &map=world.game.map;setup(map,"dense");map.resourceGrowthField();
  auto &store=world.game.snapshotStore();store.captureBoundary(world.game,existing|growth);store.metrics={};
  Uint64 elapsed=0;
  for(unsigned tick=1;tick<=256;++tick) {
   world.game.stepCounter=tick;
   // Identical stock changes in both variants, outside measured capture time.
   for(unsigned n=0;n<16;++n){auto row=(tick*14+n*22)%map.getH();auto col=(tick*18+n*26)%map.getW();map.setMaterialAmount(map.coordToIndex(col,row),MaterialId::Food,1+(tick&1));}
   if(includeGrowth||tick%cadence==0){auto start=Clock::now();auto handle=store.captureBoundary(world.game,existing|(includeGrowth?growth:0));elapsed+=ns(start);}
  }
  report["samples"].push_back({{"size",1<<shift},{"existing_cadence",cadence},{"repeat",rep},{"growth",includeGrowth},{"capture_ns",elapsed},{"copied_bytes",store.metrics.bytesCopied},{"captures",store.metrics.captures},{"final_food",stocks(map)}});
 }
 std::ofstream output(std::getenv("GLOB2_CAPTURE_ABLATION_OUTPUT"));output<<report.dump(2);REQUIRE(output.good());
}
'''
objects=dict([compile_source('src/map/ResourceGrowth.cpp',src,'controls'),compile_source('src/map/ResourceGrowthBenchmark.cpp',bench,'controls')])
link_binary('build/linux/client/release/test/glob2-engine-tests','controls-tests',objects)
(out/'controls-build-commands.json').write_text(json.dumps(commands,indent=2))
