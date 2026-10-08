// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "ResourceGrowth.h"
#include "gradient/GradientRuntime.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdlib>

namespace
{
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
Uint64 ns(Clock::time_point t)
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t).count();
}
void setup(Map &map, const std::string &scenario)
{
	auto doc = Json::parse(map.resourceRegistry().serialize());
	auto crop = doc["resources"][1];
	crop["key"] = "benchmark-crop";
	crop["properties"]["ecology"] = "uniform";
	if (scenario == "multi")
		crop["yields"]["paper"] = {{"capacity", 5},
								   {"initial", 2},
								   {"growthRate", ResourceRateScale},
								   {"consumption", "one"}};
	map.installResourceDefinitions(Json{{"schemaVersion", 1}, {"resources", {crop}}}.dump());
	const auto id = *map.resourceRegistry().find("benchmark-crop");
	const int stride = scenario == "sparse" ? 8 : 2;
	for (int y = 0; y < map.getH(); y += stride)
		for (int x = 0; x < map.getW(); x += stride)
		{
			map.setResource(x, y, id, 0);
			if (scenario == "saturated")
				map.setMaterialAmount(map.coordToIndex(x, y), MaterialId::Food, 5);
		}
	if (scenario == "blocked")
		for (int y = 0; y < map.getH(); ++y)
			for (int x = 0; x < map.getW(); ++x)
				if ((x | y) & 1)
					map.setResourcesGrow(x, y, false);
	map.game->gameHeader.setResourceGrowthDisabled(scenario == "disabled");
}
Uint64 stocks(Map &map)
{
	Uint64 result = 0;
	for (int y = 0; y < map.getH(); ++y)
		for (int x = 0; x < map.getW(); ++x)
			result += map.materialAmountAt(map.coordToIndex(x, y), MaterialId::Food);
	return result;
}
} // namespace
TEST_SUITE("ResourceGrowthBenchmark")
{
	TEST_CASE("identical live and snapshot kernel inputs [benchmark][resources]")
	{
		glob2test::HeadlessGlobals globals;
		Json report = {{"samples", Json::array()}};
		for (int shift : {7, 8, 9})
			for (const std::string scenario : {"sparse", "dense", "saturated", "blocked", "multi"})
			{
				glob2test::HeadlessGame world(
					{.wDec = shift, .hDec = shift, .header = true, .seed = 713});
				auto &map = world.game.map;
				setup(map, scenario);
				map.resourceGrowthField();
				const auto captured = world.game.snapshotStore().captureBoundary(
					world.game, ResourceGrowth::Pipeline::requirements());
				const std::array<MapState::View, 2> views{map.stateView(), captured.view()};
				ResourceGrowth::Batch output[2];
				for (auto &batch : output)
					batch.proposals.reserve(map.cellCount());
				for (unsigned seed = 1; seed <= 32; ++seed)
				{
					MersenneTwister a(seed), b(seed);
					ResourceGrowth::calculate(views[0], a, output[0]);
					ResourceGrowth::calculate(views[1], b, output[1]);
					REQUIRE(output[0].sampled == output[1].sampled);
					REQUIRE(output[0].proposals.size() == output[1].proposals.size());
					for (size_t n = 0; n < output[0].proposals.size(); ++n)
					{
						const auto &x = output[0].proposals[n], &y = output[1].proposals[n];
						REQUIRE(std::tie(x.tile, x.type, x.material, x.delta) ==
								std::tie(y.tile, y.type, y.material, y.delta));
					}
					REQUIRE(a() == b());
				}
				for (int repeat = -1; repeat < 20; ++repeat)
					for (int order = 0; order < 2; ++order)
					{
						const int mode = (order + repeat + 1) % 2;
						std::vector<MersenneTwister> rngs;
						for (unsigned seed = 1; seed <= 128; ++seed)
							rngs.emplace_back(seed);
						// Equal warm-up of the selected immutable input, outside timing.
						MersenneTwister warm(713);
						ResourceGrowth::calculate(views[mode], warm, output[mode]);
						Uint64 proposals = 0, sampled = 0;
						const auto start = Clock::now();
						for (auto &rng : rngs)
						{
							ResourceGrowth::calculate(views[mode], rng, output[mode]);
							proposals += output[mode].proposals.size();
							sampled += output[mode].sampled;
						}
						const auto elapsed = ns(start);
						report["samples"].push_back({{"size", 1 << shift},
													 {"scenario", scenario},
													 {"repeat", repeat},
													 {"snapshot", mode == 1},
													 {"elapsed_ns", elapsed},
													 {"proposals", proposals},
													 {"sampled", sampled}});
					}
			}
		if (const char *path = std::getenv("GLOB2_GROWTH_KERNEL_OUTPUT"))
		{
			std::ofstream out(path);
			out << report.dump(2);
		}
	}

	TEST_CASE("paired legacy split and delayed growth component costs [benchmark][resources]")
	{
		glob2test::HeadlessGlobals globals;
		const bool sharedCapture = std::getenv("GLOB2_GROWTH_EXISTING_CAPTURE") != nullptr;
		Json report = {{"kind", "growth-component"},
					   {"existing_capture", sharedCapture},
					   {"samples", Json::array()}};
		for (int shift : {7, 8, 9})
			for (const std::string scenario :
				 {"sparse", "dense", "saturated", "harvested", "blocked", "multi", "disabled"})
				for (int repeat = -1; repeat < 10; ++repeat)
					for (int order = 0; order < 4; ++order)
					{
						const int variant = (order + (repeat + 1)) % 4;
						glob2test::HeadlessGame world(
							{.wDec = shift, .hDec = shift, .header = true, .seed = 713});
						auto &map = world.game.map;
						setup(map, scenario);
						map.configureCompute(variant == 3 ? 4 : 1, 0);
						map.configureResourceGrowth(8, variant == 3);
						// Warm ecology before timing; snapshot capture remains inside timing.
						map.resourceGrowthField();
						map.rebuildGrowthCoverage();
						world.game.syncRandom.seed(713);
						Uint64 coldCaptureNs = 0;
						if (variant || sharedCapture)
						{
							const auto cold = Clock::now();
							world.game.snapshotStore().captureBoundary(
								world.game, sharedCapture
												? SimulationSnapshot::All
												: ResourceGrowth::Pipeline::requirements());
							coldCaptureNs = ns(cold);
							world.game.snapshotStore().metrics = {};
						}
						const auto before = stocks(map);
						const auto start = Clock::now();
						ResourceGrowth::Metrics immediate;
						ResourceGrowth::Batch immediateBatch;
						for (unsigned tick = 0; tick < 64; ++tick)
						{
							world.game.stepCounter = tick;
							if (scenario == "harvested" && tick % 8 == 0)
								for (int y = 0; y < map.getH(); y += 2)
									for (int x = 0; x < map.getW(); x += 2)
									{
										const auto i = map.coordToIndex(x, y);
										const auto amount =
											map.materialAmountAt(i, MaterialId::Food);
										if (amount > 1)
											map.setMaterialAmount(i, MaterialId::Food, amount - 1);
									}
							// Model the completed-tick capture already required by another
							// consumer. All placements pay for the same component union;
							// growth receives a projection of that capture, not another copy.
							SimulationSnapshot::Handle existing;
							if (sharedCapture)
							{
								if (variant > 1)
									map.gradientRuntime->growth.publish(map, tick);
								world.game.snapshotStore().invalidateBoundary();
								existing = world.game.snapshotStore().captureBoundary(
									world.game, SimulationSnapshot::All);
							}
							if (variant == 0)
								map.growResources();
							else if (variant == 1 && scenario != "disabled")
							{
								if (!sharedCapture)
									world.game.snapshotStore().invalidateBoundary();
								auto snapshot =
									sharedCapture
										? existing.project(ResourceGrowth::Pipeline::requirements())
										: world.game.snapshotStore().captureBoundary(
											  world.game, ResourceGrowth::Pipeline::requirements());
								MersenneTwister random(syncRand());
								const auto computing = Clock::now();
								ResourceGrowth::calculate(snapshot.view(), random, immediateBatch);
								immediate.computeNs += ns(computing);
								immediate.sampled += immediateBatch.sampled;
								immediate.proposals += immediateBatch.proposals.size();
								ResourceGrowth::apply(map, immediateBatch, immediate);
							}
							else if (variant > 1)
							{
								if (!sharedCapture)
									map.gradientRuntime->growth.publish(map, tick);
								map.stageResourceGrowth();
								if (sharedCapture)
									map.preparePendingWorld(existing);
								else
									map.preparePendingWorld();
							}
						}
						map.finishResourceGrowth();
						const auto elapsed = ns(start);
						const auto &m = variant == 1 ? immediate : map.resourceGrowthMetrics();
						Json row = {
							{"size", 1 << shift},
							{"scenario", scenario},
							{"repeat", repeat},
							{"variant", variant},
							{"elapsed_ns", elapsed},
							{"initial_food", before},
							{"final_food", stocks(map)},
							{"cold_capture_ns", coldCaptureNs},
							{"capture_ns", world.game.snapshotStore().metrics.captureNs},
							{"copied_bytes", world.game.snapshotStore().metrics.bytesCopied},
							{"compute_ns", m.computeNs},
							{"publication_ns", m.publicationNs},
							{"wait_ns", m.waitNs},
							{"proposals", m.proposals},
							{"accepted", m.accepted},
							{"sampled", m.sampled},
							{"clamped", m.clamped},
							{"stock_added", m.stockAdded},
							{"tiles_added", m.tilesAdded},
							{"max_pending", m.maxPending},
							{"proposal_bytes", m.maxProposalBytes},
							{"snapshot_peak_bytes",
							 world.game.snapshotStore().memoryMetrics().peakRetainedBytes},
							{"rejected", m.rejected}};
						report["samples"].push_back(row);
						std::cout << "GROWTH_COMPONENT " << row.dump() << '\n';
					}
		if (const char *path = std::getenv("GLOB2_GROWTH_BENCHMARK_OUTPUT"))
		{
			std::ofstream out(path);
			out << report.dump(2);
			REQUIRE(out.good());
		}
	}
	TEST_CASE("twenty seed ecology comparison [benchmark][resources]")
	{
		glob2test::HeadlessGlobals globals;
		Json report = {{"kind", "growth-ecology"}, {"samples", Json::array()}};
		for (unsigned seed = 1; seed <= 20; ++seed)
			for (bool deplete : {false, true})
				for (int variant = 0; variant < 2; ++variant)
				{
					glob2test::HeadlessGame world(
						{.wDec = 7, .hDec = 7, .header = true, .seed = seed});
					auto &map = world.game.map;
					setup(map, "dense");
					map.configureResourceGrowth(8, false);
					world.game.syncRandom.seed(seed);
					const auto initialFood = stocks(map);
					const Uint64 initialDeposits = Uint64(map.getW()) * map.getH() / 4;
					Uint64 harvested = 0, depleted = 0;
					for (unsigned tick = 0; tick < 512; ++tick)
					{
						world.game.stepCounter = tick;
						if (tick % 8 == 0)
							for (int y = 0; y < map.getH(); y += 2)
								for (int x = 0; x < map.getW(); x += 2)
								{
									const auto i = map.coordToIndex(x, y);
									auto amount = map.materialAmountAt(i, MaterialId::Food);
									if (amount > (deplete ? 0 : 1))
									{
										map.setMaterialAmount(i, MaterialId::Food, amount - 1);
										++harvested;
										depleted += map.getResource(i).type == NO_RES_TYPE;
									}
								}
						if (variant == 0)
							map.growResources();
						else
						{
							map.gradientRuntime->growth.publish(map, tick);
							map.stageResourceGrowth();
							map.preparePendingWorld();
						}
					}
					map.finishResourceGrowth();
					Uint64 deposits = 0;
					for (const auto &cell : map.cellView().resources)
						deposits += cell.resource.type != NO_RES_TYPE;
					// This fixture has one material and one-unit seeds. Conservation
					// separates replenishment from spread without instrumenting legacy growth.
					const auto food = stocks(map);
					REQUIRE(deposits + depleted >= initialDeposits);
					const auto seeds = deposits + depleted - initialDeposits;
					REQUIRE(food + harvested >= initialFood + seeds);
					report["samples"].push_back(
						{{"seed", seed},
						 {"harvest_policy", deplete ? "deplete" : "reserve"},
						 {"variant", variant},
						 {"food", food},
						 {"deposits", deposits},
						 {"depleted", depleted},
						 {"seeded", seeds},
						 {"replenished", food + harvested - initialFood - seeds},
						 {"harvested", harvested}});
				}
		if (const char *path = std::getenv("GLOB2_GROWTH_ECOLOGY_OUTPUT"))
		{
			std::ofstream out(path);
			out << report.dump(2);
			REQUIRE(out.good());
		}
		std::cout << "GROWTH_ECOLOGY " << report.dump() << '\n';
	}
}

namespace ResourceGrowth { std::size_t calculateCountOutlined(const MapState::View &, MersenneTwister &, Batch &); }
TEST_CASE("outlined proposal write ablation [benchmark][resources]") {
 glob2test::HeadlessGlobals globals;
 Json report={{"samples",Json::array()}};
 for(int shift:{7,8,9}) for(const std::string scenario:{"sparse","dense","saturated","blocked","multi"}) {
  glob2test::HeadlessGame world({.wDec=shift,.hDec=shift,.header=true,.seed=713});
  auto &map=world.game.map;setup(map,scenario);map.resourceGrowthField();
  auto snapshot=world.game.snapshotStore().captureBoundary(world.game,ResourceGrowth::Pipeline::requirements());
  auto view=snapshot.view();ResourceGrowth::Batch emitted,counted;emitted.proposals.reserve(map.cellCount());
  for(unsigned seed=1;seed<=128;++seed) {
   MersenneTwister a(seed),b(seed);ResourceGrowth::calculate(view,a,emitted);
   auto n=ResourceGrowth::calculateCountOutlined(view,b,counted);
   REQUIRE(n==emitted.proposals.size());REQUIRE(counted.sampled==emitted.sampled);REQUIRE(a()==b());
  }
  for(int rep=-1;rep<10;++rep) for(int order=0;order<2;++order) {
   const bool countOnly=(rep+1+order)%2; std::vector<MersenneTwister> rngs;
   for(unsigned seed=1;seed<=1024;++seed) rngs.emplace_back(seed);
   MersenneTwister warm(713);ResourceGrowth::calculate(view,warm,emitted);
   Uint64 proposals=0,sampled=0;auto start=Clock::now();
   for(auto &rng:rngs) {
    if(countOnly){proposals+=ResourceGrowth::calculateCountOutlined(view,rng,counted);sampled+=counted.sampled;}
    else{ResourceGrowth::calculate(view,rng,emitted);proposals+=emitted.proposals.size();sampled+=emitted.sampled;}
   }
   auto elapsed=ns(start);
   report["samples"].push_back({{"size",1<<shift},{"scenario",scenario},{"repeat",rep},{"count_only",countOnly},{"elapsed_ns",elapsed},{"proposals",proposals},{"sampled",sampled}});
  }
 }
 std::ofstream output(std::getenv("GLOB2_WRITE_ABLATION_OUTPUT"));output<<report.dump(2);REQUIRE(output.good());
}

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
