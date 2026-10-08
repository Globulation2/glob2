from pathlib import Path
r=Path.cwd();out=r/'artifacts/resource-growth/remaining'
# Keep a frozen publication reference in the test translation unit.
original=(out/'sources/ResourceGrowth.cpp').read_text();helpers=original[original.index('namespace ResourceGrowth'):original.index('void calculate(')].replace('namespace ResourceGrowth','namespace ReferenceGrowth\n{ using namespace ResourceGrowth; }\nnamespace ReferenceGrowth',1)
apply=original[original.index('void apply('):original.index('SimulationSnapshot::Requirements Pipeline::requirements()')]
p=r/'src/map/ResourceGrowthTest.cpp';base=p.read_text();(out/'sources/ResourceGrowthTest.cpp').write_text(base)
base+='\n'+helpers+apply+'\n}\n'+r'''
TEST_CASE("optimized publication matches frozen reference and statistics" * doctest::test_suite("ResourceGrowth")) {
 glob2test::HeadlessGlobals globals;
 glob2test::HeadlessGame a({.wDec=6,.hDec=6,.teams=4,.header=true,.seed=713});
 glob2test::HeadlessGame b({.wDec=6,.hDec=6,.teams=4,.header=true,.seed=713});
 auto type=crop(a.game.map,true);REQUIRE(type==crop(b.game.map,true));
 for(auto* game:{&a.game,&b.game})for(int t=0;t<4;++t) {
  auto &stats=game->teams[t]->stats;stats.coverageBuildings={{t*9,t*11,2,2},{0,0,4,4}};++stats.coverageBuildingGeneration;
 }
 MersenneTwister rng(981);ResourceGrowth::Metrics x,y;
 for(int batch=0;batch<64;++batch) {
  for(unsigned n=0;n<12;++n) {
   const auto i=rng()%a.game.map.cellCount();
   const Resource replacement=n%3==0 ? Resource{} : Resource{Uint16(n%2 ? resourceIndex(type) : STONE),0,1,0};
   a.game.map.replaceResource(i,replacement);b.game.map.replaceResource(i,replacement);
  }
  ResourceGrowth::Batch proposals;
  for(int n=0;n<512;++n)proposals.proposals.push_back({Uint32(rng()%a.game.map.cellCount()),resourceIndex(type),Uint8(rng()%2?materialIndex(MaterialId::Food):materialIndex(MaterialId::Paper)),Sint8(rng()%2?1:-1)});
  ReferenceGrowth::apply(a.game.map,proposals,x);ResourceGrowth::apply(b.game.map,proposals,y);
  for(int t=0;t<4;++t)REQUIRE(a.game.teams[t]->stats.measurements==b.game.teams[t]->stats.measurements);
  REQUIRE(x.accepted==y.accepted);REQUIRE(x.rejected==y.rejected);REQUIRE(x.clamped==y.clamped);REQUIRE(x.stockAdded==y.stockAdded);REQUIRE(x.tilesAdded==y.tilesAdded);
  REQUIRE(a.game.map.cellView().materialSourceCounts==b.game.map.cellView().materialSourceCounts);
  for(size_t i=0;i<a.game.map.cellCount();++i) {
   REQUIRE(a.game.map.getResource(i)==b.game.map.getResource(i));
   REQUIRE(a.game.map.materialStocksAt(i)==b.game.map.materialStocksAt(i));
  }
 }
}
TEST_CASE("material totals match full sums through clamping and zero crossings" * doctest::test_suite("ResourceGrowth")) {
 glob2test::HeadlessGlobals globals;
 glob2test::HeadlessGame world({.header=true});auto& map=world.game.map;
 auto resource=nlohmann::json::parse(map.resourceRegistry().serialize())["resources"][WHEAT];
 resource["key"]="maximum-stock";resource["properties"]["persistsWhenEmpty"]=true;
 resource["yields"]["food"]["capacity"]=65535;
 resource["yields"]["paper"]=resource["yields"]["food"];
 map.installResourceDefinitions(nlohmann::json{{"schemaVersion",1},{"resources",{resource}}}.dump());
 auto id=*map.resourceRegistry().find("maximum-stock");
 map.replaceResource(0,Resource{resourceIndex(id),0,1,0});
 for(unsigned food:{0u,1u,65534u,65535u,0u})for(unsigned paper:{65535u,65534u,1u,0u}) {
  map.setMaterialAmount(0,MaterialId::Food,food);map.setMaterialAmount(0,MaterialId::Paper,paper);
  REQUIRE(map.getResource(0).amount==food+paper);
  auto before=map.changes(MapState::TrackedArray::Resources).generation;
  map.setMaterialAmount(0,MaterialId::Paper,paper);
  REQUIRE(map.changes(MapState::TrackedArray::Resources).generation==before);
 }
}
''';p.write_text(base)
p=r/'src/engine/sim/snapshot/WorldSnapshotTest.cpp';base=p.read_text();(out/'sources/WorldSnapshotTest.cpp').write_text(base)
base+=r'''
TEST_CASE("stock snapshots survive slot reuse rebuild and sparse refresh" * doctest::test_suite("WorldSnapshot")) {
 glob2test::HeadlessGlobals globals;
 glob2test::HeadlessGame world({.wDec=7,.hDec=6,.header=true});auto& game=world.game;auto& map=game.map;
 auto resource=nlohmann::json::parse(map.resourceRegistry().serialize())["resources"][WHEAT];
 resource["key"]="snapshot-churn";resource["yields"]["paper"]=resource["yields"]["food"];
 auto doc=nlohmann::json{{"schemaVersion",1},{"resources",{resource}}}.dump();map.installResourceDefinitions(doc);
 auto type=resourceIndex(*map.resourceRegistry().find("snapshot-churn"));
 for(size_t i=0;i<2048;++i)map.replaceResource(i,Resource{type,0,1,0});
 SimulationSnapshot::Store store;store.setVerification(true);
 auto next=[&]{++game.stepCounter;return store.captureBoundary(game,SimulationSnapshot::bit(SimulationSnapshot::Component::Resources)|SimulationSnapshot::bit(SimulationSnapshot::Component::Terrain));};
 auto held=next();auto original=held.resources->stocks;
 for(unsigned pass=0;pass<32;++pass) {
  for(unsigned n=0;n<48;++n) {
   const auto i=(pass*71+n*13)%2048;
   map.replaceResource(i,Resource{});
   map.replaceResource(2048+i,Resource{type,0,1,0});
   map.setMaterialAmount(2048+i,MaterialId::Paper,1+pass%4);
  }
  auto captured=next();REQUIRE(captured.resources->stocks==std::vector(map.resourceStockState().begin(),map.resourceStockState().end()));
  REQUIRE(held.resources->stocks==original);
 }
 map.rebuildResourceState();auto rebuilt=next();
 REQUIRE(rebuilt.resources->stocks==std::vector(map.resourceStockState().begin(),map.resourceStockState().end()));
 map.setMaterialAmount(2050,MaterialId::Paper,4);auto changed=next();
 REQUIRE(changed.resources->stocks==std::vector(map.resourceStockState().begin(),map.resourceStockState().end()));
}
TEST_CASE("coalesced rows preserve partial and rectangular snapshot arrays" * doctest::test_suite("WorldSnapshot")) {
 glob2test::HeadlessGlobals globals;
 for(auto dimensions:{std::pair{3,2},std::pair{7,5}}) {
  glob2test::HeadlessGame world({.wDec=dimensions.first,.hDec=dimensions.second});auto& game=world.game;
  SimulationSnapshot::Store store;store.setVerification(true);
  const auto areas=SimulationSnapshot::bit(SimulationSnapshot::Component::Areas);
  auto next=[&]{++game.stepCounter;return store.captureBoundary(game,areas);};
  auto held=next();
  for(int pass=0;pass<24;++pass) {
   for(int x=0;x<game.map.getW()/2;++x)game.map.addForbidden(x,pass%game.map.getH(),0);
   auto captured=next();
   REQUIRE(std::memcmp(captured.areas->cells.data(),game.map.areaState().data(),game.map.areaState().size_bytes())==0);
  }
  REQUIRE(held.areas->cells[0].forbidden==0);
 }
}
''';p.write_text(base)
