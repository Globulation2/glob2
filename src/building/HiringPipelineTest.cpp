// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "HiringPipeline.h"
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
Building *innWithWorker(glob2test::HeadlessGame &w)
{
	auto *b = w.addBuilding("inn", 8, 8);
	auto *u = w.addUnit(WORKER, 6, 8);
	if (!b || !u)
		throw std::runtime_error("Hiring fixture");
	b->maxUnitWorking = 1;
	b->materials[WHEAT] = 0;
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
			w.game.anyPlayerWaited = mode == 1;
			w.game.prepareReadBoundary(mode == 2);
			const auto initial = w.game.hiringChecksum();
			REQUIRE(initial != 0);
			if (mode == 0)
				reference = initial;
			else
				CHECK(initial == reference);
			// A matched order changes the live world at this same boundary.
			b->desiredMaxUnitWorking = 3;
			b->updateCallLists();
			w.game.snapshotStore().invalidateBoundary();
			w.game.anyPlayerWaited = false;
			w.game.prepareReadBoundary(false);
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
			p.publishDue(3);
			CHECK(p.suggestions(*b) == nullptr);
			p.publishDue(4);
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
				w.game.prepareReadBoundary(false);
				actual.emplace_back(w.game.checkSum(), w.game.hiringChecksum());
				w.game.syncStep(0);
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
			CHECK(bool(original.suggestions(*b)) == (phase >= 4));
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
				original.game.syncStep(0);
			original.game.prepareReadBoundary(false);
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
				original.game.syncStep(0);
				restored.game.syncStep(0);
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
			b->clearingMaterials[WOOD] = true;
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
		original.game.prepareReadBoundary(false);
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
			original.game.syncStep(0);
			restored.game.syncStep(0);
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
