#include <cstring>
#include <time.h>
#include <Engine.h>
extern "C" Uint64 resourceExperimentCopyCalls();
namespace {
Uint64 componentCpu() { timespec t{};clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&t);return Uint64(t.tv_sec)*1000000000+t.tv_nsec; }
}
TEST_CASE("remaining growth component experiments [benchmark][resources]") {
 const char* output=std::getenv("GLOB2_REMAINING_COMPONENT_OUTPUT");if(!output)return;
 glob2test::HeadlessGlobals globals;Json report={{"samples",Json::array()}};
 const char* repetition=std::getenv("GLOB2_REMAINING_COMPONENT_REPEAT");
 const int first=repetition?std::atoi(repetition):-1, last=repetition?first+1:10;
 for(int repeat=first;repeat<last;++repeat) {
  for(bool multi:{false,true}) {
   glob2test::HeadlessGame world({.wDec=7,.hDec=7,.teams=4,.header=true,.seed=713});
   auto& map=world.game.map;setup(map,multi?"multi":"dense");
   for(int t=0;t<4;++t) {auto& stats=world.game.teams[t]->stats;stats.coverageBuildings={{t*25,t*29,2,2},{0,0,4,4}};++stats.coverageBuildingGeneration;}
   auto cpu=componentCpu(),calls=resourceExperimentCopyCalls();auto start=Clock::now();
   for(unsigned n=0;n<65536;++n)map.setMaterialAmount(map.coordToIndex((n*2)%128,((n/64)*2)%128),multi&&n%2?MaterialId::Paper:MaterialId::Food,1+(n/4096)%4);
   auto elapsed=ns(start),spent=componentCpu()-cpu;
   report["samples"].push_back({{"stage","stock"},{"multi",multi},{"repeat",repeat},{"elapsed_ns",elapsed},{"cpu_ns",spent},{"operations",65536},{"copy_calls",resourceExperimentCopyCalls()-calls},{"food",stocks(map)}});
  }
  for(bool multi:{false,true}) {
   glob2test::HeadlessGame world({.wDec=7,.hDec=7,.teams=4,.header=true,.seed=713});auto& map=world.game.map;setup(map,multi?"multi":"dense");
   for(int t=0;t<4;++t) {auto& stats=world.game.teams[t]->stats;stats.coverageBuildings={{t*25,t*29,2,2},{0,0,4,4}};++stats.coverageBuildingGeneration;}
   ResourceGrowth::Batch batch;MersenneTwister rng(123);auto type=Uint16(resourceIndex(*map.resourceRegistry().find("benchmark-crop")));
   for(unsigned n=0;n<32768;++n)batch.proposals.push_back({Uint32(rng()%map.cellCount()),type,Uint8(multi&&rng()%2?materialIndex(MaterialId::Paper):materialIndex(MaterialId::Food)),Sint8(rng()%2?1:-1)});
   ResourceGrowth::Metrics metrics;auto cpu=componentCpu();auto start=Clock::now();ResourceGrowth::apply(map,batch,metrics);auto elapsed=ns(start),spent=componentCpu()-cpu;
   report["samples"].push_back({{"stage","publication"},{"multi",multi},{"repeat",repeat},{"elapsed_ns",elapsed},{"cpu_ns",spent},{"proposals",batch.proposals.size()},{"accepted",metrics.accepted},{"rejected",metrics.rejected},{"clamped",metrics.clamped},{"tiles",metrics.tilesAdded},{"stock",metrics.stockAdded},{"food",stocks(map)}});
  }
  for(int shift:{7,8,9})for(std::string pattern:{"unchanged","sparse","clustered","fragmented","half","dense","churn"})for(int retained:{0,4}) {
   glob2test::HeadlessGame world({.wDec=shift,.hDec=shift,.header=true,.seed=713});auto& map=world.game.map;setup(map,"multi");map.resourceGrowthField();
   auto& store=world.game.snapshotStore();std::deque<SimulationSnapshot::Handle> handles;
   auto type=Uint16(resourceIndex(*map.resourceRegistry().find("benchmark-crop")));
   Uint64 elapsed=0,cpu=0,calls=0,allocationsBefore=0,coldNs=0,coldCpu=0,coldCalls=0,coldBytes=0;auto& geom=map.chunks();
   for(unsigned tick=1;tick<=40;++tick) {
    world.game.stepCounter=tick;
    if(pattern!="unchanged") {
     size_t count=pattern=="half"?geom.count()/2:pattern=="dense"?geom.count():pattern=="sparse"?1:std::max(size_t(1),geom.count()/4);
     for(size_t n=0;n<count;++n) {
      auto chunk=pattern=="fragmented" ? (n*7+tick)%geom.count():n;
      auto x=(chunk%geom.chunksWide)*16,y=(chunk/geom.chunksWide)*16;
      auto i=map.coordToIndex(x,y);
      if(pattern=="churn"){map.replaceResource(i,Resource{});map.replaceResource(i,Resource{type,0,1,0});}
      map.setMaterialAmount(i,MaterialId::Paper,1+tick%4);
     }
    }
    auto cb=resourceExperimentCopyCalls(),cs=componentCpu();auto start=Clock::now();auto handle=store.captureBoundary(world.game,ResourceGrowth::Pipeline::requirements());auto dt=ns(start),dc=componentCpu()-cs;
    if(tick==1){coldNs=dt;coldCpu=dc;coldCalls=resourceExperimentCopyCalls()-cb;coldBytes=store.metrics.bytesCopied;}
    if(retained){handles.push_back(handle);if(handles.size()>size_t(retained))handles.pop_front();}
    if(tick==8){allocationsBefore=store.metrics.allocations;store.metrics={};}else if(tick>8){elapsed+=dt;cpu+=dc;calls+=resourceExperimentCopyCalls()-cb;}
   }
   auto final=store.captureBoundary(world.game,ResourceGrowth::Pipeline::requirements());
   REQUIRE(final.resources->stocks==std::vector(map.resourceStockState().begin(),map.resourceStockState().end()));
   REQUIRE(std::memcmp(final.resources->cells.data(),map.resourceState().data(),map.resourceState().size_bytes())==0);
   report["samples"].push_back({{"stage","capture"},{"size",1<<shift},{"pattern",pattern},{"retained",retained},{"repeat",repeat},{"elapsed_ns",elapsed},{"cpu_ns",cpu},{"copy_calls",calls},{"copied_bytes",store.metrics.bytesCopied},{"allocations",store.metrics.allocations-allocationsBefore},{"cold_ns",coldNs},{"cold_cpu_ns",coldCpu},{"cold_copy_calls",coldCalls},{"cold_bytes",coldBytes},{"food",stocks(map)}});
  }
 }
 std::ofstream out(output);out<<report.dump(2);REQUIRE(out.good());
}
TEST_CASE("inspect remaining experiment saves [benchmark][resources]") {
 auto input=std::getenv("GLOB2_REMAINING_INSPECT_INPUT"),output=std::getenv("GLOB2_REMAINING_INSPECT_OUTPUT");if(!input||!output)return;
 glob2test::GlobalsOptions opts;opts.loadStrings=true;glob2test::HeadlessGlobals globals(opts);
 Engine engine;REQUIRE(engine.initCustom(input)==Engine::EE_NO_ERROR);auto& map=engine.gui.game.map;
 Json result={{"stocks",Json::array()},{"deposits",0},{"growth_global",Json::array()}};std::array<Uint64,MaterialCount> amounts{};unsigned deposits=0;
 for(size_t i=0;i<map.cellCount();++i)if(map.getResource(i).type!=NO_RES_TYPE){++deposits;auto stocks=map.materialStocksAt(i);for(unsigned m=0;m<MaterialCount;++m)amounts[m]+=stocks[m];}
 result["stocks"]=amounts;result["deposits"]=deposits;
 for(int t=0;t<engine.gui.game.mapHeader.getNumberOfTeams();++t){auto& m=engine.gui.game.teams[t]->stats.measurements;Json counters=Json::array();for(auto& row:m.growthGlobal){Json values=Json::array();for(auto value:row)values.push_back(value);counters.push_back(values);}result["growth_global"].push_back(counters);}
 std::ofstream out(output);out<<result.dump(2);REQUIRE(out.good());
}
