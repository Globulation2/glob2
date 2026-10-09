// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "Engine.h"
#include "ResourceGrowth.h"
#include "Version.h"
#include "MapAssetBundle.h"
#include "BuildingArtwork.h"
#include "io/MapSaveLayout.h"
#include "gradient/GradientRuntime.h"
#include <BinaryStream.h>
#include <TextStream.h>
#include <StreamBackend.h>
#include <nlohmann/json.hpp>
#include <latch>
#include <bit>
#include <tuple>

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
    TEST_CASE("source decisions cannot shift scan sampling or another source")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.header=true});
        auto& map=world.game.map;
        const auto id=crop(map);
        auto definitions=nlohmann::json::parse(map.resourceRegistry().serialize());
        for (auto& resource:definitions["resources"])
            if (resource["key"]=="growth-test") {
                resource["properties"]["spreadRate"]=0;
                resource["properties"]["growthRate"]=ResourceRateScale/2;
            }
        map.installResourceDefinitions(definitions.dump());
        seed(map,id);
        ResourceGrowth::Batch dense, changed;
        MersenneTwister first(991), second(991);
        ResourceGrowth::calculate(map.stateView(),first,dense);
        REQUIRE(!dense.proposals.empty());
        const auto removed=dense.proposals.front().tile;
        map.replaceResource(removed,Resource{});
        ResourceGrowth::calculate(map.stateView(),second,changed);
        CHECK(first==second);
        CHECK(dense.sampled==changed.sampled);
        std::vector<std::tuple<Uint32,Uint16,Uint8,Sint8>> expected,actual;
        for (const auto& p:dense.proposals) if (p.tile!=removed)
            expected.emplace_back(p.tile,p.type,p.material,p.delta);
        for (const auto& p:changed.proposals) if (p.tile!=removed)
            actual.emplace_back(p.tile,p.type,p.material,p.delta);
        CHECK(actual==expected);
    }
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
		// Conditions can change after calculation: retain growth permissions at publication.
		m.replaceResource(at, Resource{});
		m.setResourcesGrow(4, 4, false);
		m.setVertexTerrain(4, 4, WATER);
		b.proposals = {positive};
		ResourceGrowth::apply(m, b, metrics);
		CHECK(m.materialAmountAt(at, MaterialId::Food) == 0);
	}
	TEST_CASE("material deltas cap independently and create configured seed stocks")
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
		CHECK(m.materialAmountAt(at, MaterialId::Paper) == 2);
		CHECK(m.materialAmountAt(at, MaterialId::Food) == 1);
		b.proposals = {food, food, food, food, food, food, paper};
		ResourceGrowth::apply(m, b, metrics);
		CHECK(m.materialAmountAt(at, MaterialId::Food) == 5);
		CHECK(m.materialAmountAt(at, MaterialId::Paper) == 3);
		CHECK(metrics.clamped == 2);
		food.delta = -1;
		paper.delta = -1;
		b.proposals = {food, paper, paper, paper};
		ResourceGrowth::apply(m, b, metrics);
		CHECK(m.materialAmountAt(at, MaterialId::Food) == 4);
		CHECK(m.materialAmountAt(at, MaterialId::Paper) == 0);
		REQUIRE(world.addUnit(WORKER, 8, 8));
		b.proposals = {increment(m, m.coordToIndex(8, 8), id)};
		ResourceGrowth::apply(m, b, metrics);
		CHECK(m.getResource(8, 8).type == NO_RES_TYPE);
		const auto &stats = world.game.teams[0]->stats.measurements;
		CHECK(stats.growthGlobal[0][materialIndex(MaterialId::Food)] == 1);
		CHECK(stats.growthGlobal[0][materialIndex(MaterialId::Paper)] == 1);
		CHECK(stats.growthGlobal[1][materialIndex(MaterialId::Food)] == 5);
		CHECK(stats.growthGlobal[1][materialIndex(MaterialId::Paper)] == 3);
		CHECK(stats.growthGlobal[2][materialIndex(MaterialId::Food)] == 1);
		CHECK(stats.growthGlobal[2][materialIndex(MaterialId::Paper)] == 3);
	}
	TEST_CASE("proposal capacity grows safely and is retained for reuse")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.header = true});
		auto &m = world.game.map;
		seed(m, crop(m, true));
		m.resourceGrowthField();
		ResourceGrowth::Batch b;
		MersenneTwister rng(991);
		ResourceGrowth::calculate(m.stateView(), rng, b);
		REQUIRE(b.proposals.size() > 0);
		CHECK(b.capacityGrew);
		const auto capacity = b.proposals.capacity();
		rng.seed(991);
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
		MersenneTwister r1(991), r2(991);
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
	TEST_CASE("zero-worker and shared deadlines match and pending work survives saving")
	{
		glob2test::HeadlessGlobals globals;
		for (unsigned delay : {1u, 3u, 8u})
		{
			glob2test::HeadlessGame fallback({.loadDefaultRace = true, .header = true, .seed = 48});
			auto &m = fallback.game.map;
			seed(m, crop(m));
			m.configureCompute(1);
			m.setResourceGrowthDelay(delay);
			fallback.step(12);
			auto *bytes = new GAGCore::MemoryStreamBackend;
			GAGCore::BinaryOutputStream output(bytes);
			fallback.game.save(&output, false, "Growth continuation");
			output.flush();
			auto *copy = new GAGCore::MemoryStreamBackend(*bytes);
			copy->seekFromStart(0);
			GAGCore::BinaryInputStream input(copy);
			glob2test::HeadlessGame shared({.loadDefaultRace = true, .header = true});
			REQUIRE(shared.game.load(&input));
			shared.game.map.configureCompute(4);
			shared.game.map.setResourceGrowthDelay(delay);
			REQUIRE(shared.game.checkSum(nullptr, nullptr, nullptr, true) ==
					fallback.game.checkSum(nullptr, nullptr, nullptr, true));
			CHECK_THROWS(shared.game.map.setResourceGrowthDelay(delay == 1 ? 3 : 1));
			auto foodStocks = [&]()
			{
				Uint64 total = 0;
				for (size_t cell = 0; cell < shared.game.map.cellCount(); ++cell)
					total += shared.game.map.materialAmountAt(cell, MaterialId::Food);
				return total;
			};
			const auto initialFood = foodStocks();
			const auto initialAdded =
				shared.game.teams[0]->stats.measurements.growthGlobal[1][materialIndex(MaterialId::Food)];
			for (int tick = 0; tick < 40; ++tick)
			{
				fallback.step();
				shared.step();
				const auto &metrics = shared.game.map.resourceGrowthMetrics();
				CHECK(metrics.publishedProposals == metrics.accepted + metrics.rejected);
				CHECK(foodStocks() == initialFood + metrics.stockAdded);
				CHECK(shared.game.teams[0]->stats.measurements.growthGlobal[1][materialIndex(MaterialId::Food)] ==
					  initialAdded + metrics.stockAdded);
				CHECK(shared.game.checkSum(nullptr, nullptr, nullptr, true) ==
					  fallback.game.checkSum(nullptr, nullptr, nullptr, true));
			}
		}
	}
	TEST_CASE("setting the delay leaves reservations and unfinished work untouched")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.header = true});
		auto &map = world.game.map;
		seed(map, crop(map));
		map.configureCompute(1); // No worker threads.
		auto &pipeline = map.gradientRuntime->growth;
		pipeline.stage(0, 48);
		map.setResourceGrowthDelay(8);
		CHECK_THROWS(map.setResourceGrowthDelay(3));
		CHECK(pipeline.needsPreparation());
		CHECK(pipeline.count() == 0);
		auto snapshot = world.game.snapshotStore().captureBoundary(
			world.game, ResourceGrowth::Pipeline::requirements());
		pipeline.prepare(snapshot, map.computeExecutor());
		map.setResourceGrowthDelay(8);
		CHECK_THROWS(map.setResourceGrowthDelay(3));
		CHECK(pipeline.count() == 1);
		CHECK_FALSE(map.computeExecutor().finished(pipeline.pending.front()->work));
		CHECK(pipeline.metrics.computeNs == 0);
		pipeline.finish();
		CHECK(pipeline.metrics.sampled > 0);
	}
	TEST_CASE("pipeline waits at exact deadline and pause does not publish")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.header = true});
		auto &m = world.game.map;
		seed(m, crop(m));
		m.setResourceGrowthDelay(3);
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
				m.setVertexTerrain(x, y, WATER);
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
		CHECK(metrics.accepted == 0);
		CHECK(metrics.rejected == b.proposals.size());
	}
	TEST_CASE("light checksums leave zero-worker work deferred and world replacement discards it")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.header = true});
		auto &m = world.game.map;
		seed(m, crop(m));
		m.configureCompute(1);
		m.setResourceGrowthDelay(8);
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

	TEST_CASE("publication accounts for proposals rejected by a different world")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.header = true});
		auto &map = world.game.map;
		seed(map, crop(map));
		ResourceGrowth::Pipeline pipeline;

		auto snapshot = world.game.snapshotStore().captureBoundary(
			world.game, ResourceGrowth::Pipeline::requirements());
		pipeline.stage(snapshot.tick, 991);
		pipeline.prepare(snapshot, map.computeExecutor());
		pipeline.finish();
		REQUIRE(pipeline.metrics.proposals > 0);
		glob2test::HeadlessGame replacement({.header = true});
		pipeline.publish(replacement.game.map, snapshot.tick + pipeline.delay);
		CHECK(pipeline.metrics.publishedProposals == pipeline.metrics.proposals);
		CHECK(pipeline.metrics.rejected == pipeline.metrics.publishedProposals);
		CHECK(pipeline.metrics.accepted == 0);
	}

	TEST_CASE("later completed work cannot publish ahead of an earlier batch")
	{
		if (!GAGCore::ThreadSupport::available)
			return;
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.header = true});
		auto &map = world.game.map;
		seed(map, crop(map));
		map.configureCompute(3);
		auto &executor = map.computeExecutor();
		ResourceGrowth::Pipeline pipeline;
		auto first = world.game.snapshotStore().captureBoundary(
			world.game, ResourceGrowth::Pipeline::requirements());
		pipeline.stage(first.tick, 1);
		pipeline.prepare(first, executor);
		pipeline.finish();
		{
			// Hold the first batch's completion fence after calculation. This
			// forces out-of-order completion without a production scheduling hook.
			struct Fence
			{
				ComputeExecutor &executor;
				std::latch entered{1}, release{1};
				ComputeExecutor::Batch work;
				~Fence() { release.count_down(); executor.join(work); }
			} fence{executor};
			ComputeExecutor::Group blocked{1, {[](void *p, size_t) {
				auto &fence = *static_cast<Fence *>(p);
				fence.entered.count_down();
				fence.release.wait();
			}, &fence}, ComputeExecutor::NoLane};
			fence.work = executor.submit(std::span(&blocked, 1), ComputeExecutor::advanceDue(8));
			pipeline.pending.front()->work = fence.work;
			fence.entered.wait();
			world.game.stepCounter = 1;
			auto second = world.game.snapshotStore().captureBoundary(
				world.game, ResourceGrowth::Pipeline::requirements());
			pipeline.stage(second.tick, 2);
			pipeline.prepare(second, executor);
			// Join only the later job; the first worker remains blocked.
			executor.join(pipeline.pending.back()->work);
			CHECK_FALSE(executor.finished(pipeline.pending.front()->work));
			pipeline.publish(map, 7);
			CHECK(pipeline.metrics.published == 0);
		}
		pipeline.publish(map, 8);
		CHECK(pipeline.metrics.published == 1);
		CHECK(pipeline.count() == 1);
		pipeline.publish(map, 9);
		CHECK(pipeline.metrics.published == 2);
		CHECK(pipeline.count() == 0);
	}

}

TEST_CASE("format 144 and 145 packed growth headers cannot alias artwork lengths" * doctest::test_suite("ResourceGrowth"))
{
    for (size_t cells : {256u, 1024u, 4096u, 16384u})
    {
        CHECK(MapSaveLayout::legacyGrowthMapHeader(0, std::min(cells, size_t(4096)), cells));
        CHECK(MapSaveLayout::legacyGrowthMapHeader(1, 1, cells));
        CHECK(MapSaveLayout::legacyGrowthMapHeader(2, 17, cells));
        for (unsigned tag : {0u, 1u, 2u})
            CHECK_FALSE(MapSaveLayout::legacyGrowthMapHeader(0, tag, cells)); // Empty artwork, then packed data.
        for (Uint32 length : {1u, 16u, 255u, 256u, 4096u, 16777216u})
            for (Uint32 first : {Uint32('{'), Uint32(' '), Uint32('\t'), Uint32('\r'), Uint32('\n'), 239u})
                CHECK_FALSE(MapSaveLayout::legacyGrowthMapHeader(length >> 24, (length << 8) | first, cells));
    }
}

TEST_CASE("text save layout probes are relative and preserve section state" * doctest::test_suite("ResourceGrowth"))
{
    auto *bytes = new GAGCore::MemoryStreamBackend;
    GAGCore::TextOutputStream output(bytes);
    output.writeEnterSection("outer");
    output.writeEnterSection("Map");
    output.writeEnterSection("customAssets");
    output.writeUint32(0, "length");
    output.writeLeaveSection(3);
    output.flush();
    GAGCore::MemoryStreamBackend saved(bytes->takeContents());
    GAGCore::TextInputStream input(&saved);
    CHECK_FALSE(input.hasField("customAssets.length"));
    input.readEnterSection("outer");
    input.readEnterSection("Map");
    CHECK(input.hasField("customAssets.length"));
    CHECK_FALSE(input.hasField("resourceIncarnations.count"));
    input.readEnterSection("customAssets");
    CHECK(input.hasField("length"));
    CHECK(input.readUint32("length") == 0);
    input.readLeaveSection(3);
}

TEST_CASE("both format 144 and 145 lineages and compact growth saves retain continuation [save-format]" * doctest::test_suite("ResourceGrowth"))
{
    glob2test::GlobalsOptions options;
    options.loadStrings = true;
    glob2test::HeadlessGlobals globals(options);
    for (const auto name : {"growth144.game.gz", "growth145.game.gz", "growth146.game.gz", "growth148.game.gz", "vertex148.game.gz", "artwork144.game.gz", "empty-artwork144.game.gz", "building145.game.gz", "empty-building145.game.gz"})
    {
        CAPTURE(name);
        glob2test::HeadlessGame original({.loadDefaultRace = true, .header = true});
        GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(glob2test::readFile(
            glob2test::inflated(std::string("resources/growth-save-layout/") + name))));
        REQUIRE(original.game.load(&input));
        const bool growth = std::string_view(name).starts_with("growth");
        CHECK((original.game.map.gradientRuntime->growth.count() != 0) == growth);
        CHECK(original.game.map.frozenAssetBundle()->isEmpty() == (std::string_view(name) != "artwork144.game.gz"));
        CHECK(bool(original.game.gameHeader.getBuildingArtwork()) == (std::string_view(name) == "building145.game.gz"));
        const auto artwork = original.game.gameHeader.getBuildingArtwork() ? original.game.gameHeader.getBuildingArtwork()->bytes() : std::string{};
        const auto pending = original.game.map.gradientRuntime->growth.count();
        auto *bytes = new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream output(bytes);
        original.game.save(&output, false, "layout continuation");
        output.flush();
        CHECK(original.game.map.gradientRuntime->growth.count() == pending);
        glob2test::HeadlessGame restored({.loadDefaultRace = true, .header = true});
        GAGCore::BinaryInputStream saved(new GAGCore::MemoryStreamBackend(bytes->takeContents()));
        REQUIRE(restored.game.load(&saved));
        CHECK(restored.game.map.gradientRuntime->growth.count() == pending);
        CHECK((restored.game.gameHeader.getBuildingArtwork() ? restored.game.gameHeader.getBuildingArtwork()->bytes() : std::string{}) == artwork);
        CHECK(restored.game.map.frozenAssetBundle()->serialize() == original.game.map.frozenAssetBundle()->serialize());
        // Save version is the only intentional header checksum difference.
        const auto headerDelta = std::rotr(original.game.mapHeader.checkSum() ^ restored.game.mapHeader.checkSum(),
            4 + original.game.mapHeader.getNumberOfTeams() + original.game.gameHeader.getNumberOfPlayers());
        original.game.map.configureCompute(1);
        original.game.map.setResourceGrowthDelay(8);
        restored.game.map.configureCompute(4);
        restored.game.map.setResourceGrowthDelay(8);
        for (unsigned tick = 0; tick < 24; ++tick)
        {
            CHECK((original.game.checkSum(nullptr, nullptr, nullptr, true) ^ headerDelta) ==
                restored.game.checkSum(nullptr, nullptr, nullptr, true));
            original.step();
            restored.step();
        }
    }
}

TEST_CASE("configured seeds preserve rates variety collisions and saved output" * doctest::test_suite("ResourceGrowth"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.header = true});
    auto &map = world.game.map;
    const auto id = crop(map, true);
    auto catalog = nlohmann::json::parse(map.resourceRegistry().serialize());
    for (auto &resource : catalog["resources"])
        if (resource["key"] == "growth-test") resource["yields"]["paper"]["growthRate"] = 0;
    map.installResourceDefinitions(catalog.dump());
    for (int y = 0; y < map.getH(); y += 2)
        for (int x = 0; x < map.getW(); x += 2)
            map.replaceResource(map.coordToIndex(x, y), Resource{Uint16(resourceIndex(id)), 3, 1, 0});
    auto snapshot = world.game.snapshotStore().captureBoundary(world.game, ResourceGrowth::Pipeline::requirements());
    ResourceGrowth::Batch batch;
    MersenneTwister random(991);
    ResourceGrowth::calculate(snapshot.view(), random, batch);
    const auto found = std::find_if(batch.proposals.begin(), batch.proposals.end(),
        [](const auto &p) { return p.kind == ResourceGrowth::Proposal::Kind::Seed; });
    REQUIRE(found != batch.proposals.end());
    const auto seedProposal = *found;
    CHECK(seedProposal.variety == 3);
    CHECK((seedProposal.incrementMask & materialBit(MaterialId::Paper)) == 0);
    const auto at = seedProposal.tile;
    ResourceGrowth::Pipeline pipeline;

    pipeline.stage(snapshot.tick, 991);
    pipeline.prepare(snapshot, map.computeExecutor());
    auto *bytes = new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(bytes);
    pipeline.save(&output, 0);
    output.flush();
    CHECK(map.getResource(at).type == NO_RES_TYPE);
    auto *copy = new GAGCore::MemoryStreamBackend(*bytes);
    copy->seekFromStart(0);
    GAGCore::BinaryInputStream input(copy);
    ResourceGrowth::Pipeline restored;
    restored.load(&input, map, 0, VERSION_MINOR);
    restored.publish(map, 7);
    CHECK(map.getResource(at).type == NO_RES_TYPE);
    restored.publish(map, 8);
    CHECK(map.getResource(at).variety == 3);
    CHECK(map.materialAmountAt(at, MaterialId::Paper) == 2);
    CHECK(restored.metrics.publishedProposals == restored.metrics.accepted + restored.metrics.rejected);

    // A colliding seed applies only its frozen replenishment mask, never initial stocks.
    map.setMaterialAmount(at, MaterialId::Food, 4);
    map.setMaterialAmount(at, MaterialId::Paper, 1);
    batch.proposals = {seedProposal};
    ResourceGrowth::Metrics metrics;
    ResourceGrowth::apply(map, batch, metrics);
    CHECK(map.materialAmountAt(at, MaterialId::Food) == 5);
    CHECK(map.materialAmountAt(at, MaterialId::Paper) == 1);
    batch.proposals[0].incrementMask = 0;
    ResourceGrowth::apply(map, batch, metrics);
    CHECK(metrics.rejected == 1);
    CHECK(map.materialAmountAt(at, MaterialId::Paper) == 1);
    batch.proposals[0].incrementMask = materialBit(MaterialId::Food) | materialBit(MaterialId::Paper);
    ResourceGrowth::apply(map, batch, metrics);
    CHECK(map.materialAmountAt(at, MaterialId::Food) == 5);
    CHECK(map.materialAmountAt(at, MaterialId::Paper) == 2);

    // Replenishment in flight can recreate a deposit, using its configured seed stocks.
    map.replaceResource(at, Resource{});
    auto unit = increment(map, at, id);
    unit.variety = 3;
    batch.proposals = {unit};
    ResourceGrowth::apply(map, batch, metrics);
    CHECK(map.materialAmountAt(at, MaterialId::Food) == 1);
    CHECK(map.materialAmountAt(at, MaterialId::Paper) == 2);
    CHECK(map.getResource(at).variety == 3);
}

TEST_CASE("zero seed stocks use authoritative resource initialization" * doctest::test_suite("ResourceGrowth"))
{
    glob2test::HeadlessGlobals globals;
    for (bool multi : {false, true}) for (bool persistent : {false, true})
    {
        glob2test::HeadlessGame world({.header = true});
        auto &map = world.game.map;
        const auto id = crop(map, multi);
        auto catalog = nlohmann::json::parse(map.resourceRegistry().serialize());
        for (auto &resource : catalog["resources"])
            if (resource["key"] == "growth-test") {
                resource["properties"]["persistsWhenEmpty"] = persistent;
                for (auto &yield : resource["yields"]) yield["initial"] = 0;
            }
        if (!persistent) {
            CHECK_THROWS(map.installResourceDefinitions(catalog.dump()));
            if (!multi) continue;
            // A nonpersistent multi-material resource may have one empty material.
            for (auto &resource : catalog["resources"])
                if (resource["key"] == "growth-test") resource["yields"]["paper"]["initial"] = 2;
        }
        map.installResourceDefinitions(catalog.dump());
        map.incResource(4, 4, id, 2);
        ResourceGrowth::Batch batch;
        auto p = increment(map, map.coordToIndex(6, 6), id);
        p.kind = ResourceGrowth::Proposal::Kind::Seed;
        p.material = 0;
        p.variety = 2;
        batch.proposals = {p};
        ResourceGrowth::Metrics metrics;
        ResourceGrowth::apply(map, batch, metrics);
        CHECK(map.getResource(4, 4) == map.getResource(6, 6));
        CHECK(map.materialStocksAt(map.coordToIndex(4, 4)) == map.materialStocksAt(map.coordToIndex(6, 6)));
        CHECK(metrics.stockAdded == (persistent ? 0 : 2));
    }
}

TEST_CASE("typed proposal loading validates fields and preserves old queued units" * doctest::test_suite("ResourceGrowth"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.header = true});
    auto &map = world.game.map;
    const auto id = crop(map, true);
    const auto at = map.coordToIndex(4, 4);
    auto load = [&](ResourceGrowth::Pipeline &pipeline, int version, const ResourceGrowth::Proposal &p)
    {
        auto *bytes = new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream out(bytes);
        out.writeEnterSection("resourceGrowth"); out.writeUint8(8, "delay"); out.writeUint8(1, "count");
        out.writeEnterSection(0); out.writeUint32(0, "sourceTick"); out.writeUint32(1, "seed");
        out.writeUint32(map.resourceRegistry().checksum(), "catalog"); out.writeUint8(8, "remaining");
        out.writeUint32(1, "proposals"); out.writeEnterSection(0);
        out.writeUint32(p.tile, "tile"); out.writeUint16(p.type, "type");
        out.writeUint8(p.material, "material"); out.writeSint8(p.delta, "delta");
        if (version >= FILE_FORMAT_VERSION_CONFIGURED_GROWTH_SEEDS) {
            out.writeUint8(p.variety, "variety"); out.writeUint8(Uint8(p.kind), "kind");
            out.writeUint16(p.incrementMask, "incrementMask");
        }
        out.writeLeaveSection(3); out.flush();
        auto *copy = new GAGCore::MemoryStreamBackend(*bytes); copy->seekFromStart(0);
        GAGCore::BinaryInputStream in(copy); pipeline.load(&in, map, 0, version);
    };
    const auto paper = increment(map, at, id, MaterialId::Paper);
    for (int version : {145, 146, 147}) {
        ResourceGrowth::Pipeline old;
        load(old, version, paper);
        auto *bytes = new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream out(bytes); old.save(&out, 0); out.flush();
        auto *copy = new GAGCore::MemoryStreamBackend(*bytes); copy->seekFromStart(0);
        GAGCore::BinaryInputStream in(copy);
        ResourceGrowth::Pipeline restored; restored.load(&in, map, 0, VERSION_MINOR);
        map.replaceResource(at, Resource{}); map.setResourcesGrow(4, 4, false); map.setVertexTerrain(4, 4, WATER);
        restored.publish(map, 8);
        CHECK(map.materialAmountAt(at, MaterialId::Food) == 0);
        CHECK(map.materialAmountAt(at, MaterialId::Paper) == 1);
    }
    for (int bad = 0; bad < 5; ++bad) {
        auto p = paper;
        if (bad == 0) p.kind = ResourceGrowth::Proposal::Kind(255);
        if (bad == 1) { p.kind = ResourceGrowth::Proposal::Kind::Seed; p.material = 0; p.incrementMask = 0x8000; }
        if (bad == 2) p.material = 255;
        if (bad == 3) p.delta = 2;
        if (bad == 4) p.incrementMask = 1;
        ResourceGrowth::Pipeline invalid;
        CHECK_THROWS(load(invalid, VERSION_MINOR, p));
    }
}

TEST_CASE("finishing the last deferred tick computes without publishing early" * doctest::test_suite("ResourceGrowth"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.header = true});
    auto &map = world.game.map;
    map.setResourceGrowthDelay(8);
    world.game.syncStep(-1, Game::PreparationCompletion::Deferred);
    REQUIRE(map.gradientRuntime->growth.needsPreparation());
    const auto published = map.resourceGrowthMetrics().published;
    map.finishResourceGrowth();
    CHECK_FALSE(map.gradientRuntime->growth.needsPreparation());
    CHECK(map.resourceGrowthMetrics().submitted == 1);
    CHECK(map.resourceGrowthMetrics().published == published);
    CHECK(map.gradientRuntime->growth.count() == 1);
}

TEST_CASE("engine preflight opens both historical growth and released vertex saves" * doctest::test_suite("ResourceGrowth"))
{
    glob2test::GlobalsOptions options; options.loadStrings=true;
    glob2test::HeadlessGlobals globals(options);
    for (const char *name : {"growth146.game.gz","growth148.game.gz","vertex148.game.gz"}) {
        CAPTURE(std::string(name));
        Engine engine;
        const auto path=glob2test::inflated(std::string("resources/growth-save-layout/")+name);
        // These simulation fixtures omit the optional GameGUI tail. Exercise the
        // production header preflight, then load the complete simulation payload.
        MapHeader header; GameHeader players;
        auto input=engine.openGameInput(path.string(),header,players);
        REQUIRE(input != nullptr);
        REQUIRE(engine.gui.game.load(input.get()));
        CHECK(engine.gui.game.map.resourceGrowthDelay()==8);
    }
}
