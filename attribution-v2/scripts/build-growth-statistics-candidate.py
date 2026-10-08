from pathlib import Path
import json
source=Path('docs/.work/build-growth-attribution-v2.py').read_text();exec(source[:source.index("src=(root/'src/map/ResourceGrowth.cpp')")])
src=(root/'src/map/ResourceGrowth.cpp').read_text();a=src.index('void recordDelta(');b=src.index('\n} // namespace',a)
src=src[:a]+'''struct GrowthTotals
{
 std::array<std::array<Uint64, MaterialCount>, 3> values{};
 unsigned materials = 0;
};
void recordDelta(Map &map, const Proposal &p, bool newTile, GrowthTotals &totals)
{
 totals.materials |= 1u << p.material;
 totals.values[0][p.material] += newTile;
 totals.values[1][p.material] += p.delta > 0;
 totals.values[2][p.material] += p.delta < 0;
 const int x = p.tile & map.getMaskW(), y = p.tile >> map.getShiftW();
 const unsigned active = (1u << map.game->mapHeader.getNumberOfTeams()) - 1;
 for (int band=0; band<GROWTH_COVERAGE_BANDS; ++band)
  for (unsigned teams=map.teamsWithBuildingsNear(x,y,band)&active; teams; teams &= teams-1)
  {
   auto *team=map.game->teams[std::countr_zero(teams)];
   if (!team) continue;
   auto &m=team->stats.measurements;
   m.growthTiles[band][p.material] += newTile;
   m.growthAmount[band][p.material] += p.delta > 0;
   m.growthReduction[band][p.material] += p.delta < 0;
  }
}
void recordTotals(Map &map, const GrowthTotals &totals)
{
 for (int t=0; t<map.game->mapHeader.getNumberOfTeams(); ++t)
 {
  auto *team=map.game->teams[t];
  if (!team) continue;
  for (unsigned mask=totals.materials; mask; mask &= mask-1)
  {
   const auto material=std::countr_zero(mask);
   for(unsigned kind=0; kind<3; ++kind)
    team->stats.measurements.growthGlobal[kind][material] += totals.values[kind][material];
  }
 }
}
'''+src[b:]
src=src.replace('map.rebuildGrowthCoverage();\n\tfor (const auto &p : batch.proposals)','map.rebuildGrowthCoverage();\n\tGrowthTotals totals;\n\tfor (const auto &p : batch.proposals)')
src=src.replace('recordDelta(map, p, oldType == NO_RES_TYPE);','recordDelta(map, p, oldType == NO_RES_TYPE, totals);')
src=src.replace('\n\t}\n\tmetrics.publicationNs += now() - start;','\n\t}\n\trecordTotals(map, totals);\n\tmetrics.publicationNs += now() - start;')
original=(root/'src/map/ResourceGrowth.cpp').read_text()
a=original.index('void recordDelta(');b=original.index('\n} // namespace',a)
referenceDelta=original[a:b].replace('recordDelta(', 'recordDeltaReference(')
src=src.replace('struct GrowthTotals', referenceDelta+'\nstruct GrowthTotals',1)
a=original.index('void apply(');b=original.index('SimulationSnapshot::Requirements Pipeline::requirements()',a)
referenceApply=original[a:b].replace('void apply(', 'void applyReference(').replace('recordDelta(', 'recordDeltaReference(')
src=src.replace('SimulationSnapshot::Requirements Pipeline::requirements()',referenceApply+'SimulationSnapshot::Requirements Pipeline::requirements()',1)
objects=dict([compile_source('src/map/ResourceGrowth.cpp',src,'batched-statistics')])
link_binary('build/linux/client/release/src/glob2','batched-statistics',objects)
bench=(root/'src/map/ResourceGrowthBenchmark.cpp').read_text()
bench+=r'''
namespace ResourceGrowth { void applyReference(Map &, const Batch &, Metrics &); }
TEST_CASE("batched statistics match reference across coverage and signed deltas") {
 glob2test::HeadlessGlobals globals;
 glob2test::HeadlessGame a({.wDec=7,.hDec=7,.teams=4,.header=true,.seed=713});setup(a.game.map,"multi");
 glob2test::HeadlessGame b({.wDec=7,.hDec=7,.teams=4,.header=true,.seed=713});setup(b.game.map,"multi");
 for(auto *game:{&a.game,&b.game})for(int t=0;t<4;++t){auto &stats=game->teams[t]->stats;stats.coverageBuildings={{t*25,t*29,2,2},{0,0,4,4}};++stats.coverageBuildingGeneration;}
 auto type=resourceIndex(*a.game.map.resourceRegistry().find("benchmark-crop"));
 MersenneTwister rng(981);ResourceGrowth::Metrics x,y;
 for(int batch=0;batch<64;++batch){
  ResourceGrowth::Batch proposals;
  for(int n=0;n<512;++n)proposals.proposals.push_back({Uint32(rng()%a.game.map.cellCount()),type,Uint8(rng()%2?materialIndex(MaterialId::Food):materialIndex(MaterialId::Paper)),Sint8(rng()%2?1:-1)});
  ResourceGrowth::applyReference(a.game.map,proposals,x);ResourceGrowth::apply(b.game.map,proposals,y);
  for(int t=0;t<4;++t)REQUIRE(a.game.teams[t]->stats.measurements==b.game.teams[t]->stats.measurements);
  REQUIRE(x.accepted==y.accepted);REQUIRE(x.rejected==y.rejected);REQUIRE(x.clamped==y.clamped);REQUIRE(x.stockAdded==y.stockAdded);REQUIRE(x.tilesAdded==y.tilesAdded);
  for(size_t i=0;i<a.game.map.cellCount();++i){REQUIRE(a.game.map.cellView().resources[i].resource.type==b.game.map.cellView().resources[i].resource.type);REQUIRE(a.game.map.materialAmountAt(i,MaterialId::Food)==b.game.map.materialAmountAt(i,MaterialId::Food));REQUIRE(a.game.map.materialAmountAt(i,MaterialId::Paper)==b.game.map.materialAmountAt(i,MaterialId::Paper));}
 }
}
'''
objects.update([compile_source('src/map/ResourceGrowthBenchmark.cpp',bench,'batched-statistics')])
link_binary('build/linux/client/release/test/glob2-engine-tests','batched-statistics-tests',objects)
(out/'batched-build-commands.json').write_text(json.dumps(commands,indent=2))
