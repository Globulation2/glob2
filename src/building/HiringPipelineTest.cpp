// SPDX-License-Identifier: GPL-3.0-or-later
#include "SharedBuildingGradient.h"
#include "GradientExecutionAudit.h"
#include "EngineFixtures.h"
#include "HiringPipeline.h"
#include "FileFormatVersions.h"
#include "LegacyPopulationOutput.h"
#include "Order.h"
#include <BinaryStream.h>
#include <TextStream.h>
#include <StreamBackend.h>
#include <set>
#include <nlohmann/json.hpp>

namespace
{
SimulationSnapshot::Handle observation(glob2test::HeadlessGame &w)
{
	w.game.snapshotStore().invalidateBoundary();
	return w.game.captureReadBoundary();
}
Building *innWithWorker(glob2test::HeadlessGame &w, int level = 0)
{
	auto *b = w.addBuilding("inn", 8, 8, level);
	auto *u = w.addUnit(WORKER, 6, 8);
	if (!b || !u)
		throw std::runtime_error("Hiring fixture");
	b->maxUnitWorking = 1;
	b->setMaterial(WHEAT, 0);
	b->updateCallLists();
	u->activity = Unit::ACT_RANDOM;
	u->medical = Unit::MED_FREE;
	u->hungriness = 0;
	u->carriedMaterial = WHEAT;
	return b;
}
} // namespace
TEST_SUITE("HiringPipeline")
{
	TEST_CASE("paused and waiting replicas retain the same pre-order hiring observation")
	{
		glob2test::HeadlessGlobals globals;
		Uint32 reference = 0;
		for (unsigned mode = 0; mode < 3; ++mode)
		{
			CAPTURE(mode);
			glob2test::HeadlessGame w({.discovered = true, .loadDefaultRace = true});
			auto *b = innWithWorker(w);
			const auto world = w.game.observeCompletedWorld();
			w.game.anyPlayerWaited = mode == 1;
			const auto retained = w.game.observeCompletedWorld();
			CHECK(retained.entities == world.entities);
			const auto initial = w.game.hiringChecksum();
			REQUIRE(initial != 0);
			if (mode == 0)
				reference = initial;
			else
				CHECK(initial == reference);
			// A matched order changes the live world at this same boundary.
			b->desiredMaxUnitWorking = 3;
			b->updateCallLists();
			w.game.anyPlayerWaited = false;
			w.game.observeCompletedWorld();
			CHECK(w.game.hiringChecksum() == initial);
		}
	}
	TEST_CASE("cold route generations publish only at the fixed deadline")
	{
		glob2test::HeadlessGlobals globals;
		for (unsigned participants : {1u, 2u, 4u, 8u})
		{
			CAPTURE(participants);
			glob2test::HeadlessGame w({.discovered = true, .loadDefaultRace = true});
			w.game.map.configureCompute(participants);
			auto *b = innWithWorker(w);
			HiringPipeline p(w.game.map.computeExecutor());
			p.observe(w.game, observation(w));
			const auto admitted = p.checkSum();
			p.drain();
			CHECK(p.checkSum() == admitted);
			CHECK(p.suggestions(*b) == nullptr);
			p.publishDue(Hiring::PublicationDelay - 1);
			CHECK(p.suggestions(*b) == nullptr);
			p.publishDue(Hiring::PublicationDelay);
			REQUIRE(p.suggestions(*b));
			bool found = false;
			for (const auto &j : p.suggestions(*b)->jobs)
				if (j.key == Hiring::Key{Hiring::Job::Delivery, WHEAT})
					found = !j.candidates.empty();
			CHECK(found);
		}
	}
	TEST_CASE("production traces and published candidates match across participant counts")
	{
		glob2test::HeadlessGlobals globals;
		std::vector<std::pair<Uint32, Uint32>> reference;
		for (unsigned participants : {1u, 2u, 4u, 8u})
		{
			CAPTURE(participants);
			glob2test::HeadlessGame w({.wDec = 6,
									   .hDec = 6,
									   .teams = 8,
									   .discovered = true,
									   .loadDefaultRace = true,
									   .header = true});
			w.game.map.configureCompute(participants);
			innWithWorker(w);
			for (unsigned team = 1; team < 8; ++team)
			{
				auto *b = w.addBuilding("warflag", 8 + team * 6, 16, 0, team);
				b->maxUnitWorking = 2;
				b->subscriptionWorkingTimer = 28;
				b->updateCallLists();
				for (unsigned i = 0; i < team; ++i)
				{
					auto *u = w.addUnit(WARRIOR, 8 + team * 6 + i % 3, 22 + i / 3, team);
					u->activity = Unit::ACT_RANDOM;
					u->medical = Unit::MED_FREE;
					u->hungriness = 0;
				}
			}
			std::vector<std::pair<Uint32, Uint32>> actual;
			for (unsigned tick = 0; tick < 64; ++tick)
			{
				w.game.observeCompletedWorld();
				actual.emplace_back(w.game.checkSum(), w.game.hiringChecksum());
				w.game.advanceLiveSimulation(0);
			}
			if (reference.empty())
				reference = actual;
			else
				CHECK(actual == reference);
		}
	}
	TEST_CASE(
		"saving every delay phase retains outputs and pending identity without early publication")
	{
		glob2test::HeadlessGlobals globals;
		for (unsigned phase = 0; phase < 8; ++phase)
		{
			CAPTURE(phase);
			glob2test::HeadlessGame w({.discovered = true, .loadDefaultRace = true});
			auto *b = innWithWorker(w);
			HiringPipeline original(w.game.map.computeExecutor());
			for (unsigned tick = 0; tick <= phase; ++tick)
			{
				w.game.stepCounter = tick;
				original.publishDue(tick);
				original.observe(w.game, observation(w));
			}
			const auto checksum = original.checkSum();
			auto *bytes = new GAGCore::MemoryStreamBackend;
			GAGCore::BinaryOutputStream out(bytes);
			original.save(&out);
			CHECK(original.checkSum() == checksum);
			CHECK(bool(original.suggestions(*b)) == (phase >= Hiring::PublicationDelay));
			GAGCore::BinaryInputStream in(
				new GAGCore::MemoryStreamBackend(bytes->getBuffer(), bytes->getPosition()));
			in.seekFromStart(0);
			HiringPipeline restored(w.game.map.computeExecutor());
			restored.load(w.game, &in);
			CHECK(restored.checkSum() == checksum);
			for (unsigned tick = phase + 1; tick <= phase + 12; ++tick)
			{
				w.game.stepCounter = tick;
				original.publishDue(tick);
				restored.publishDue(tick);
				const auto world = observation(w);
				original.observe(w.game, world);
				restored.observe(w.game, world);
				CHECK(original.checkSum() == restored.checkSum());
			}
		}
	}
	TEST_CASE("whole game save and resume preserves every publication phase [save-format][artifacts]")
	{
		glob2test::HeadlessGlobals globals;
		for (bool text : {false, true})
		for (unsigned phase = 0; phase < 8; ++phase)
		{
			CAPTURE(phase);
			CAPTURE(text);
			glob2test::HeadlessGame original(
				{.discovered = true, .loadDefaultRace = true, .header = true});
			innWithWorker(original);
			for (unsigned tick = 0; tick < phase; ++tick)
				original.game.step(0);
			original.game.observeCompletedWorld();
			const auto checksum = original.game.checkSum();
			auto *storage = new GAGCore::MemoryStreamBackend;
			std::unique_ptr<GAGCore::OutputStream> out;
			if (text) out = std::make_unique<GAGCore::TextOutputStream>(storage);
			else out = std::make_unique<GAGCore::BinaryOutputStream>(storage);
			original.game.save(out.get(), false, "Hiring continuation");
			out->flush();
			glob2test::writeFile(glob2test::artifactDir() /
				("phase-" + std::to_string(phase) + (text ? ".game.txt" : ".game")),
				std::string(storage->getBuffer(), storage->getPosition()));
			glob2test::HeadlessGame restored(
				{.discovered = true, .loadDefaultRace = true, .header = true});
			auto *input = new GAGCore::MemoryStreamBackend(storage->getBuffer(), storage->getPosition());
			input->seekFromStart(0);
			std::unique_ptr<GAGCore::InputStream> in;
			if (text) in = std::make_unique<GAGCore::TextInputStream>(input);
			else in = std::make_unique<GAGCore::BinaryInputStream>(input);
			REQUIRE(restored.game.load(in.get()));
			CHECK(original.game.checkSum() == checksum);
			CHECK(restored.game.checkSum() == checksum);
			for (unsigned tick = 0; tick < 12; ++tick)
			{
				original.game.step(0);
				restored.game.step(0);
				CHECK(original.game.checkSum() == restored.game.checkSum());
			}
		}
	}
	TEST_CASE("configuration changes keep suggestions and admission rechecks live eligibility")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame w({.discovered = true, .loadDefaultRace = true});
		auto *b = w.addBuilding("warflag", 8, 8);
		auto *u = w.addUnit(WARRIOR, 6, 8);
		b->maxUnitWorking = 1;
		b->subscriptionWorkingTimer = 32;
		b->updateCallLists();
		u->activity = Unit::ACT_RANDOM;
		u->medical = Unit::MED_FREE;
		u->hungriness = 0;
		glob2test::publishCurrentHiringSuggestions(w.game);
		REQUIRE(w.game.hiringCandidates(*b));
		b->minLevelToFlag = NB_UNIT_LEVELS - 1;
		b->updateCallLists();
		REQUIRE(w.game.hiringCandidates(*b));
		CHECK_FALSE(b->subscribeForFlagingStep());
		CHECK(b->unitsWorking.empty());
		b->minLevelToFlag = 0;
		CHECK(b->subscribeForFlagingStep());
		CHECK(b->unitsWorking.size() == 1);
	}
	TEST_CASE("missing lists do not synchronously scan or assign a worker")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame w({.discovered = true, .loadDefaultRace = true});
		auto *b = innWithWorker(w);
		CHECK_FALSE(b->subscribeToBringMaterialsStep());
		CHECK(b->unitsWorking.empty());
		glob2test::publishCurrentHiringSuggestions(w.game);
		CHECK(b->subscribeToBringMaterialsStep());
		CHECK(b->unitsWorking.size() == 1);
	}
	TEST_CASE("transient unavailability and changed cargo are validated on every attempt")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame w({.discovered = true, .loadDefaultRace = true});
		auto *b = innWithWorker(w);
		auto *u = w.game.teams[0]->myUnits[0];
		REQUIRE(u);
		glob2test::publishCurrentHiringSuggestions(w.game);
		u->medical = Unit::MED_HUNGRY;
		CHECK_FALSE(b->subscribeToBringMaterialsStep());
		u->medical = Unit::MED_FREE;
		u->carriedMaterial = -1;
		CHECK_FALSE(b->subscribeToBringMaterialsStep());
		u->carriedMaterial = WHEAT;
		CHECK(b->subscribeToBringMaterialsStep());
	}
	TEST_CASE("all attraction jobs admit through the production candidate consumer")
	{
		glob2test::HeadlessGlobals globals;
		const char *names[] = {"clearingflag", "explorationflag", "warflag"};
		const int units[] = {WORKER, EXPLORER, WARRIOR};
		for (unsigned role = 0; role < 3; ++role)
		{
			CAPTURE(role);
			glob2test::HeadlessGame w({.discovered = true, .loadDefaultRace = true});
			auto *b = w.addBuilding(names[role], 8, 8);
			auto *u = w.addUnit(units[role], 6, 8);
			b->maxUnitWorking = 1;
			b->subscriptionWorkingTimer = 32;
			b->setClearsMaterial(WOOD, true);
			if (role == 0)
				REQUIRE(w.game.map.incResourceByIndex(7, 8, WOOD, 0));
			b->updateCallLists();
			u->activity = Unit::ACT_RANDOM;
			u->medical = Unit::MED_FREE;
			u->hungriness = 0;
			glob2test::publishCurrentHiringSuggestions(w.game);
			CHECK(b->subscribeForFlagingStep());
			REQUIRE(b->unitsWorking.size() == 1);
			CHECK(b->unitsWorking.front() == u);
			CHECK(u->jobPurpose == UnitJobPurpose(int(UnitJobPurpose::Clear) + role));
		}
	}
	TEST_CASE("a returning attraction worker does not wait another recruitment-clock cycle")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame w({.discovered = true, .loadDefaultRace = true});
		auto *b = w.addBuilding("explorationflag", 8, 8);
		auto *u = w.addUnit(EXPLORER, 6, 8);
		b->maxUnitWorking = 1;
		b->subscriptionWorkingTimer = 28;
		b->updateCallLists();
		u->activity = Unit::ACT_FILLING;
		u->medical = Unit::MED_FREE;
		u->hungriness = 0;
		// Install the real schedule in the Game so the normal consumer sees it.
		w.game.hiringPipeline = std::make_unique<HiringPipeline>(w.game.map.computeExecutor());
		unsigned hiredAt = 100;
		for (unsigned tick = 0; tick < 40; ++tick)
		{
			w.game.stepCounter = tick;
			if (tick == 3)
				u->activity = Unit::ACT_RANDOM;
			w.game.hiringPipeline->publishDue(tick);
			w.game.hiringPipeline->observe(w.game, observation(w));
			if (b->subscribeForFlagingStep())
			{
				hiredAt = tick;
				break;
			}
		}
		CHECK(hiredAt >= 4);
		CHECK(hiredAt - 4 <= 8);
	}
	TEST_CASE("an owed attraction round retries for a bounded window")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame w({.discovered = true, .loadDefaultRace = true});
		auto *b = w.addBuilding("explorationflag", 8, 8);
		b->maxUnitWorking = 1;
		b->subscriptionWorkingTimer = 32;
		b->updateCallLists();
		for (unsigned i = 0; i < Hiring::RecruitmentRetryTicks; ++i)
		{
			CHECK_FALSE(b->subscribeForFlagingStep());
			CHECK(b->subscriptionWorkingTimer == 33 + int(i));
		}
		CHECK_FALSE(b->subscribeForFlagingStep());
		CHECK(b->subscriptionWorkingTimer == 0);
	}
	TEST_CASE("whole-game continuation retains an outstanding attraction request")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame original(
			{.discovered = true, .loadDefaultRace = true, .header = true});
		auto *b = original.addBuilding("explorationflag", 8, 8);
		b->maxUnitWorking = 1;
		b->subscriptionWorkingTimer = 32;
		b->updateCallLists();
		CHECK_FALSE(b->subscribeForFlagingStep());
		REQUIRE(b->subscriptionWorkingTimer == 33);
		original.game.observeCompletedWorld();
		const auto checksum = original.game.checkSum();
		auto *storage = new GAGCore::MemoryStreamBackend;
		GAGCore::BinaryOutputStream out(storage);
		original.game.save(&out, false, "Owed recruitment");
		out.flush();
		glob2test::HeadlessGame restored(
			{.discovered = true, .loadDefaultRace = true, .header = true});
		GAGCore::BinaryInputStream in(
			new GAGCore::MemoryStreamBackend(storage->getBuffer(), storage->getPosition()));
		in.seekFromStart(0);
		REQUIRE(restored.game.load(&in));
		CHECK(restored.game.checkSum() == checksum);
		auto *loaded = restored.game.teams[0]->myBuildings[Building::GIDtoID(b->gid)];
		REQUIRE(loaded);
		CHECK(loaded->subscriptionWorkingTimer == 33);
		for (unsigned tick = 0; tick < 12; ++tick)
		{
			original.game.step(0);
			restored.game.step(0);
			CHECK(original.game.checkSum() == restored.game.checkSum());
		}
	}
	TEST_CASE("an expired hybrid attraction request releases unused delivery seats")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame w({.discovered = true, .loadDefaultRace = true});
		auto catalog = nlohmann::json::parse(w.game.buildingsTypes.snapshotJson());
		const auto type = w.game.buildingsTypes.getFinishedTypeNum("inn");
		catalog["variants"][type]["properties"]["zonable"] = {0, 1, 0};
		w.game.buildingsTypes.loadSnapshotJson(catalog.dump());
		w.game.configureBuildingCatalog();
		auto *b = innWithWorker(w);
		b->maxUnitWorking = 2;
		b->updateCallLists();
		auto *extra = w.addUnit(WORKER, 6, 9);
		extra->activity = Unit::ACT_RANDOM;
		extra->medical = Unit::MED_FREE;
		extra->hungriness = 0;
		extra->carriedMaterial = WHEAT;
		b->subscriptionWorkingTimer = 33;
		glob2test::publishCurrentHiringSuggestions(w.game);
		CHECK(b->subscribeWorkStep()); // One fair delivery seat.
		CHECK(b->unitsWorking.size() == 1);
		CHECK_FALSE(b->subscribeWorkStep()); // Attraction seat remains owed.
		CHECK(b->unitsWorking.size() == 1);
		b->subscriptionWorkingTimer = 32 + int(Hiring::RecruitmentRetryTicks);
		CHECK(b->subscribeWorkStep());
		CHECK(b->subscriptionWorkingTimer == 0);
		CHECK(b->unitsWorking.size() == 2);
	}
	TEST_CASE("imminent attraction peers receive alternatives without inactive peers claiming them")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame w({.discovered = true, .loadDefaultRace = true});
		std::vector<Building *> flags;
		for (unsigned i = 0; i < 4; ++i)
		{
			auto *b = w.addBuilding("warflag", 8 + i * 3, 8);
			b->maxUnitWorking = 2;
			b->subscriptionWorkingTimer = i == 0 ? 20 : 32;
			b->updateCallLists();
			flags.push_back(b);
		}
		for (unsigned i = 0; i < 6; ++i)
		{
			auto *u = w.addUnit(WARRIOR, 6 + i, 10);
			u->activity = Unit::ACT_RANDOM;
			u->medical = Unit::MED_FREE;
			u->hungriness = 0;
			u->performance[ATTACK_SPEED] = 100 - i;
		}
		glob2test::publishCurrentHiringSuggestions(w.game);
		for (unsigned i = 1; i < flags.size(); ++i)
		{
			CHECK(flags[i]->subscribeForFlagingStep());
			CHECK(flags[i]->unitsWorking.size() == 2);
		}
		CHECK(flags[0]->unitsWorking.empty());
	}
}

TEST_SUITE("HiringPlanner")
{
	TEST_CASE("food-unsafe high-skill workers cannot crowd a viable worker out")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame w({.discovered = true, .loadDefaultRace = true});
		auto *b = innWithWorker(w);
		auto *viable = w.game.teams[0]->myUnits[0];
		for (unsigned i = 0; i < 4; ++i)
		{
			auto *u = w.addUnit(WORKER, 16 + i, 16);
			u->activity = Unit::ACT_RANDOM;
			u->medical = Unit::MED_FREE;
			u->constructionLevel = 5;
			u->hungry = 100;
			u->trigHungry = 99;
			u->hungriness = 1;
			u->carriedMaterial = WHEAT;
		}
		HiringPlanner::Planner cache;
		for (unsigned tick : {0u, 4u, 8u})
		{
			w.game.stepCounter = tick;
			const auto output = cache.rank(HiringPipeline::bindRoutes(w.game, observation(w)), 0);
			REQUIRE(output.buildings.size() == 1);
			bool found = false;
			for (const auto &list : output.buildings[0].jobs)
				if (list.key == Hiring::Key{Hiring::Job::Delivery, WHEAT})
				{
					REQUIRE(list.candidates.size() == 1);
					CHECK(list.candidates[0].unit == Game::refOf(viable));
					found = true;
				}
			CHECK(found);
		}
		(void)b;
	}
}

TEST_SUITE("HiringGradientPreparation")
{
	TEST_CASE("cold shortlisted destinations are settled before suggestions publish")
	{
		glob2test::HeadlessGlobals globals;
		for (unsigned participants : {1u, 4u})
			for (unsigned invalidate : {0u, 1u, 2u})
			{
				CAPTURE(participants);
				CAPTURE(invalidate);
				glob2test::HeadlessGame w(
					{.wDec = 6, .hDec = 6, .discovered = true, .loadDefaultRace = true});
				w.game.map.configureCompute(participants);
				auto *b = w.addBuilding("inn", 8, 8);
				b->maxUnitWorking = 1;
				b->setMaterial(WHEAT, 0);
				b->updateCallLists();
				auto *u = w.addUnit(WORKER, 35, 35);
				u->activity = Unit::ACT_RANDOM;
				u->medical = Unit::MED_FREE;
				u->hungriness = 0;
				u->carriedMaterial = WHEAT;
				const auto slot = b->routeSlot(u->swimClass(), BuildingRoute::Footprint);
				REQUIRE(!b->globalGradient[slot]);
				GradientExecutionAudit audit;
				HiringPipeline pipeline(w.game.map.computeExecutor());
				pipeline.observe(w.game, observation(w));
				pipeline.drain();
				if (participants > 1 && GAGCore::ThreadSupport::available)
				{
					CHECK(audit.counts(GradientExecutionAudit::Kind::Seed).owner == 0);
					CHECK(audit.counts(GradientExecutionAudit::Kind::Seed).other >= 1);
				}
				REQUIRE(!b->globalGradient[slot]);
				if (invalidate == 1)
					w.game.map.bumpTopologyGeneration();
				else if (invalidate == 2)
					b->supersedeGradient(slot);
				pipeline.publishDue(HiringPipeline::Delay);
				const auto *lists = pipeline.suggestions(*b);
				REQUIRE(lists);
				bool found = false;
				for (const auto &list : lists->jobs)
					for (const auto &candidate : list.candidates)
						if (candidate.unit == Game::refOf(u))
							found = true;
				REQUIRE(found);
				if (invalidate)
					CHECK(!b->globalGradient[slot]);
				else
				{
					REQUIRE(b->globalGradient[slot]);
					REQUIRE(b->globalGradientSearch[slot]);
					CHECK(b->globalGradientSearch[slot]->resolved(
						w.game.map.coordToIndex(u->posX, u->posY)));
					CHECK(b->gradientGeneration[slot] == w.game.map.topologyGeneration);
				}
			}
	}
}

TEST_SUITE("HiringGradientPreparation")
{
	TEST_CASE("prewarming reuses a dirty serving generation and its search")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame w({.discovered = true, .loadDefaultRace = true});
		auto *b = innWithWorker(w);
		auto *u = w.game.teams[0]->myUnits[0];
		const int slot = b->routeSlot(u->swimClass(), BuildingRoute::Footprint);
		b->globalGradient[slot] = w.game.map.acquireBuildingGradientBuffer();
		w.game.map.updateGlobalGradient(b, u->swimClass(), BuildingRoute::Footprint);
		auto *data = b->globalGradient[slot];
		auto *search = b->globalGradientSearch[slot].get();
		b->dirtyGradient[slot] = true;
		HiringPipeline pipeline(w.game.map.computeExecutor());
		pipeline.observe(w.game, observation(w));
		pipeline.drain();
		pipeline.publishDue(HiringPipeline::Delay);
		CHECK(b->globalGradient[slot] == data);
		CHECK(b->globalGradientSearch[slot].get() == search);
		CHECK(b->dirtyGradient[slot]);
		REQUIRE(pipeline.suggestions(*b));
	}
}

TEST_SUITE("HiringGradientPreparation")
{
	TEST_CASE("a stale unreachable serving field keeps candidates for a live refresh")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame w({.discovered = true, .loadDefaultRace = true});
		auto *b = innWithWorker(w);
		auto *u = w.game.teams[0]->myUnits[0];
		const int slot = b->routeSlot(u->swimClass(), BuildingRoute::Footprint);
		b->globalGradient[slot] = w.game.map.acquireBuildingGradientBuffer();
		std::fill_n(b->globalGradient[slot], std::size_t(w.game.map.getW()) * w.game.map.getH(),
					GRADIENT_FORBIDDEN);
		b->dirtyGradient[slot] = true;
		HiringPipeline pipeline(w.game.map.computeExecutor());
		pipeline.observe(w.game, observation(w));
		pipeline.drain();
		pipeline.publishDue(HiringPipeline::Delay);
		const auto *lists = pipeline.suggestions(*b);
		REQUIRE(lists);
		bool found = false;
		for (const auto &list : lists->jobs)
			for (const auto &candidate : list.candidates)
				if (candidate.unit == Game::refOf(u))
					found = true;
		CHECK(found);
	}
}

TEST_SUITE("HiringGradientPreparation")
{
	TEST_CASE("primary-only prewarming leaves fallback cells lazy while retaining candidates")
	{
		glob2test::HeadlessGlobals globals;
		for (unsigned participants : {1u, 4u})
			for (unsigned count : {1u, 3u})
			{
				CAPTURE(participants);
				CAPTURE(count);
				glob2test::HeadlessGame w(
					{.wDec = 7, .hDec = 7, .discovered = true, .loadDefaultRace = true});
				w.game.map.configureCompute(participants);
				auto *b = w.addBuilding("inn", 8, 8);
				b->maxUnitWorking = 1;
				b->setMaterial(WHEAT, 0);
				b->updateCallLists();
				std::vector<Unit *> units;
				for (unsigned i = 0; i < count; ++i)
				{
					auto *u = w.addUnit(WORKER, i ? 58 + i : 35, 8);
					u->activity = Unit::ACT_RANDOM;
					u->medical = Unit::MED_FREE;
					u->hungriness = 0;
					u->carriedMaterial = WHEAT;
					units.push_back(u);
				}
				HiringPipeline pipeline(w.game.map.computeExecutor());
				pipeline.observe(w.game, observation(w));
				pipeline.drain();
				pipeline.publishDue(HiringPipeline::Delay);
				const auto *lists = pipeline.suggestions(*b);
				REQUIRE(lists);
				const HiringPlanner::List *delivery = nullptr;
				for (const auto &list : lists->jobs)
					if (list.key == Hiring::Key{Hiring::Job::Delivery, WHEAT})
						delivery = &list;
				REQUIRE(delivery);
				REQUIRE(delivery->candidates.size() == count);
				CHECK(delivery->primaryCount == 1);
				CHECK(delivery->candidates.front().unit == Game::refOf(units.front()));
				const auto slot =
					b->routeSlot(units.front()->swimClass(), BuildingRoute::Footprint);
				REQUIRE(b->globalGradientSearch[slot]);
				CHECK(b->globalGradientSearch[slot]->resolved(
					w.game.map.coordToIndex(units.front()->posX, 8)));
				if (count == 3)
				{
					const auto cell = w.game.map.coordToIndex(units.back()->posX, 8);
					CHECK(b->globalGradientSearch[slot]->resolved(cell) == false);
					auto lease = b->leaseGradient(slot);
					REQUIRE(lease);
					CHECK(lease->distance(units.back()->posX, 8) != INT_MAX);
					CHECK(b->globalGradientSearch[slot]->resolved(cell));
				}
			}
	}
}

TEST_SUITE("HiringGradientPreparation")
{
	TEST_CASE(
		"a cold primary field survives binary and text saves without early publication [artifacts]")
	{
		glob2test::HeadlessGlobals globals;
		for (bool text : {false, true})
		{
			CAPTURE(text);
			glob2test::HeadlessGame original({.wDec = 7,
											  .hDec = 7,
											  .discovered = true,
											  .loadDefaultRace = true,
											  .header = true});
			auto *b = original.addBuilding("inn", 8, 8);
			b->maxUnitWorking = 1;
			b->setMaterial(WHEAT, 0);
			b->updateCallLists();
			auto *u = original.addUnit(WORKER, 35, 8);
			u->activity = Unit::ACT_RANDOM;
			u->medical = Unit::MED_FREE;
			u->hungriness = 0;
			u->carriedMaterial = WHEAT;
			const auto ref = Game::refOf(b);
			const int slot = b->routeSlot(u->swimClass(), BuildingRoute::Footprint);
			original.game.observeCompletedWorld();
			auto *storage = new GAGCore::MemoryStreamBackend;
			std::unique_ptr<GAGCore::OutputStream> out;
			if (text)
				out = std::make_unique<GAGCore::TextOutputStream>(storage);
			else
				out = std::make_unique<GAGCore::BinaryOutputStream>(storage);
			original.game.save(out.get(), false, "Pending primary gradient");
			out->flush();
			CHECK(!b->globalGradient[slot]);
			glob2test::writeFile(glob2test::artifactDir() /
									 (text ? "pending-primary.game.txt" : "pending-primary.game"),
								 std::string(storage->getBuffer(), storage->getPosition()));
			glob2test::HeadlessGame restored({.wDec = 7,
											  .hDec = 7,
											  .discovered = true,
											  .loadDefaultRace = true,
											  .header = true});
			auto *input =
				new GAGCore::MemoryStreamBackend(storage->getBuffer(), storage->getPosition());
			input->seekFromStart(0);
			std::unique_ptr<GAGCore::InputStream> in;
			if (text)
				in = std::make_unique<GAGCore::TextInputStream>(input);
			else
				in = std::make_unique<GAGCore::BinaryInputStream>(input);
			REQUIRE(restored.game.load(in.get()));
			auto *copy = restored.game.resolveBuilding(ref);
			REQUIRE(copy);
			CHECK(!copy->globalGradient[slot]);
			original.game.stepCounter = restored.game.stepCounter = HiringPipeline::Delay;
			original.game.hiringPipeline->publishDue(HiringPipeline::Delay);
			restored.game.hiringPipeline->publishDue(HiringPipeline::Delay);
			REQUIRE(b->globalGradient[slot]);
			REQUIRE(copy->globalGradient[slot]);
			CHECK(b->globalGradient[slot][original.game.map.coordToIndex(35, 8)] ==
				  copy->globalGradient[slot][restored.game.map.coordToIndex(35, 8)]);
			for (unsigned i = 0; i < 16; ++i)
			{
				original.game.step(0);
				restored.game.step(0);
				CHECK(original.game.checkSum() == restored.game.checkSum());
			}
		}
	}
    TEST_CASE("historical fixture adapter omits only empty new hiring sections")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.discovered=true,.loadDefaultRace=true});
        for (int version : {164,169,173,174,175}) {
            CAPTURE(version);
            auto* bytes=new GAGCore::MemoryStreamBackend;
            glob2test::LegacyPopulationOutput output(bytes,&world.game,version);
            HiringPipeline original(world.game.map.computeExecutor());
            original.save(&output);
            output.writeUint32(0xabcdef01,"sentinel"); output.flush();
            GAGCore::BinaryInputStream input(
                new GAGCore::MemoryStreamBackend(bytes->getBuffer(),bytes->getPosition()));
            input.seekFromStart(0);
            HiringPipeline restored(world.game.map.computeExecutor());
            restored.load(world.game,&input,version);
            CHECK(input.readUint32("sentinel")==0xabcdef01);
        }
        // A synthetic historical fixture must never erase real new-format work.
        for (const auto* count : {"gradients","initializations"}) {
            auto* bytes=new GAGCore::MemoryStreamBackend;
            glob2test::LegacyPopulationOutput output(bytes,&world.game,173);
            output.writeEnterSection("HiringPipeline");
            CHECK_THROWS_AS(output.writeUint32(1,count),std::logic_error);
            output.writeLeaveSection();
        }
    }
	TEST_CASE("pre-174 formats load without gradient or proactive initialization records")
	{
		glob2test::HeadlessGlobals globals;
		for (int version : {170, 171, 173})
		{
			CAPTURE(version);
			glob2test::HeadlessGame w({.discovered = true, .loadDefaultRace = true});
			auto *bytes = new GAGCore::MemoryStreamBackend;
			GAGCore::BinaryOutputStream out(bytes);
			out.writeEnterSection("HiringPipeline");
			out.writeUint8(1, "hasLast");
			out.writeUint32(0, "last");
			out.writeUint32(HiringPipeline::Delay, "delay");
			out.writeUint32(HiringPipeline::Refresh, "refresh");
			out.writeUint32(HiringPipeline::Extras, "extras");
			out.writeUint32(HiringPipeline::Capacity, "capacity");
			out.writeEnterSection("published");
			for (unsigned t = 0; t < 16; ++t)
			{
				out.writeEnterSection(t);
				out.writeUint8(0, "present");
				out.writeLeaveSection();
			}
			out.writeLeaveSection();
			out.writeUint32(1, "pending");
			out.writeEnterSection("admissions");
			out.writeEnterSection(0);
			out.writeUint32(0, "tick");
			out.writeUint32(HiringPipeline::Delay, "due");
			out.writeUint32(1, "teams");
			out.writeEnterSection("work");
			out.writeEnterSection(0);
			out.writeUint32(0, "tick");
			out.writeUint32(0, "team");
			out.writeUint32(0, "buildings");
			out.writeEnterSection("buildingLists");
			out.writeLeaveSection();
			if (version >= FILE_FORMAT_VERSION_HIRING_GRADIENTS)
			{
				out.writeUint32(0, "gradients");
				out.writeEnterSection("preparedGradients");
				out.writeLeaveSection();
			}
			out.writeLeaveSection();
			out.writeLeaveSection();
			out.writeLeaveSection();
			out.writeLeaveSection();
			out.writeLeaveSection();
			out.writeUint32(0xabcdef01, "sentinel");
			GAGCore::BinaryInputStream in(
				new GAGCore::MemoryStreamBackend(bytes->getBuffer(), bytes->getPosition()));
			in.seekFromStart(0);
			HiringPipeline pipeline(w.game.map.computeExecutor());
			pipeline.load(w.game, &in, version);
			CHECK(in.readUint32("sentinel") == 0xabcdef01);
			pipeline.publishDue(HiringPipeline::Delay);
			CHECK(pipeline.metrics.published == 1);
		}
	}
}

TEST_SUITE("BuildingGradientInitialization")
{
	TEST_CASE("saving a primary-only job does not turn it into a proactive claim")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame w({.discovered = true, .loadDefaultRace = true});
		auto *b = innWithWorker(w);
		HiringPipeline original(w.game.map.computeExecutor());
		original.observe(w.game, observation(w));
		auto *bytes = new GAGCore::MemoryStreamBackend;
		GAGCore::BinaryOutputStream out(bytes);
		original.save(&out);
		GAGCore::BinaryInputStream in(
			new GAGCore::MemoryStreamBackend(bytes->getBuffer(), bytes->getPosition()));
		in.seekFromStart(0);
		HiringPipeline restored(w.game.map.computeExecutor());
		restored.load(w.game, &in);
		CHECK(!original.preparing(*b, b->routeSlot(0, BuildingRoute::Footprint)));
		CHECK(!restored.preparing(*b, b->routeSlot(0, BuildingRoute::Footprint)));
	}

	TEST_CASE("legacy proactive requests prepare busy units outside the hiring cohort")
	{
		glob2test::HeadlessGlobals globals;
		for (unsigned participants : {1u, 4u})
		{
			glob2test::HeadlessGame w(
				{.wDec = 7, .hDec = 7, .discovered = true, .loadDefaultRace = true});
			w.game.map.configureCompute(participants);
			w.game.stepCounter = 1; // Team zero's next shortlist is three ticks away.
			auto *b = w.addBuilding("inn", 8, 8);
			b->maxUnitWorking = 0;
			b->updateCallLists();
			auto *u = w.addUnit(WORKER, 60, 8);
			u->medical = Unit::MED_HUNGRY;
			const int slot = b->routeSlot(u->swimClass(), BuildingRoute::Footprint);
			GradientExecutionAudit audit;
			w.game.hiringPipeline = std::make_unique<HiringPipeline>(w.game.map.computeExecutor());
			auto &pipeline = *w.game.hiringPipeline;
			pipeline.requestInitialization(*b);
			pipeline.observe(w.game, observation(w));
			CHECK(pipeline.preparing(*b, slot));
			CHECK(w.game.map.requestBuildingField(b, u->swimClass(), BuildingRoute::Footprint) ==
				  FieldReadiness::Pending);
			CHECK(w.game.map.buildingGradientPipelineStatus().queued == 0);
			pipeline.drain();
			CHECK(!b->globalGradient[slot]);
			CHECK(audit.counts(GradientExecutionAudit::Kind::Seed).owner +
					  audit.counts(GradientExecutionAudit::Kind::Seed).other ==
				  1);
			if (participants == 4)
				CHECK(audit.counts(GradientExecutionAudit::Kind::Seed).owner == 0);
			pipeline.publishDue(2);
			REQUIRE(b->globalGradientSearch[slot]);
			CHECK(!b->globalGradientSearch[slot]->resolved(w.game.map.coordToIndex(60, 8)));
			CHECK(!pipeline.preparing(*b, slot));
			CHECK(!pipeline.suggestions(*b));
		}
	}
	TEST_CASE("creation upgrade and cancellation leave routes lazy")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame w({.discovered = true, .loadDefaultRace = true});
		w.game.stepCounter = 1; // Team zero is outside the hiring cohort.
		auto *b = innWithWorker(w, 1);
		auto checkLazy = [&]
		{
			const int slot = b->routeSlot(0, BuildingRoute::Footprint);
			w.game.observeCompletedWorld();
			w.game.hiringPipeline->drain();
			CHECK(!w.game.hiringPipeline->preparing(*b, slot));
			CHECK(!b->globalGradient[slot]);
			CHECK(w.game.map.buildingGradientPipelineStatus().queued == 0);
		};
		checkLazy();
		REQUIRE(b->launchConstruction(1, 1));
		REQUIRE(b->tryToBuildingSiteRoom());
		w.game.stepCounter = 2;
		checkLazy();
		b->cancelConstruction(0);
		w.game.stepCounter = 3;
		checkLazy();
		CHECK(w.game.map.requestBuildingField(b, 0, BuildingRoute::Footprint) ==
			  FieldReadiness::Pending);
		CHECK(w.game.map.buildingGradientPipelineStatus().queued == 1);
	}
	TEST_CASE("legacy preparation and primary hiring seed one shared route")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame w(
			{.wDec = 7, .hDec = 7, .discovered = true, .loadDefaultRace = true});
		w.game.stepCounter = 4;
		auto *b = innWithWorker(w);
		CHECK(w.game.map.requestBuildingField(b, 0, BuildingRoute::Footprint) ==
			  FieldReadiness::Pending);
		GradientExecutionAudit audit;
		w.game.hiringPipeline = std::make_unique<HiringPipeline>(w.game.map.computeExecutor());
		auto &pipeline = *w.game.hiringPipeline;
		pipeline.requestInitialization(*b);
		const auto world = observation(w);
		pipeline.observe(w.game, world);
		pipeline.drain();
		CHECK(audit.counts(GradientExecutionAudit::Kind::Seed).owner +
				  audit.counts(GradientExecutionAudit::Kind::Seed).other ==
			  1);
		w.game.map.submitWorldWork(world);
		CHECK(w.game.map.buildingGradientPipelineStatus().jobs == 0);
		pipeline.publishDue(5);
		REQUIRE(pipeline.suggestions(*b));
	}
	TEST_CASE(
		"queued and off-cohort pending initialization survive binary and text saves [artifacts]")
	{
		glob2test::HeadlessGlobals globals;
		for (bool text : {false, true})
			for (bool pending : {false, true})
			{
				glob2test::HeadlessGame original(
					{.discovered = true, .loadDefaultRace = true, .header = true});
				original.game.stepCounter = 1;
				auto *b = original.addBuilding("inn", 8, 8);
				b->maxUnitWorking = 0;
				b->updateCallLists();
				auto *u = original.addUnit(WORKER, 6, 8);
				u->medical = Unit::MED_HUNGRY;
				original.game.hiringPipeline =
					std::make_unique<HiringPipeline>(original.game.map.computeExecutor());
				original.game.hiringPipeline->requestInitialization(*b);
				const auto ref = Game::refOf(b);
				const int slot = b->routeSlot(u->swimClass(), BuildingRoute::Footprint);
				if (pending)
					original.game.observeCompletedWorld();
				auto *storage = new GAGCore::MemoryStreamBackend;
				std::unique_ptr<GAGCore::OutputStream> out;
				if (text)
					out = std::make_unique<GAGCore::TextOutputStream>(storage);
				else
					out = std::make_unique<GAGCore::BinaryOutputStream>(storage);
				original.game.save(out.get(), false, "Proactive continuation");
				out->flush();
				glob2test::writeFile(glob2test::artifactDir() /
										 (std::string(pending ? "pending" : "queued") +
										  (text ? ".game.txt" : ".game")),
									 std::string(storage->getBuffer(), storage->getPosition()));
				glob2test::HeadlessGame restored(
					{.discovered = true, .loadDefaultRace = true, .header = true});
				auto *bytes =
					new GAGCore::MemoryStreamBackend(storage->getBuffer(), storage->getPosition());
				bytes->seekFromStart(0);
				std::unique_ptr<GAGCore::InputStream> in;
				if (text)
					in = std::make_unique<GAGCore::TextInputStream>(bytes);
				else
					in = std::make_unique<GAGCore::BinaryInputStream>(bytes);
				REQUIRE(restored.game.load(in.get()));
				auto *copy = restored.game.resolveBuilding(ref);
				REQUIRE(copy);
				if (!pending)
				{
					original.game.observeCompletedWorld();
					restored.game.observeCompletedWorld();
				}
				CHECK(restored.game.hiringPipeline->preparing(*copy, slot));
				original.game.hiringPipeline->publishDue(2);
				restored.game.hiringPipeline->publishDue(2);
				REQUIRE(b->globalGradient[slot]);
				REQUIRE(copy->globalGradient[slot]);
				CHECK(original.game.checkSum() == restored.game.checkSum());
			}
	}
}
