// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "ResourceGrowth.h"
#include "gradient/GradientRuntime.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <nlohmann/json.hpp>
#include <latch>

namespace
{
ResourceId crop(Map &map, bool multi = false)
{
	auto document = nlohmann::json::parse(map.resourceRegistry().serialize());
	auto r = document["resources"][1];
	r["key"] = "growth-test";
	r["properties"]["ecology"] = "uniform";
	r["properties"]["growthRate"] = ResourceRateScale;
	r["properties"]["stockDependentGrowth"] = false;
	if (multi)
		r["yields"]["paper"] = {{"capacity", 5},
								{"initial", 2},
								{"growthRate", ResourceRateScale},
								{"consumption", "one"}};
	map.installResourceDefinitions(nlohmann::json{{"schemaVersion", 1}, {"resources", {r}}}.dump());
	return *map.resourceRegistry().find("growth-test");
}
ResourceGrowth::Proposal increment(Map &m, size_t at, ResourceId id,
								   MaterialId material = MaterialId::Food)
{
	ResourceGrowth::Proposal p;
	p.tile = at;
	p.type = resourceIndex(id);
	p.material = materialIndex(material);
	return p;
}
void seed(Map &map, ResourceId id)
{
	for (int y = 0; y < map.getH(); y += 2)
		for (int x = 0; x < map.getW(); x += 2)
			map.setResource(x, y, id, 0);
}
} // namespace
TEST_SUITE("ResourceGrowth")
{
	TEST_CASE("signed proposals use only current resource type and apply in order")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.loadDefaultRace = true, .header = true});
		auto &m = world.game.map;
		const auto id = crop(m);
		const auto at = m.coordToIndex(4, 4);
		auto positive = increment(m, at, id);
		auto negative = positive;
		negative.delta = -1;
		ResourceGrowth::Batch b;
		ResourceGrowth::Metrics metrics;
		b.proposals = {negative};
		ResourceGrowth::apply(m, b, metrics);
		CHECK(m.getResource(4, 4).type == NO_RES_TYPE);
		b.proposals = {positive};
		ResourceGrowth::apply(m, b, metrics);
		CHECK(m.materialAmountAt(at, MaterialId::Food) == 1);
		m.replaceResource(at, Resource{});
		m.setResource(4, 4, id, 0);
		m.setMaterialAmount(at, MaterialId::Food, 1);
		ResourceGrowth::apply(m, b, metrics);
		CHECK(m.materialAmountAt(at, MaterialId::Food) == 2);
		b.proposals = {negative};
		ResourceGrowth::apply(m, b, metrics);
		CHECK(m.materialAmountAt(at, MaterialId::Food) == 1);
		b.proposals = {negative, positive, positive};
		ResourceGrowth::apply(m, b, metrics);
		CHECK(m.materialAmountAt(at, MaterialId::Food) == 2);
		// A different deposit type rejects both signs, without applying old identity rules.
		m.setResource(4, 4, static_cast<ResourceId>(STONE), 0);
		const auto before = m.getResource(4, 4);
		b.proposals = {positive, negative};
		ResourceGrowth::apply(m, b, metrics);
		CHECK(m.getResource(4, 4).getUint32() == before.getUint32());
		// Conditions can change after calculation: publication intentionally does not recheck them.
		m.replaceResource(at, Resource{});
		m.setResourcesGrow(4, 4, false);
		m.setCellTerrain(4, 4, WATER);
		b.proposals = {positive};
		ResourceGrowth::apply(m, b, metrics);
		CHECK(m.materialAmountAt(at, MaterialId::Food) == 1);
	}
	TEST_CASE("material deltas cap independently and seed exactly one unit")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.loadDefaultRace = true, .header = true});
		auto &m = world.game.map;
		const auto id = crop(m, true);
		const auto at = m.coordToIndex(4, 4);
		auto food = increment(m, at, id);
		auto paper = increment(m, at, id, MaterialId::Paper);
		ResourceGrowth::Batch b;
		ResourceGrowth::Metrics metrics;
		b.proposals = {paper};
		ResourceGrowth::apply(m, b, metrics);
		CHECK(m.materialAmountAt(at, MaterialId::Paper) == 1);
		CHECK(m.materialAmountAt(at, MaterialId::Food) == 0);
		b.proposals = {food, food, food, food, food, food, paper};
		ResourceGrowth::apply(m, b, metrics);
		CHECK(m.materialAmountAt(at, MaterialId::Food) == 5);
		CHECK(m.materialAmountAt(at, MaterialId::Paper) == 2);
		CHECK(metrics.clamped == 1);
		food.delta = -1;
		paper.delta = -1;
		b.proposals = {food, paper, paper, paper};
		ResourceGrowth::apply(m, b, metrics);
		CHECK(m.materialAmountAt(at, MaterialId::Food) == 4);
		CHECK(m.materialAmountAt(at, MaterialId::Paper) == 0);
		REQUIRE(world.addUnit(WORKER, 8, 8));
		b.proposals = {increment(m, m.coordToIndex(8, 8), id)};
		ResourceGrowth::apply(m, b, metrics);
		CHECK(m.getResource(8, 8).type == resourceIndex(id));
		const auto &stats = world.game.teams[0]->stats.measurements;
		CHECK(stats.growthGlobal[0][materialIndex(MaterialId::Food)] == 1);
		CHECK(stats.growthGlobal[0][materialIndex(MaterialId::Paper)] == 1);
		CHECK(stats.growthGlobal[1][materialIndex(MaterialId::Food)] == 6);
		CHECK(stats.growthGlobal[1][materialIndex(MaterialId::Paper)] == 2);
		CHECK(stats.growthGlobal[2][materialIndex(MaterialId::Food)] == 1);
		CHECK(stats.growthGlobal[2][materialIndex(MaterialId::Paper)] == 2);
	}
	TEST_CASE("proposal capacity grows safely and is retained for reuse")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.header = true});
		auto &m = world.game.map;
		seed(m, crop(m, true));
		m.resourceGrowthField();
		ResourceGrowth::Batch b;
		MersenneTwister rng(48);
		ResourceGrowth::calculate(m.stateView(), rng, b);
		REQUIRE(b.proposals.size() > 0);
		CHECK(b.capacityGrew);
		const auto capacity = b.proposals.capacity();
		rng.seed(48);
		ResourceGrowth::calculate(m.stateView(), rng, b);
		CHECK_FALSE(b.capacityGrew);
		CHECK(b.proposals.capacity() == capacity);
	}
	TEST_CASE("legacy pending masks migrate and signed pending proposals validate on load")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.header = true});
		auto &m = world.game.map;
		const auto id = crop(m, true);
		const auto at = m.coordToIndex(4, 4);
		for (int version : {144, 145})
			for (int delta : {1, -1, 2})
			{
				m.setResource(4, 4, id, 0);
				m.setMaterialAmount(at, MaterialId::Food, 2);
				auto *bytes = new GAGCore::MemoryStreamBackend;
				GAGCore::BinaryOutputStream out(bytes);
				out.writeEnterSection("resourceGrowth");
				out.writeUint8(8, "delay");
				out.writeUint8(1, "count");
				out.writeEnterSection(0);
				out.writeUint32(0, "sourceTick");
				out.writeUint32(13, "seed");
				out.writeUint32(m.resourceRegistry().checksum(), "catalog");
				out.writeUint8(8, "remaining");
				out.writeUint32(1, "proposals");
				out.writeEnterSection(0);
				out.writeUint32(at, "tile");
				if (version == 144)
					out.writeUint32(123, "incarnation");
				out.writeUint16(resourceIndex(id), "type");
				if (version == 144)
				{
					out.writeUint8(3, "variety");
					out.writeUint8(0, "spread");
					out.writeUint16(materialBit(MaterialId::Food) | materialBit(MaterialId::Paper),
									"incrementMask");
				}
				else
				{
					out.writeUint8(materialIndex(MaterialId::Food), "material");
					out.writeSint8(delta, "delta");
				}
				out.writeLeaveSection();
				out.writeLeaveSection();
				out.writeLeaveSection();
				out.flush();
				auto *copy = new GAGCore::MemoryStreamBackend(*bytes);
				copy->seekFromStart(0);
				GAGCore::BinaryInputStream in(copy);
				ResourceGrowth::Pipeline p;
				if (version == 145 && delta == 2)
				{
					CHECK_THROWS(p.load(&in, m, 0, version));
					continue;
				}
				p.load(&in, m, 0, version);
				p.publish(m, 7);
				CHECK(p.count() == 1);
				p.publish(m, 8);
				CHECK(p.count() == 0);
				CHECK(m.materialAmountAt(at, MaterialId::Food) == (version == 144 ? 3 : 2 + delta));
				if (version == 144)
					CHECK(m.materialAmountAt(at, MaterialId::Paper) == 3);
			}
	}

	TEST_CASE("kernel is immutable deterministic and responds to disabled growth and scarcity")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.header = true});
		auto &m = world.game.map;
		const auto id = crop(m, true);
		seed(m, id);
		const auto snapshot = world.game.snapshotStore().captureBoundary(
			world.game, ResourceGrowth::Pipeline::requirements());
		const auto before = m.checkSum(true);
		ResourceGrowth::Batch a, b;
		MersenneTwister r1(18), r2(18);
		ResourceGrowth::calculate(snapshot.view(), r1, a);
		ResourceGrowth::calculate(snapshot.view(), r2, b);
		REQUIRE(a.proposals.size() > 0);
		CHECK(a.proposals.size() == b.proposals.size());
		CHECK(r1 == r2);
		CHECK(m.checkSum(true) == before);
		auto view = snapshot.view();
		view.resourceGrowthDisabled = true;
		ResourceGrowth::calculate(view, r1, b);
		CHECK(b.proposals.empty());
		Uint64 normal = 0, scarce = 0;
		for (int seed = 0; seed < 100; ++seed)
		{
			r1.seed(seed);
			r2.seed(seed);
			view.resourceGrowthDisabled = false;
			view.resourceScarcityLevel = 0;
			ResourceGrowth::calculate(view, r1, a);
			view.resourceScarcityLevel = 3;
			ResourceGrowth::calculate(view, r2, b);
			normal += a.proposals.size();
			scarce += b.proposals.size();
		}
		CHECK(scarce < normal / 4);
		CHECK(scarce > 0);
	}
	TEST_CASE("owner and shared deadlines match and pending work survives saving")
	{
		glob2test::HeadlessGlobals globals;
		for (unsigned delay : {1u, 3u, 8u})
		{
			glob2test::HeadlessGame owner({.loadDefaultRace = true, .header = true, .seed = 48});
			auto &m = owner.game.map;
			seed(m, crop(m));
			m.configureCompute(1, 0);
			m.configureResourceGrowth(delay, false);
			owner.step(12);
			auto *bytes = new GAGCore::MemoryStreamBackend;
			GAGCore::BinaryOutputStream output(bytes);
			owner.game.save(&output, false, "Growth continuation");
			output.flush();
			auto *copy = new GAGCore::MemoryStreamBackend(*bytes);
			copy->seekFromStart(0);
			GAGCore::BinaryInputStream input(copy);
			glob2test::HeadlessGame shared({.loadDefaultRace = true, .header = true});
			REQUIRE(shared.game.load(&input));
			shared.game.map.configureCompute(4, 0);
			shared.game.map.configureResourceGrowth(delay, true);
			REQUIRE(shared.game.checkSum(nullptr, nullptr, nullptr, true) ==
					owner.game.checkSum(nullptr, nullptr, nullptr, true));
			CHECK_THROWS(shared.game.map.configureResourceGrowth(delay == 1 ? 3 : 1, true));
			for (int tick = 0; tick < 40; ++tick)
			{
				owner.step();
				shared.step();
				CHECK(shared.game.checkSum(nullptr, nullptr, nullptr, true) ==
					  owner.game.checkSum(nullptr, nullptr, nullptr, true));
			}
		}
	}
	TEST_CASE("pipeline waits at exact deadline and pause does not publish")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.header = true});
		auto &m = world.game.map;
		seed(m, crop(m));
		m.configureResourceGrowth(3, false);
		world.step();
		CHECK(m.resourceGrowthMetrics().published == 0);
		world.game.anyPlayerWaited = true;
		world.step(5);
		CHECK(world.game.stepCounter == 1);
		CHECK(m.resourceGrowthMetrics().published == 0);
		world.game.anyPlayerWaited = false;
		world.step(3);
		CHECK(world.game.stepCounter == 4);
		CHECK(m.resourceGrowthMetrics().published == 0);
		world.step();
		CHECK(m.resourceGrowthMetrics().published == 1);
	}
	TEST_CASE("frozen input outlives terrain and resource edits and wrapping remains valid")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.header = true});
		auto &m = world.game.map;
		seed(m, crop(m));
		const auto snapshot = world.game.snapshotStore().captureBoundary(
			world.game, ResourceGrowth::Pipeline::requirements());
		ResourceGrowth::Batch a, b;
		MersenneTwister first(991), second(991);
		ResourceGrowth::calculate(snapshot.view(), first, a);
		for (int y = 0; y < m.getH(); ++y)
			for (int x = 0; x < m.getW(); ++x)
			{
				m.replaceResource(x, y, Resource{});
				m.setCellTerrain(x, y, WATER);
			}
		ResourceGrowth::calculate(snapshot.view(), second, b);
		REQUIRE(a.proposals.size() == b.proposals.size());
		for (size_t i = 0; i < a.proposals.size(); ++i)
		{
			CHECK(a.proposals[i].tile == b.proposals[i].tile);
			CHECK(a.proposals[i].tile < snapshot.resources->cells.size());
			CHECK(a.proposals[i].material == b.proposals[i].material);
		}
		ResourceGrowth::Metrics metrics;
		ResourceGrowth::apply(m, b, metrics);
		CHECK(metrics.accepted > 0);
	}
	TEST_CASE("light checksums leave owner work deferred and world replacement discards it")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.header = true});
		auto &m = world.game.map;
		seed(m, crop(m));
		m.configureResourceGrowth(8, false);
		world.step();
		REQUIRE(m.gradientRuntime->growth.count() == 1);
		CHECK(m.resourceGrowthMetrics().computeNs == 0);
		m.checkSum(false);
		CHECK(m.resourceGrowthMetrics().computeNs == 0);
		m.finishResourceGrowth();
		CHECK(m.resourceGrowthMetrics().sampled > 0);
		CHECK(m.resourceGrowthMetrics().published == 0);
		m.setSize(5, 5, GRASS);
		CHECK(m.gradientRuntime->growth.count() == 0);
	}

	TEST_CASE("later worker completion cannot publish ahead of an earlier owner batch")
	{
		if (!GAGCore::ThreadSupport::available)
			return;
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.header = true});
		auto &m = world.game.map;
		seed(m, crop(m));
		m.configureCompute(2, 0);
		ResourceGrowth::Pipeline pipeline;
		pipeline.shared = false;
		auto first = world.game.snapshotStore().captureBoundary(
			world.game, ResourceGrowth::Pipeline::requirements());
		pipeline.stage(first.tick, 1);
		pipeline.prepare(first, m.computeExecutor());
		world.game.stepCounter = 1;
		auto second = world.game.snapshotStore().captureBoundary(
			world.game, ResourceGrowth::Pipeline::requirements());
		pipeline.shared = true;
		pipeline.stage(second.tick, 2);
		pipeline.prepare(second, m.computeExecutor());
		std::latch finished(1);
		ComputeExecutor::Group signal{
			1,
			{[](void *p, size_t) { static_cast<std::latch *>(p)->count_down(); }, &finished},
			ComputeExecutor::NoLane};
		auto marker =
			m.computeExecutor().submit(std::span(&signal, 1), ComputeExecutor::Placement::Shared);
		finished.wait();
		m.computeExecutor().join(marker);
		pipeline.publish(m, 7);
		CHECK(pipeline.metrics.published == 0);
		pipeline.publish(m, 8);
		CHECK(pipeline.metrics.published == 1);
		CHECK(pipeline.count() == 1);
		pipeline.publish(m, 9);
		CHECK(pipeline.metrics.published == 2);
		CHECK(pipeline.count() == 0);
	}
}

namespace ReferenceGrowth
{ using namespace ResourceGrowth; }
namespace ReferenceGrowth
{
namespace
{
Uint64 now()
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
			   std::chrono::steady_clock::now().time_since_epoch())
		.count();
}
// Publication already knows the one changed material and its exact delta.
// Avoid constructing and comparing all material stocks for each unit operation.
void recordDelta(Map &map, const Proposal &p, bool newTile)
{
	const int x = p.tile % map.getW(), y = p.tile / map.getW();
	std::array<Uint32, GROWTH_COVERAGE_BANDS> coverage;
	for (int band = 0; band < GROWTH_COVERAGE_BANDS; ++band)
		coverage[band] = map.teamsWithBuildingsNear(x, y, band);
	for (int t = 0; t < map.game->mapHeader.getNumberOfTeams(); ++t)
	{
		auto *team = map.game->teams[t];
		if (!team)
			continue;
		auto &m = team->stats.measurements;
		m.growthGlobal[0][p.material] += newTile;
		m.growthGlobal[1][p.material] += p.delta > 0;
		m.growthGlobal[2][p.material] += p.delta < 0;
		for (int band = 0; band < GROWTH_COVERAGE_BANDS; ++band)
			if (coverage[band] & (Uint32(1) << t))
			{
				m.growthTiles[band][p.material] += newTile;
				m.growthAmount[band][p.material] += p.delta > 0;
				m.growthReduction[band][p.material] += p.delta < 0;
			}
	}
}
} // namespace
void apply(Map &map, const Batch &batch, Metrics &metrics)
{
	const auto start = now();
	if (batch.proposals.empty())
		return;
	if (map.game->gameHeader.isResourceGrowthDisabled())
	{
		metrics.rejected += batch.proposals.size();
		metrics.publicationNs += now() - start;
		return;
	}
	const auto v = map.cellView();
	map.rebuildGrowthCoverage();
	for (const auto &p : batch.proposals)
	{
		// Kernel output and save loading validate indices, materials and +/-1 deltas.
		assert(p.tile < v.resources.size());
		const auto oldType = v.resources[p.tile].resource.type;
		if (oldType != p.type && (oldType != NO_RES_TYPE || p.delta < 0))
		{
			++metrics.rejected;
			continue;
		}
		const auto capacity =
			v.resourceRegistry->yields(static_cast<ResourceId>(p.type))[p.material].capacity;
		const unsigned amount =
			oldType == NO_RES_TYPE ? 0 : map.materialAmountAtSlot(p.tile, p.material);
		if ((p.delta > 0 && amount >= capacity) || (p.delta < 0 && amount == 0))
		{
			metrics.clamped += p.delta > 0;
			++metrics.rejected;
			continue;
		}
		if (oldType == NO_RES_TYPE)
		{
			// A seed is exactly one unit, including for multi-material deposits.
			std::array<Uint16, MaterialCount> stocks{};
			stocks[p.material] = 1;
			map.replaceResource(p.tile, Resource{p.type, 0, 1, 0}, &stocks);
			++metrics.tilesAdded;
		}
		else
			map.setMaterialAmountSlot(p.tile, p.material, amount + p.delta);
		++metrics.accepted;
		metrics.stockAdded += p.delta > 0;
		recordDelta(map, p, oldType == NO_RES_TYPE);
	}
	metrics.publicationNs += now() - start;
}


}

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
  for(int n=0;n<512;++n)proposals.proposals.push_back({Uint32(rng()%a.game.map.cellCount()),Uint16(resourceIndex(type)),Uint8(rng()%2?materialIndex(MaterialId::Food):materialIndex(MaterialId::Paper)),Sint8(rng()%2?1:-1)});
  ReferenceGrowth::apply(a.game.map,proposals,x);ResourceGrowth::apply(b.game.map,proposals,y);
  for(int t=0;t<4;++t)REQUIRE(a.game.teams[t]->stats.measurements==b.game.teams[t]->stats.measurements);
  REQUIRE(x.accepted==y.accepted);REQUIRE(x.rejected==y.rejected);REQUIRE(x.clamped==y.clamped);REQUIRE(x.stockAdded==y.stockAdded);REQUIRE(x.tilesAdded==y.tilesAdded);
  REQUIRE(std::equal(a.game.map.cellView().materialSourceCounts.begin(),a.game.map.cellView().materialSourceCounts.end(),b.game.map.cellView().materialSourceCounts.begin()));
  for(size_t i=0;i<a.game.map.cellCount();++i) {
   REQUIRE(a.game.map.getResource(i)==b.game.map.getResource(i));
   REQUIRE(a.game.map.materialStocksAt(i)==b.game.map.materialStocksAt(i));
   Uint32 total=0;for(auto amount:b.game.map.materialStocksAt(i))total+=amount;
   REQUIRE(b.game.map.getResource(i).amount==total);
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
 map.replaceResource(0,Resource{Uint16(resourceIndex(id)),0,1,0});
 for(unsigned food:{0u,1u,65534u,65535u,0u})for(unsigned paper:{65535u,65534u,1u,0u}) {
  map.setMaterialAmount(0,MaterialId::Food,food);map.setMaterialAmount(0,MaterialId::Paper,paper);
  REQUIRE(map.getResource(0).amount==food+paper);
  auto before=map.changes(MapState::TrackedArray::Resources).generation;
  map.setMaterialAmount(0,MaterialId::Paper,paper);
  REQUIRE(map.changes(MapState::TrackedArray::Resources).generation==before);
 }
}
