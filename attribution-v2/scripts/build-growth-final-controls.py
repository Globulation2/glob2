from pathlib import Path
import json
source=Path('docs/.work/build-growth-attribution-v2.py').read_text();exec(source[:source.index("src=(root/'src/map/ResourceGrowth.cpp')")])
src=(out/'controls-ResourceGrowth.cpp').read_text()
# Force the same outlining boundary in BOTH measured kernels. Extra functions
# changed GCC's TU-wide inlining decision in the first attempted control.
src=src.replace('auto propose = [&](size_t i, const Resource &source)\n','auto propose = [&](size_t i, const Resource &source) __attribute__((noinline))\n',1)
bench=(out/'controls-ResourceGrowthBenchmark.cpp').read_text()
bench+='''
TEST_CASE("snapshot lease retention capture [benchmark][resources]") {
 glob2test::HeadlessGlobals globals;Json report={{"samples",Json::array()}};
 using namespace SimulationSnapshot;
 auto required=ResourceGrowth::Pipeline::requirements()|bit(Component::Areas);
 for(int shift:{8,9}) for(int rep=-1;rep<10;++rep) for(int order=0;order<4;++order) {
  const int leases[4]={0,1,4,8};const int retained=leases[(order+rep+1)%4];
  glob2test::HeadlessGame world({.wDec=shift,.hDec=shift,.header=true,.seed=713});
  auto &map=world.game.map;setup(map,"dense");map.resourceGrowthField();
  auto &store=world.game.snapshotStore();std::deque<Handle> handles;Uint64 elapsed=0;
  for(unsigned tick=1;tick<=288;++tick) {
   world.game.stepCounter=tick;
   for(unsigned n=0;n<16;++n){auto row=(tick*14+n*22)%map.getH();auto col=(tick*18+n*26)%map.getW();map.setMaterialAmount(map.coordToIndex(col,row),MaterialId::Food,1+(tick&1));}
   auto start=Clock::now();auto handle=store.captureBoundary(world.game,required);auto duration=ns(start);
   if(retained){handles.push_back(handle.project(ResourceGrowth::Pipeline::requirements()));if(handles.size()>size_t(retained))handles.pop_front();}
   if(tick==32)store.metrics={};else if(tick>32)elapsed+=duration;
  }
  report["samples"].push_back({{"size",1<<shift},{"retained",retained},{"repeat",rep},{"capture_ns",elapsed},{"copied_bytes",store.metrics.bytesCopied},{"captures",store.metrics.captures},{"final_food",stocks(map)}});
 }
 std::ofstream output(std::getenv("GLOB2_RETENTION_OUTPUT"));output<<report.dump(2);REQUIRE(output.good());
}
'''
objects=dict([compile_source('src/map/ResourceGrowth.cpp',src,'matched-outlining'),compile_source('src/map/ResourceGrowthBenchmark.cpp',bench,'matched-outlining')])
link_binary('build/linux/client/release/test/glob2-engine-tests','matched-controls-tests',objects)
(out/'matched-build-commands.json').write_text(json.dumps(commands,indent=2))
