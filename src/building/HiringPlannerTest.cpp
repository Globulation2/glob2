// SPDX-License-Identifier: GPL-3.0-or-later
#include "HiringPipeline.h"
#include "EngineFixtures.h"
#include "HiringPlanner.h"
#include "HiringDiagnostics.h"
#include <sstream>
#include "SharedBuildingGradient.h"
#include <thread>
#include <set>
#include <nlohmann/json.hpp>

namespace
{
void checkPlannerOutput(const HiringPlanner::Output &actual, const HiringPlanner::Output &expected)
{
	CHECK(actual.tick == expected.tick);
	CHECK(actual.team == expected.team);
	REQUIRE(actual.buildings.size() == expected.buildings.size());
	for (unsigned b = 0; b < actual.buildings.size(); ++b)
	{
		CAPTURE(b);
		const auto &a = actual.buildings[b], &e = expected.buildings[b];
		CHECK(a.building == e.building); CHECK(a.type == e.type);
		CHECK(a.configuration == e.configuration);
		REQUIRE(a.jobs.size() == e.jobs.size());
		for (unsigned j = 0; j < a.jobs.size(); ++j)
		{
			CAPTURE(j);
			const auto &aj = a.jobs[j], &ej = e.jobs[j];
			CHECK(aj.key == ej.key);
			REQUIRE(aj.candidates.size() == ej.candidates.size());
			for (unsigned c = 0; c < aj.candidates.size(); ++c)
			{
				CAPTURE(c);
				CHECK(aj.candidates[c].unit == ej.candidates[c].unit);
				CHECK(aj.candidates[c].level == ej.candidates[c].level);
				CHECK(aj.candidates[c].value == ej.candidates[c].value);
			}
		}
	}
}
} // namespace

TEST_SUITE("HiringPlanner")
{
	TEST_CASE("distance bounds preserve exhaustive rankings for every job")
	{
		glob2test::HeadlessGlobals globals;
		const char *names[] = {"inn", "clearingflag", "explorationflag", "warflag"};
		const int types[] = {WORKER, WORKER, EXPLORER, WARRIOR};
		Uint64 boundSkips = 0;
		for (unsigned role = 0; role < 4; ++role)
		{
			CAPTURE(role);
			glob2test::HeadlessGame w({.wDec = 7, .hDec = 7, .discovered = true, .loadDefaultRace = true});
			auto *b = w.addBuilding(names[role], 8, 8);
			b->maxUnitWorking = 2;
			b->subscriptionWorkingTimer = 32;
			b->materials[WHEAT] = 0;
			b->clearingMaterials[WOOD] = true;
			b->updateCallLists();
			REQUIRE(w.game.map.incResourceByIndex(7, 8, WOOD, 0));
			REQUIRE(w.game.map.incResourceByIndex(24, 12, WHEAT, 0));
			for (unsigned i = 0; i < 24; ++i)
			{
				auto *u = w.addUnit(types[role], 5 + i % 4, 14 + i / 4);
				u->activity = Unit::ACT_RANDOM;
				u->medical = Unit::MED_FREE;
				u->hungriness = 1;
				// Include equal scores, negative food budgets and later candidates
				// with better levels, which must survive the optimistic bound.
				u->hungry = i % 3 == 0 ? 0 : 10000;
				u->trigHungry = 100;
				u->constructionLevel = i % 4;
				u->level[MAGIC_ATTACK_GROUND] = i % 4;
				if (role == 0 && i % 2) u->carriedMaterial = WHEAT;
			}
			const auto input = HiringPipeline::bindRoutes(w.game, w.game.captureReadBoundary());
			for (unsigned capacity : {1u, 4u, 8u})
			{
				CAPTURE(capacity);
				HiringPlanner::Planner pruned, exhaustive({.pruneCandidates = false});
				const auto actual = pruned.rank(input, 0, capacity);
				const auto expected = exhaustive.rank(input, 0, capacity);
				checkPlannerOutput(actual, expected);
				CHECK(actual.metrics.rankedCandidates <= expected.metrics.rankedCandidates);
				REQUIRE_FALSE(actual.buildings.empty());
				boundSkips += actual.metrics.distanceBoundSkips;
				std::set<Hiring::Job> populated;
				for (const auto &building : actual.buildings)
					for (const auto &list : building.jobs)
						if (!list.candidates.empty()) populated.insert(list.key.job);
				if (role == 0)
				{
					CHECK(populated.count(Hiring::Job::Delivery) == 1);
					CHECK(populated.count(Hiring::Job::Fetch) == 1);
				}
				else CHECK(populated.count(Hiring::Job(unsigned(Hiring::Job::Clear) + role - 1)) == 1);
			}
		}
		CHECK(boundSkips > 0);
	}
	TEST_CASE("resource estimates borrow the normal snapshot plane without private builds")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame w({.wDec = 7, .hDec = 7, .discovered = true, .loadDefaultRace = true});
		auto *b = w.addBuilding("inn", 8, 8);
		b->maxUnitWorking = 2;
		b->materials[WHEAT] = 0;
		b->updateCallLists();
		auto *u = w.addUnit(WORKER, 12, 12);
		u->activity = Unit::ACT_RANDOM;
		u->medical = Unit::MED_FREE;
		u->hungriness = 0;
		REQUIRE(w.game.map.incResourceByIndex(24, 12, WHEAT, 0));
		w.game.map.getMaterialGradientSlot(0, WHEAT, u->swimClass());
		const auto snapshot = w.game.captureReadBoundary();
		const auto input = HiringPipeline::bindRoutes(w.game, snapshot);
		REQUIRE(input.world.resourceFields == snapshot.resourceFields);
		HiringPlanner::Planner planner;
		const auto expected = planner.rank(input, 0);
		CHECK(expected.metrics.resourcePlaneReads > 0);
		const auto key = MapState::planeKey(0, WHEAT, u->swimClass(), false);
		const auto plane = input.world.resourceFields->find(key)->values;
		const auto frozen = *plane;
		// Normal field publication can replace a generation without changing
		// a job's immutable lease or requiring a hiring-specific copy.
		w.game.map.addForbidden(24, 12, 0);
		w.game.map.updateMaterialGradient(0, WHEAT, u->swimClass());
		CHECK(*plane == frozen);
		checkPlannerOutput(planner.rank(input, 0), expected);
	}
	TEST_CASE("shared resource planes retain ordered unreachable-neighbor admission estimates")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame w({.wDec = 7, .hDec = 7, .discovered = true, .loadDefaultRace = true});
		auto *building = w.addBuilding("inn", 8, 8);
		building->maxUnitWorking = 2;
		building->materials[WHEAT] = 0;
		building->updateCallLists();
		auto *unit = w.addUnit(WORKER, 12, 12);
		unit->activity = Unit::ACT_RANDOM;
		unit->medical = Unit::MED_FREE;
		unit->hungriness = 0;
		REQUIRE(w.game.map.incResourceByIndex(24, 24, WHEAT, 0));
		for (int y = 22; y <= 26; ++y)
			for (int x = 22; x <= 26; ++x)
				if (x == 22 || x == 26 || y == 22 || y == 26)
					w.game.map.addForbidden(x, y, 0);
		w.game.map.getMaterialGradientSlot(0, WHEAT, unit->swimClass());
		const auto input = HiringPipeline::bindRoutes(w.game, w.game.captureReadBoundary());
		const auto actual = HiringPlanner::Planner{}.rank(input, 0);
		CHECK(actual.metrics.resourcePlaneReads > 0);
		for (const auto &b : actual.buildings)
			for (const auto &list : b.jobs)
				if (list.key.job == Hiring::Job::Fetch && list.key.material == WHEAT)
					CHECK(list.candidates.empty());
	}
	TEST_CASE("busy ground classes do not initialize unused hiring routes")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame w({.discovered = true, .loadDefaultRace = true});
		auto *b = w.addBuilding("inn", 8, 8);
		b->maxUnitWorking = 1;
		b->materials[WHEAT] = 0;
		b->updateCallLists();
		auto *u = w.addUnit(WORKER, 6, 8);
		u->medical = Unit::MED_FREE;
		u->activity = Unit::ACT_FILLING;
		const auto busy = w.game.captureReadBoundary();
		CHECK(busy.entities->availableGroundClasses[0] == 0);
		CHECK(HiringPipeline::bindRoutes(w.game, busy).routes->empty());
		u->activity = Unit::ACT_RANDOM;
		w.game.snapshotStore().invalidateBoundary();
		const auto available = w.game.captureReadBoundary();
		CHECK(available.entities->availableGroundClasses[0] != 0);
		CHECK(HiringPipeline::bindRoutes(w.game, available).routes->empty());
		// A normal simulation query establishes the shared generation.
		w.game.map.buildingGradient(b, u->swimClass());
		CHECK_FALSE(HiringPipeline::bindRoutes(w.game, available).routes->empty());
	}
	TEST_CASE("cold routes are established by live admission rather than observation")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame w({.discovered = true, .loadDefaultRace = true});
		auto *b = w.addBuilding("inn", 8, 8);
		b->maxUnitWorking = 1;
		b->materials[WHEAT] = 0;
		b->updateCallLists();
		auto *u = w.addUnit(WORKER, 6, 8);
		u->medical = Unit::MED_FREE;
		u->activity = Unit::ACT_RANDOM;
		u->hungriness = 0;
		u->carriedMaterial = WHEAT;
		const auto slot = b->routeSlot(u->swimClass(), BuildingRoute::Footprint);
		CHECK(b->globalGradient[slot] == nullptr);
		glob2test::publishCurrentHiringSuggestions(w.game);
		CHECK(b->globalGradient[slot] == nullptr);
		CHECK(b->subscribeToBringMaterialsStep());
		REQUIRE(b->unitsWorking.size() == 1);
		CHECK(b->unitsWorking.front() == u);
		CHECK(b->globalGradient[slot] != nullptr);
	}
	TEST_CASE("unrelated live route enrollment cannot change hiring inputs")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame w({.discovered = true, .loadDefaultRace = true});
		auto *building = w.addBuilding("inn", 8, 8);
		building->maxUnitWorking = 1;
		building->updateCallLists();
		w.addUnit(WORKER, 6, 8);
		const auto snapshot = w.game.captureReadBoundary();
		const auto before = HiringPipeline::bindRoutes(w.game, snapshot);
		// Other query consumers may enroll unused movement classes or routes.
		w.game.map.buildingGradient(building, SWIM_CLASS_COUNT - 1, BuildingRoute::Combat);
		const auto after = HiringPipeline::bindRoutes(w.game, snapshot);
		CHECK(before.routes->size() == after.routes->size());
	}
	TEST_CASE("published resource estimates are used and missing planes remain recruitable")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame w({.discovered = true, .loadDefaultRace = true});
		auto *b = w.addBuilding("inn", 8, 8);
		b->maxUnitWorking = 1;
		b->materials[WHEAT] = 0;
		b->updateCallLists();
		auto *u = w.addUnit(WORKER, 6, 8);
		u->activity = Unit::ACT_RANDOM;
		u->medical = Unit::MED_FREE;
		u->hungriness = 0;
		REQUIRE(w.game.map.incResourceByIndex(5, 8, WHEAT, 0));
		auto input = HiringPipeline::bindRoutes(w.game, w.game.captureReadBoundary());
		auto local = input;
		auto planes = std::make_shared<SimulationSnapshot::ResourceFields>();
		auto values = std::make_shared<const std::vector<Uint16>>(
			w.game.map.getW() * w.game.map.getH(), GRADIENT_UNREACHABLE);
		planes->add({MapState::planeKey(0, WHEAT, 0, false), 1, values});
		local.world.resourceFields = planes;
		HiringPlanner::Planner a, bCache;
		const auto normal = a.rank(input, 0), enrolled = bCache.rank(local, 0);
		const auto fetch = [](const auto &output) -> const std::vector<HiringPlanner::Candidate> &
		{
			for (const auto &building : output.buildings)
				for (const auto &job : building.jobs)
					if (job.key == Hiring::Key{Hiring::Job::Fetch, WHEAT})
						return job.candidates;
			throw std::runtime_error("missing fetch list");
		};
		REQUIRE(fetch(normal).size() == 1);
		CHECK(fetch(enrolled).empty());
		CHECK(normal.metrics.resourceEstimateFallbacks > 0);
		CHECK(enrolled.metrics.resourcePlaneReads > 0);
	}
	TEST_CASE("missing shared planes do not suppress suggestions after visibility-rule changes")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame w({.discovered = true, .loadDefaultRace = true});
		const auto registry = ResourceRegistry::builtins();
		const auto food = registry->find("wheat");
		REQUIRE(food);
		auto a = nlohmann::json::parse(registry->serialize())["resources"][resourceIndex(*food)];
		a["key"] = "fixture:visible-food";
		a["properties"]["visibleToHarvest"] = true;
		auto b = a;
		b["key"] = "fixture:unseen-food";
		b["properties"]["visibleToHarvest"] = false;
		w.game.map.installResourceDefinitions(
			nlohmann::json{{"schemaVersion", 1}, {"resources", {a, b}}}.dump());
		auto *building = w.addBuilding("inn", 8, 8);
		building->maxUnitWorking = 1;
		building->materials[WHEAT] = 0;
		building->updateCallLists();
		auto *u = w.addUnit(WORKER, 6, 8);
		u->activity = Unit::ACT_RANDOM;
		u->medical = Unit::MED_FREE;
		u->hungriness = 0;
		HiringPlanner::Planner warm;
		const auto count = [](const auto &output)
		{
			for (const auto &b : output.buildings)
				for (const auto &j : b.jobs)
					if (j.key == Hiring::Key{Hiring::Job::Fetch, WHEAT})
						return j.candidates.size();
			throw std::runtime_error("missing fetch list");
		};
		for (unsigned phase = 0; phase < 3; ++phase)
		{
			if (phase == 2)
			{
				b["properties"]["visibleToHarvest"] = true;
				w.game.map.installResourceDefinitions(
					nlohmann::json{{"schemaVersion", 1}, {"resources", {a, b}}}.dump());
			}
			const auto resource = w.game.map.resourceRegistry().find(
				phase == 0 ? "fixture:visible-food" : "fixture:unseen-food");
			REQUIRE(resource);
			const auto resourceId = resourceIndex(*resource);
			w.game.map.setResourceByIndex(5, 8, resourceId, 0);
			REQUIRE(w.game.map.getResource(w.game.map.coordToIndex(5, 8)).type == resourceId);
			w.game.snapshotStore().invalidateBoundary();
			auto input = HiringPipeline::bindRoutes(w.game, w.game.captureReadBoundary());
			auto hidden = std::make_shared<SimulationSnapshot::Visibility>(*input.world.visibility);
			std::fill(hidden->visible.begin(), hidden->visible.end(), 0);
			input.world.visibility = hidden;
			HiringPlanner::Planner fresh;
			const auto reused = warm.rank(input, 0), rebuilt = fresh.rank(input, 0);
			CHECK(count(reused) == count(rebuilt));
			CHECK(count(reused) == 1);
			CHECK(reused.metrics.resourceEstimateFallbacks > 0);
		}
	}
	TEST_CASE("worker demand uses immutable shared funding and staffing facts")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.discovered = true, .loadDefaultRace = true});
		auto *inn = world.addBuilding("inn", 8, 8);
		auto *unit = world.addUnit(WORKER, 12, 12);
		REQUIRE(inn);
		REQUIRE(unit);
		inn->desiredMaxUnitWorking = 2;
		inn->unitsWorking.push_back(unit);
		unit->destinationPurpose = WHEAT;
		const auto observed = world.game.captureReadBoundary();
		const auto inputs = HiringPipeline::bindRoutes(world.game, observed);
		const auto before = HiringPlanner::prepare(inputs, 0);
		REQUIRE(before.size() == 1);
		CHECK(before.front().seats == 1);
		// Demand is frozen even if owner staffing and stock subsequently change.
		inn->desiredMaxUnitWorking = 0;
		inn->unitsWorking.clear();
		inn->materials[WHEAT] = 100;
		const auto after = HiringPlanner::prepare(inputs, 0);
		REQUIRE(after.size() == 1);
		CHECK(after.front().seats == before.front().seats);
		CHECK(after.front().vacancies == before.front().vacancies);
		CHECK(after.front().modes == before.front().modes);
		const auto shared = inputs;
		CHECK(shared.world.entities.get() == inputs.world.entities.get());
		CHECK(shared.routes.get() == inputs.routes.get());
	}
	TEST_CASE("observed funding reproduces live material apportionment")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.discovered = true, .loadDefaultRace = true});
		auto *inn = world.addBuilding("inn", 8, 8);
		auto *unit = world.addUnit(WORKER, 12, 12);
		REQUIRE(inn);
		REQUIRE(unit);
		inn->desiredMaxUnitWorking = 3;
		inn->unitsWorking.push_back(unit);
		unit->destinationPurpose = WHEAT;
		for (const int stock : {0, 1, 5, 20})
		{
			inn->materials[WHEAT] = stock;
			inn->reservedMaterials[WHEAT] = 1;
			world.game.snapshotStore().invalidateBoundary();
			const auto snapshot = world.game.captureReadBoundary();
			auto input = HiringPipeline::bindRoutes(world.game, snapshot);
			const auto supplies = HiringPlanner::prepare(input, 0);
			REQUIRE(supplies.size() == 1);
			const auto &supply = supplies.front();
			for (unsigned m = 0; m < MaterialCount; ++m)
			{
				const int mult = inn->type->materialMultiplier[m],
						  target = (inn->materialDeliveryTarget(m) + mult - 1) / mult,
						  missing = inn->materialDeliveryNeed(m);
				int served = target - (missing + mult - 1) / mult;
				for (auto *u : inn->unitsWorking)
					served += u->destinationPurpose == int(m);
				const unsigned vacancies = missing > 0 && served < target
											   ? std::min(supply.seats, unsigned(target - served))
											   : 0;
				CHECK(supply.vacancies[m] == vacancies);
				CHECK(supply.modes[m] ==
					  (vacancies ? world.game.map.materialSupplyModesSlot(inn, m) : 0));
			}
			REQUIRE(snapshot.entities->funding.size() == 1);
			CHECK(snapshot.entities->funding.front().reservedMaterials[WHEAT] == 1);
		}
	}
	TEST_CASE("worker construction quotas match repair funding")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.loadDefaultRace = true});
		auto *inn = world.addBuilding("inn", 8, 8);
		REQUIRE(inn);
		inn->hp = inn->getEffectiveMaxHp() / 2;
		inn->launchConstruction(2, 2);
		REQUIRE(inn->tryToBuildingSiteRoom());
		inn->desiredMaxUnitWorking = 2;
		for (int r = 0; r < MaterialCount; ++r)
			inn->materials[r] = 3;
		const auto snapshot = world.game.captureReadBoundary();
		const auto input = HiringPipeline::bindRoutes(world.game, snapshot);
		const auto supplies = HiringPlanner::prepare(input, 0);
		REQUIRE(supplies.size() == 1);
		for (unsigned r = 0; r < MaterialCount; ++r)
		{
			const int multiplier = inn->type->materialMultiplier[r];
			const int target = (inn->materialDeliveryTarget(r) + multiplier - 1) / multiplier;
			const int missing = inn->materialDeliveryNeed(r);
			const int served = target - (missing + multiplier - 1) / multiplier;
			CHECK(supplies.front().vacancies[r] ==
				  (missing > 0 && served < target ? std::min(2u, unsigned(target - served)) : 0u));
		}
		SimulationSnapshot::verifyCapture(world.game, snapshot);
	}
	TEST_CASE("defense score uses frozen attack modifiers and saturated integer strength")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.discovered = true, .loadDefaultRace = true});
		auto catalog = nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
		const int id = world.game.buildingsTypes.getTypeNum("stonewall", 0, false);
		catalog["variants"][id]["semantics"]["areaEffects"] = {{"radius", 4},
															   {"attackBuffBps", 2000}};
		world.game.buildingsTypes.loadSnapshotJson(catalog.dump());
		world.game.configureBuildingCatalog();
		REQUIRE(world.addBuilding("stonewall", 12, 12));
		auto *flag = world.addBuilding("warflag", 8, 8);
		auto *warrior = world.addUnit(WARRIOR, 10, 10);
		REQUIRE(flag);
		REQUIRE(warrior);
		flag->desiredMaxUnitWorking = 1;
		warrior->activity = Unit::ACT_RANDOM;
		warrior->medical = Unit::MED_FREE;
		warrior->hungriness = 0;
		warrior->insideTimeout = 0;
		warrior->experienceLevel = INT_MAX;
		world.game.gameHeader.setGlassCannonLevel(2);
		world.game.areaEffects.beginTick(world.game);
		world.game.map.buildingGradient(flag, warrior->swimClass(), BuildingRoute::Combat);
		const auto expected =
			std::int64_t(warrior->performance[ATTACK_SPEED]) * warrior->getRealAttackStrength();
		const auto snapshot = world.game.captureReadBoundary();
		REQUIRE(snapshot.entities->units.size() == 1);
		CHECK(snapshot.entities->units.front().realAttackStrength ==
			  warrior->getRealAttackStrength());
		HiringPlanner::Planner cache;
		const auto output = cache.rank(HiringPipeline::bindRoutes(world.game, snapshot), 0, 8);
		bool found = false;
		for (const auto &b : output.buildings)
			for (const auto &list : b.jobs)
				if (list.key.job == Hiring::Job::Defend)
					for (const auto &candidate : list.candidates)
					{
						found = true;
						CHECK(candidate.level == expected);
					}
		CHECK(found);
	}
	TEST_CASE("supplier membership survives waiting-for-construction observations")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::GameOptions options;
		options.discovered = true;
		options.loadDefaultRace = true;
		options.header = true;
		options.experiments.set(ExperimentId::MarketsV2);
		glob2test::HeadlessGame world(options);
		auto *inn = world.addBuilding("inn", 6, 6, 1);
		auto *market = world.addBuilding("market", 24, 24);
		REQUIRE(inn);
		REQUIRE(market);
		inn->desiredMaxUnitWorking = 1;
		REQUIRE(!world.team->stockSuppliers.empty());
		// A waiting provider can remain enrolled even while excluded as a goal.
		market->buildingState = Building::WAITING_FOR_CONSTRUCTION;
		const auto snapshot = world.game.captureReadBoundary();
		const auto supplies =
			HiringPlanner::prepare(HiringPipeline::bindRoutes(world.game, snapshot), 0);
		REQUIRE(supplies.size() == 1);
		CHECK(supplies.front().modes[CHERRY] ==
			  world.game.map.materialSupplyModesSlot(inn, CHERRY));
		CHECK((snapshot.teams->values[0].supplierPresence & 1) != 0);
	}
	TEST_CASE("snapshot planning skips busy workers without changing live units")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.discovered = true, .loadDefaultRace = true});
		auto *inn = world.addBuilding("inn", 8, 8);
		REQUIRE(inn);
		inn->desiredMaxUnitWorking = 2;
		auto *free = world.addUnit(WORKER, 12, 12);
		auto *busy = world.addUnit(WORKER, 13, 12);
		REQUIRE(free);
		REQUIRE(busy);
		for (auto *u : {free, busy})
		{
			u->carriedMaterial = WHEAT;
			u->medical = Unit::MED_FREE;
		}
		free->activity = Unit::ACT_RANDOM;
		busy->activity = Unit::ACT_FILLING;
		for (int swim = 0; swim < SWIM_CLASS_COUNT; ++swim)
			world.game.map.buildingGradient(inn, swim);
		const auto checksum = world.checksum();
		const auto snapshot = world.game.captureReadBoundary();
		auto inputs = HiringPipeline::bindRoutes(world.game, snapshot);
		HiringPlanner::Planner cache;
		auto result = cache.rank(inputs, 0, 8);
		CHECK(world.checksum() == checksum);
		bool found = false;
		for (const auto &b : result.buildings)
			if (b.building == Game::refOf(inn))
				for (const auto &job : b.jobs)
					if (job.key == Hiring::Key{Hiring::Job::Delivery, WHEAT})
					{
						found = true;
						CHECK(job.candidates.size() == 1);
						CHECK(job.candidates.front().unit == Game::refOf(free));
					}
		CHECK(found);
		const auto repeated = cache.rank(inputs, 0, 8);
		CHECK(repeated.candidateVisits == result.candidateVisits);
		CHECK(result.metrics.sharedQueries > 0);
		CHECK(repeated.metrics.sharedQueries > 0);
		CHECK(repeated.metrics.rankedCandidates == result.metrics.rankedCandidates);
		CHECK(world.checksum() == checksum);
	}
	TEST_CASE("planning skips zero-demand and staffed buildings and resumes on vacancies")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.discovered = true, .loadDefaultRace = true});
		auto *inn = world.addBuilding("inn", 8, 8);
		auto *worker = world.addUnit(WORKER, 12, 12);
		REQUIRE(inn);
		REQUIRE(worker);
		worker->carriedMaterial = WHEAT;
		world.game.map.buildingGradient(inn, worker->swimClass());
		HiringPlanner::Planner cache;
		const auto plan = [&]
		{
			world.game.snapshotStore()
				.invalidateBoundary(); // Announce these owner-side fixture edits.
			auto snapshot = world.game.captureReadBoundary();
			auto inputs = HiringPipeline::bindRoutes(world.game, snapshot);
			auto output = cache.rank(inputs, 0, 8);
			return std::pair{std::move(inputs), std::move(output)};
		};
		inn->desiredMaxUnitWorking = 0;
		auto [zeroInputs, zero] = plan();
		CHECK(zeroInputs.routes->empty());
		CHECK(HiringPlanner::prepare(zeroInputs, 0).empty());
		CHECK(zero.buildings.empty());
		CHECK(zero.candidateVisits == 0);
		CHECK(zero.metrics.buildingsSkippedNoDemand == 1);
		inn->desiredMaxUnitWorking = 1;
		inn->unitsWorking.push_back(worker);
		auto [staffedInputs, staffed] = plan();
		CHECK(staffedInputs.routes->empty());
		CHECK(staffed.buildings.empty());
		CHECK(staffed.candidateVisits == 0);
		inn->unitsWorking.clear();
		auto [vacancyInputs, vacancy] = plan();
		REQUIRE(vacancy.buildings.size() == 1);
		CHECK(vacancy.candidateVisits > 0);
		CHECK_FALSE(vacancyInputs.routes->empty());
		CHECK(vacancy.metrics.buildingsNeedingHiring == 1);
		inn->desiredMaxUnitWorking = 0;
		auto [finishedInputs, finished] = plan();
		CHECK(finished.buildings.empty());
		CHECK(finished.candidateVisits == 0);
	}
	TEST_CASE("job quotas bound each list to vacancies plus two")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.discovered = true, .loadDefaultRace = true});
		auto *inn = world.addBuilding("inn", 8, 8);
		REQUIRE(inn);
		inn->desiredMaxUnitWorking = 2;
		for (int i = 0; i < 8; ++i)
		{
			auto *u = world.addUnit(WORKER, 12 + i, 12);
			REQUIRE(u);
			u->activity = Unit::ACT_RANDOM;
			u->medical = Unit::MED_FREE;
			u->hungriness = 0;
			u->carriedMaterial = WHEAT;
		}
		HiringPlanner::Planner cache;
		const auto input = HiringPipeline::bindRoutes(world.game, world.game.captureReadBoundary());
		const auto out = cache.rank(input, 0);
		REQUIRE(out.buildings.size() == 1);
		for (const auto &list : out.buildings.front().jobs)
			CHECK(list.candidates.size() <= 4);
	}
	TEST_CASE("diverse material offers cover peers without reserving live workers")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.discovered = true, .loadDefaultRace = true});
		auto *a = world.addBuilding("inn", 8, 8);
		auto *b = world.addBuilding("inn", 16, 8);
		REQUIRE(a);
		REQUIRE(b);
		a->desiredMaxUnitWorking = b->desiredMaxUnitWorking = 2;
		for (int i = 0; i < 16; ++i)
		{
			auto *u = world.addUnit(WORKER, 12 + i % 4, 12 + i / 4);
			REQUIRE(u);
			u->medical = Unit::MED_FREE;
			u->activity = Unit::ACT_RANDOM;
		}
		world.game.map.incResourceByIndex(24, 24, WHEAT, 0);
		for (auto *building : {a, b})
			for (int swim = 0; swim < SWIM_CLASS_COUNT; ++swim)
				world.game.map.buildingGradient(building, swim);
		for (int swim = 0; swim < SWIM_CLASS_COUNT; ++swim)
			world.game.map.getMaterialGradientSlot(0, WHEAT, swim);
		auto snapshot = world.game.captureReadBoundary();
		auto inputs = HiringPipeline::bindRoutes(world.game, snapshot);
		const auto checksum = world.checksum();
		HiringPlanner::Planner cache;
		auto output = cache.rank(inputs, 0, 128);
		CHECK(output.metrics.coordinatedTeams == 1);
		CHECK(world.checksum() == checksum);
		std::set<std::pair<Uint16, Uint32>> primary;
		for (const auto &building : output.buildings)
			for (const auto &list : building.jobs)
				if (list.key == Hiring::Key{Hiring::Job::Fetch, WHEAT})
				{
					REQUIRE(list.candidates.size() >= 2);
					CHECK(list.candidates.size() <= 4);
					for (unsigned i = 0; i < 2; ++i)
						CHECK(primary
								  .insert({list.candidates[i].unit.gid,
										   list.candidates[i].unit.generation})
								  .second);
				}
		CHECK(primary.size() == 4);
		// A narrow ceiling cannot hold the private team ranking buffer. Falling
		// back must retain the same independent vacancy-sized prefixes.
		const auto fallback = cache.rank(inputs, 0, 4);
		CHECK(fallback.metrics.coordinationSkipped == 1);
		for (const auto &building : fallback.buildings)
			for (const auto &list : building.jobs)
			{
				CHECK(list.candidates.size() <= 4);
				for (unsigned n = 1; n < list.candidates.size(); ++n)
					CHECK(HiringPlanner::better(list.candidates[n - 1], list.candidates[n],
												list.key.job));
			}
	}
	TEST_CASE("shared field survives replacement and proposals preserve owner demand")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.discovered = true, .loadDefaultRace = true});
		auto *inn = world.addBuilding("inn", 8, 8);
		REQUIRE(inn);
		int distance = 0;
		REQUIRE(world.game.map.buildingAvailable(inn, 0, 12, 12, &distance));
		auto lease = inn->leaseGradient(inn->routeSlot(0, BuildingRoute::Automatic));
		REQUIRE(lease);
		REQUIRE(lease->search);
		const int required = lease->search->requiredCost();
		std::uint64_t wait = 0, query = 0, expansions = 0;
		const auto oldDistance = lease->distance(20, 20, wait, query, expansions);
		CHECK(oldDistance != INT_MAX);
		CHECK(lease->search->requiredCost() == required);
		CHECK(expansions > 0);
		std::thread worker(
			[&]
			{
				for (int i = 0; i < 1000; ++i)
					lease->distance(24 + i % 4, 24, wait, query, expansions);
			});
		for (int i = 0; i < 1000; ++i)
			CHECK(world.game.map.buildingAvailable(inn, 0, 12 + i % 4, 12, &distance));
		inn->resetPathfindGradients();
		REQUIRE(world.game.map.buildingAvailable(inn, 0, 20, 20, &distance));
		worker.join();
		CHECK(distance == oldDistance);
		CHECK(lease->values != inn->globalGradient[0]);
		CHECK(lease->distance(20, 20, wait, query, expansions) == oldDistance);
	}
	TEST_CASE("missing building fields never trigger private construction")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.discovered = true, .loadDefaultRace = true});
		auto *inn = world.addBuilding("inn", 8, 8);
		REQUIRE(inn);
		inn->desiredMaxUnitWorking = 1;
		auto *unit = world.addUnit(WORKER, 12, 12);
		REQUIRE(unit);
		unit->carriedMaterial = WHEAT;
		auto snapshot = world.game.captureReadBoundary();
		HiringPlanner::Planner cache;
		auto inputs = HiringPipeline::bindRoutes(world.game, snapshot);
		inputs.routes = std::make_shared<const std::vector<HiringPlanner::Inputs::Route>>();
		const auto output = cache.rank(inputs, 0, 8);
		CHECK(output.metrics.sharedQueries == 0);
		CHECK(output.metrics.missingBuildingFields > 0);
	}
	TEST_CASE("ranking tie break follows unit identity")
	{
		HiringPlanner::Candidate a{{7, 3}, 10, 20}, b{{8, 1}, 10, 20};
		CHECK(HiringPlanner::better(a, b, Hiring::Job::Fetch));
		CHECK_FALSE(HiringPlanner::better(b, a, Hiring::Job::Fetch));
	}
	TEST_CASE("bounded shortlists match sorted prefixes for every job")
	{
		for (auto job : {Hiring::Job::Delivery, Hiring::Job::Fetch, Hiring::Job::Clear,
						 Hiring::Job::Explore, Hiring::Job::Defend})
			for (unsigned capacity : {1u, 8u, 16u, 128u})
			{
				std::vector<HiringPlanner::Candidate> candidates;
				for (unsigned n = 0; n < 140; ++n)
					candidates.push_back(
						{{Uint16(139 - n), 1}, Sint64(n % 7), Sint64((n * 3) % 19)});
				auto sorted = candidates;
				std::sort(sorted.begin(), sorted.end(), [&](const auto &a, const auto &b)
						  { return HiringPlanner::better(a, b, job); });
				auto actual = HiringPlanner::shortlist({job, 0}, std::move(candidates), capacity);
				REQUIRE(actual.candidates.size() == capacity);
				for (unsigned i = 0; i < capacity; ++i)
				{
					CHECK(actual.candidates[i].unit == sorted[i].unit);
					CHECK(actual.candidates[i].level == sorted[i].level);
					CHECK(actual.candidates[i].value == sorted[i].value);
				}
			}
	}
	TEST_CASE("fetch jobs reuse canonical resource estimates independently of live planes")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.discovered = true, .loadDefaultRace = true});
		auto *inn = world.addBuilding("inn", 8, 8);
		auto *worker = world.addUnit(WORKER, 12, 12);
		REQUIRE(inn);
		REQUIRE(worker);
		inn->desiredMaxUnitWorking = 1;
		world.game.map.incResourceByIndex(16, 16, WHEAT, 0);
		const int swim = worker->swimClass();
		world.game.map.buildingGradient(inn, swim);
		world.game.map.getMaterialGradientSlot(0, WHEAT, swim, false, inn);
		const auto checksum = world.checksum();
		auto snapshot = world.game.captureReadBoundary();
		auto inputs = HiringPipeline::bindRoutes(world.game, snapshot);
		const auto supplies = HiringPlanner::prepare(inputs, 0);
		REQUIRE(supplies.size() == 1);
		CHECK(supplies.front().modes[WHEAT] == 0);
		CHECK((inn->runtime->fetchesDirectStockMask & (1u << WHEAT)) != 0);
		HiringPlanner::Planner cache;
		const auto output = cache.rank(inputs, 0, 8);
		CHECK(output.metrics.resourcePlaneReads > 0);
		const auto repeated = cache.rank(inputs, 0, 8);
		CHECK(repeated.metrics.resourcePlaneReads + repeated.metrics.resourceEstimateFallbacks > 0);
		CHECK(world.checksum() == checksum);
	}
	TEST_CASE("missing supplier planes retain fetch suggestions for live admission")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::GameOptions options;
		options.discovered = true;
		options.loadDefaultRace = true;
		options.header = true;
		options.experiments.set(ExperimentId::MarketsV2);
		glob2test::HeadlessGame world(options);
		auto *inn = world.addBuilding("inn", 6, 6, 1);
		auto *market = world.addBuilding("market", 24, 24);
		auto *worker = world.addUnit(WORKER, 20, 20, 0, 1);
		REQUIRE(inn);
		REQUIRE(market);
		REQUIRE(worker);
		inn->desiredMaxUnitWorking = 1;
		market->materials[CHERRY] = 10;
		for (int swim = 0; swim < SWIM_CLASS_COUNT; ++swim)
			world.game.map.buildingGradient(inn, swim);
		worker->activity = Unit::ACT_RANDOM;
		worker->medical = Unit::MED_FREE;
		auto snapshot = world.game.captureReadBoundary();
		auto inputs = HiringPipeline::bindRoutes(world.game, snapshot);
		const auto supplies = HiringPlanner::prepare(inputs, 0);
		const auto supply = std::find_if(supplies.begin(), supplies.end(), [&](const auto &s)
										 { return s.building == Game::refOf(inn); });
		REQUIRE(supply != supplies.end());
		CHECK(supply->modes[CHERRY] == world.game.map.materialSupplyModesSlot(inn, CHERRY));
		CHECK(supply->modes[CHERRY] != 0);
		HiringPlanner::Planner cache;
		const auto output = cache.rank(inputs, 0, 8);
		bool found = false;
		for (const auto &building : output.buildings)
			if (building.building == Game::refOf(inn))
				for (const auto &list : building.jobs)
					if (list.key == Hiring::Key{Hiring::Job::Fetch, CHERRY})
					{
						found = true;
						REQUIRE(list.candidates.size() == 1);
						CHECK(list.candidates[0].unit == Game::refOf(worker));
					}
		CHECK(found);
		// Missing supplier planes use an optimistic estimate and remain recruitable.
		REQUIRE(supply->modes[CHERRY] == 1);
		CHECK_FALSE(supply->consumerSpecific[CHERRY]);
		const auto repeated = cache.rank(inputs, 0, 8);
		CHECK(repeated.metrics.resourcePlaneReads + repeated.metrics.resourceEstimateFallbacks > 0);
		checkPlannerOutput(repeated, output);
		world.game.map.getMaterialGradientSlot(0, CHERRY, worker->swimClass(), false, inn);
		world.game.snapshotStore()
			.invalidateBoundary(); // Announce this mid-boundary fixture query.
		auto published = world.game.captureReadBoundary();
		const auto shared = cache.rank(HiringPipeline::bindRoutes(world.game, published), 0, 8);
		CHECK(shared.metrics.resourcePlaneReads > 0);
		// A canonical shared publication now supplies the resource-distance estimate.
		CHECK(shared.metrics.resourcePlaneReads > 0);
	}
}
