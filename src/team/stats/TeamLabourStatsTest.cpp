// SPDX-License-Identifier: GPL-3.0-or-later
// Worker time use, combat-death attribution and the defence snapshot in gameplay
// measurements (docs/ai/gameplay-statistics.md): each worker-tick lands in one
// bucket, places agree with a brute-force reference, and nothing touches the
// simulation checksum.
#include "EngineFixtures.h"
#include "TeamStat.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <algorithm>
#include <chrono>
#include <memory>
#include <numeric>
#include <random>

namespace
{
using M = GameplayMeasurements;

Uint64 workerTicks(const M &m)
{
	Uint64 total = std::accumulate(std::begin(m.labour), std::end(m.labour), Uint64(0));
	for (const auto &job : m.filling)
		total += std::accumulate(std::begin(job), std::end(job), Uint64(0));
	return total;
}

constexpr int placeRadius = GROWTH_COVERAGE_RADII[M::PLACE_BAND];

// Place by scanning every footprint tile of every building.
M::Place referencePlace(Game &game, const Team *team, int x, int y)
{
	int own = 1 << 30, enemy = 1 << 30;
	for (int t = 0; t < game.teamsCount(); ++t)
	{
		const Team *other = game.teams[t];
		for (int i = 0; i < Building::MAX_COUNT; ++i)
		{
			const Building *b = other->myBuildings[i];
			if (!b || b->type->isVirtual)
				continue;
			for (int by = b->posY; by < b->posY + b->type->height; ++by)
				for (int bx = b->posX; bx < b->posX + b->type->width; ++bx)
				{
					const int d = game.map.warpDistMax(x, y, bx, by);
					if (other == team)
						own = std::min(own, d);
					else if (team->enemies & other->me)
						enemy = std::min(enemy, d);
				}
		}
	}
	if (own <= placeRadius)
		return M::HOME;
	return enemy <= placeRadius ? M::AWAY : M::FIELD;
}

// Take every team's 512-tick sample, which snapshots the buildings places use.
void sampleAll(Game &game)
{
	game.stepCounter = (game.stepCounter | 511) + 1;
	for (int t = 0; t < game.teamsCount(); ++t)
		game.teams[t]->stats.refreshMeasurements(game.teams[t]);
}

// Team 0 is at war with team 1 and allied with team 2, on a 128x128 torus.
struct ThreeTeams
{
	glob2test::HeadlessGame world{glob2test::GameOptions{
		.wDec = 7, .hDec = 7, .teams = 3, .clearImmobile = true, .loadDefaultRace = true}};
	ThreeTeams()
	{
		auto **t = world.game.teams;
		t[0]->enemies = t[1]->me;
		t[0]->allies = t[0]->me | t[2]->me;
		t[1]->enemies = t[0]->me | t[2]->me;
		t[2]->enemies = t[1]->me;
		t[2]->allies = t[2]->me | t[0]->me;
	}
	Game &game() { return world.game; }
	Team *team(int i) { return world.game.teams[i]; }
	// A flag as the engine places it: a building, but not on the map's building layer.
	Building *flag(const char *name, int x, int y, int team)
	{
		auto *b = world.addBuilding(name, x, y, 0, team);
		world.game.map.setBuilding(x, y, b->type->width, b->type->height, NOGBID);
		return b;
	}
};
} // namespace

TEST_SUITE("TeamStatsSave")
{
	TEST_CASE("every live worker-tick lands in exactly one labour bucket")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world(glob2test::GameOptions{.wDec = 6, .hDec = 6, .loadDefaultRace = true});
		auto *worker = world.addUnit(WORKER, 30, 30);
		auto *swarm = world.addBuilding("swarm", 20, 20);
		auto *inn = world.addBuilding("inn", 40, 20);
		auto *site = world.game.addBuilding(
			20, 40, globalContainer->buildingsTypes.getTypeNum("inn", 0, true), 0);
		REQUIRE(site != nullptr);
		auto &m = world.team->stats.measurements;

		auto expect = [&](Uint64 &bucket)
		{
			const Uint64 before = bucket, total = workerTicks(m);
			const Uint32 checksum = world.game.checkSum();
			world.team->stats.observeLabour(worker);
			CHECK(world.game.checkSum() == checksum);
			CHECK(bucket == before + 1);
			CHECK(workerTicks(m) == total + 1);
		};
		worker->medical = Unit::MED_FREE;
		worker->activity = Unit::ACT_RANDOM;
		expect(m.labour[M::IDLE]);

		worker->medical = Unit::MED_HUNGRY;
		worker->displacement = Unit::DIS_GOING_TO_BUILDING;
		worker->targetBuilding = nullptr;
		expect(m.labour[M::EAT_NO_INN]);
		worker->targetBuilding = inn;
		expect(m.labour[M::EAT_WALKING]);
		CHECK(m.eatWalkSamples == 1);
		CHECK(m.eatWalkDistance == Uint64(world.game.map.warpDistMax(30, 30, inn->getMidX(), inn->getMidY())));
		worker->displacement = Unit::DIS_INSIDE;
		expect(m.labour[M::EAT_INSIDE]);

		worker->medical = Unit::MED_DAMAGED;
		worker->displacement = Unit::DIS_GOING_TO_BUILDING;
		worker->targetBuilding = nullptr;
		expect(m.labour[M::HEAL_NO_HOSPITAL]);
		worker->targetBuilding = inn;
		expect(m.labour[M::HEAL_WALKING]);
		worker->displacement = Unit::DIS_ENTERING_BUILDING;
		expect(m.labour[M::HEAL_INSIDE]);

		worker->medical = Unit::MED_FREE;
		worker->activity = Unit::ACT_UPGRADING;
		worker->destinationPurpose = WALK;
		worker->displacement = Unit::DIS_GOING_TO_BUILDING;
		expect(m.labour[M::TRAIN_WALKING]);
		worker->displacement = Unit::DIS_INSIDE;
		expect(m.labour[M::TRAIN_INSIDE]);
		worker->destinationPurpose = HEAL;
		expect(m.labour[M::HEAL_INSIDE]);

		worker->activity = Unit::ACT_FLAG;
		expect(m.labour[M::FLAG_WORK]);

		worker->activity = Unit::ACT_FILLING;
		worker->attachedBuilding = nullptr;
		expect(m.labour[M::OTHER_ACTIVITY]);
		const std::pair<Building *, M::LabourJob> jobs[] = {
			{swarm, M::SWARM_JOB}, {inn, M::INN_JOB}, {site, M::SITE_JOB}};
		for (const auto &[building, job] : jobs)
		{
			worker->attachedBuilding = building;
			worker->displacement = Unit::DIS_GOING_TO_RESOURCE;
			expect(m.filling[job][M::TO_RESOURCE]);
			worker->displacement = Unit::DIS_HARVESTING;
			expect(m.filling[job][M::HARVESTING]);
			CHECK(m.harvestSamples[job] == 1);
			CHECK(m.harvestDistance[job] ==
				  Uint64(world.game.map.warpDistMax(30, 30, building->getMidX(), building->getMidY())));
			worker->displacement = Unit::DIS_GOING_TO_BUILDING;
			expect(m.filling[job][M::TO_BUILDING]);
			worker->displacement = Unit::DIS_INSIDE;
			expect(m.filling[job][M::OTHER_PHASE]);
		}

		// Units that are not live workers are not counted.
		auto *warrior = world.addUnit(WARRIOR, 34, 34);
		const Uint64 total = workerTicks(m);
		world.team->stats.observeLabour(warrior);
		world.team->stats.observeLabour(nullptr);
		worker->isDead = true;
		world.team->stats.observeLabour(worker);
		worker->isDead = false;
		CHECK(workerTicks(m) == total);

		worker->attachedBuilding = worker->targetBuilding = nullptr;
	}

	TEST_CASE("places: own buildings first, then enemies; allies, flags and new buildings do not count")
	{
		glob2test::HeadlessGlobals globals;
		ThreeTeams w;
		auto *own = w.world.addBuilding("swarm", 10, 10, 0, 0);
		const int right = 10 + own->type->width - 1;
		const int r = placeRadius;
		w.world.addBuilding("swarm", 60, 10, 0, 1); // enemy
		w.world.addBuilding("swarm", 10, 60, 0, 2); // ally
		w.flag("warflag", 60, 60, 1);               // enemy flag
		// Contested ground: within reach of both an own and an enemy building.
		w.world.addBuilding("swarm", right + 20, 30, 0, 1);
		w.world.addBuilding("swarm", 1, 100, 0, 0); // wraps to (126, 98)
		sampleAll(w.game());
		auto place = [&](int x, int y) { return TeamStats::placeOf(w.team(0), x, y); };
		CHECK(place(11, 11) == M::HOME);
		CHECK(place(right + r, 11) == M::HOME);
		CHECK(place(right + r + 1, 11) == M::FIELD);
		CHECK(place(55, 11) == M::AWAY);
		CHECK(place(11, 60) == M::FIELD);
		CHECK(place(60, 60) == M::FIELD);
		CHECK(place(right + 10, 26) == M::HOME);
		CHECK(place(right + 22, 40) == M::AWAY);
		CHECK(place(126, 98) == M::HOME);
		// A building placed after the last sample counts from the next one.
		w.world.addBuilding("swarm", 90, 90, 0, 0);
		CHECK(place(90, 90) == M::FIELD);
		sampleAll(w.game());
		CHECK(place(90, 90) == M::HOME);
	}

	TEST_CASE("place lookup agrees with a brute-force scan of every building tile")
	{
		glob2test::HeadlessGlobals globals;
		ThreeTeams w;
		std::mt19937 random(1234);
		const char *types[] = {"swarm", "inn", "hospital", "defencetower"};
		for (int placed = 0, attempts = 0; placed < 40 && attempts < 1000; ++attempts)
		{
			const int team = random() % 3;
			const char *name = types[random() % 4];
			const int type = globalContainer->buildingsTypes.getTypeNum(name, 0, false);
			REQUIRE(type >= 0);
			const auto *bt = globalContainer->buildingsTypes.get(type);
			const int x = random() % 128, y = random() % 128;
			if (!w.game().map.isFreeForBuilding(x, y, bt->width, bt->height))
				continue;
			w.world.addBuilding(name, x, y, 0, team);
			++placed;
		}
		sampleAll(w.game());
		for (int i = 0; i < 4000; ++i)
		{
			const int x = random() % 128, y = random() % 128, team = random() % 3;
			CAPTURE(x);
			CAPTURE(y);
			CAPTURE(team);
			REQUIRE(TeamStats::placeOf(w.team(team), x, y) == referencePlace(w.game(), w.team(team), x, y));
		}
	}

	TEST_CASE("combat deaths are attributed by place and assignment")
	{
		glob2test::HeadlessGlobals globals;
		ThreeTeams w;
		auto *home = w.world.addBuilding("swarm", 10, 10, 0, 0);
		w.world.addBuilding("swarm", 40, 10, 0, 1);
		auto *war = w.flag("warflag", 40, 30, 0);
		auto *clearing = w.flag("clearingflag", 30, 50, 0);
		auto *exploration = w.flag("explorationflag", 50, 50, 0);
		sampleAll(w.game());
		auto &m = w.team(0)->stats.measurements;
		struct Case
		{
			int type, x, y;
			Building *attached;
			M::Place place;
			M::Assignment assignment;
		};
		const Case cases[] = {
			{WARRIOR, 41, 14, war, M::AWAY, M::WAR_FLAG},
			{WARRIOR, 14, 14, nullptr, M::HOME, M::UNASSIGNED},
			{WORKER, 30, 51, clearing, M::FIELD, M::CLEARING_FLAG},
			{EXPLORER, 50, 51, exploration, M::FIELD, M::EXPLORATION_FLAG},
			{WORKER, 15, 14, home, M::HOME, M::OTHER_BUILDING},
		};
		for (const auto &c : cases)
		{
			CAPTURE(c.x);
			CAPTURE(c.y);
			auto *u = w.world.addUnit(c.type, c.x, c.y, 0);
			u->attachedBuilding = c.attached;
			if(c.attached && c.attached->type->zonable[c.type])u->activity=Unit::ACT_FLAG;
			const Uint64 place = m.combatDeathPlace[c.type][c.place];
			const Uint64 assignment = m.combatDeathAssignment[c.type][c.assignment];
			const Uint32 checksum = w.game().checkSum();
			w.team(0)->stats.recordCombatDeath(u);
			CHECK(w.game().checkSum() == checksum);
			CHECK(m.combatDeathPlace[c.type][c.place] == place + 1);
			CHECK(m.combatDeathAssignment[c.type][c.assignment] == assignment + 1);
			u->attachedBuilding = nullptr;
		}
	}

	TEST_CASE("defence snapshot counts warriors by place and enemy warriors at home")
	{
		glob2test::HeadlessGlobals globals;
		ThreeTeams w;
		w.world.addBuilding("swarm", 10, 10, 0, 0);
		w.world.addBuilding("swarm", 40, 10, 0, 1);
		auto *war = w.flag("warflag", 40, 20, 0);
		sampleAll(w.game());
		auto warrior = [&](int x, int y, int team, int speed, int strength)
		{
			auto *u = w.world.addUnit(WARRIOR, x, y, team);
			u->level[ATTACK_SPEED] = speed;
			u->level[ATTACK_STRENGTH] = strength;
			return u;
		};
		warrior(14, 14, 0, 1, 2);
		auto *hurt = warrior(15, 14, 0, 0, 0);
		hurt->medical = Unit::MED_DAMAGED;
		auto *flagged = warrior(41, 15, 0, 3, 1);
		flagged->attachedBuilding = war;
		flagged->activity = Unit::ACT_FLAG;
		auto *inside = warrior(30, 50, 0, 0, 1);
		inside->displacement = Unit::DIS_INSIDE;
		warrior(13, 15, 1, 2, 2); // enemy warrior at our home
		warrior(42, 16, 1, 1, 1); // enemy warrior at its own home
		warrior(13, 16, 2, 3, 3); // allied warrior at our home
		auto *dead = warrior(16, 14, 0, 4, 4);
		dead->isDead = true;
		const Uint32 checksum = w.game().checkSum();
		w.team(0)->stats.sampleDefence(w.team(0));
		const auto &m = w.team(0)->stats.measurements;
		CHECK(m.defenceTick == w.game().stepCounter);
		CHECK(m.warriors[M::HOME] == 2);
		CHECK(m.warriors[M::AWAY] == 1);
		CHECK(m.warriors[M::FIELD] == 1);
		CHECK(m.warriorLevels[M::HOME] == 3);
		CHECK(m.warriorLevels[M::AWAY] == 4);
		CHECK(m.warriorLevels[M::FIELD] == 1);
		CHECK(m.warriorsHurt == 1);
		CHECK(m.warriorsFlagged == 1);
		CHECK(m.warriorsInside == 1);
		CHECK(m.intruders == 1);
		CHECK(m.intruderLevels == 4);
		CHECK(w.game().checkSum() == checksum);
		flagged->attachedBuilding = nullptr;
		dead->isDead = false;
	}

	TEST_CASE("labour, combat and defence measurements survive a save [save-format]")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world(glob2test::GameOptions{
			.wDec = 6, .hDec = 6, .clearImmobile = true, .loadDefaultRace = true, .header = true});
		auto *swarm = world.addBuilding("swarm", 10, 10);
		swarm->materials[WHEAT] = swarm->type->maxMaterial[WHEAT];
		swarm->update();
		world.addBuilding("inn", 20, 10);
		for (int i = 0; i < 6; ++i)
			world.addUnit(i < 4 ? WORKER : WARRIOR, 12 + i, 20);
		for (int y = 30; y < 34; ++y)
			for (int x = 10; x < 20; ++x)
				world.game.map.setResource(x, y, WHEAT, 1);
		world.step(1100);
		const auto &stats = world.team->stats;
		REQUIRE(stats.measurementHistory.size() >= 2);
		CHECK(workerTicks(stats.measurements) > 0);
		CHECK(stats.measurements.defenceTick == 1024);

		auto *backend = new GAGCore::MemoryStreamBackend;
		GAGCore::BinaryOutputStream out(backend);
		world.game.save(&out, false, "labour statistics");
		out.flush();
		const auto bytes = backend->takeContents();
		GameGUI restored(false);
		GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(), bytes.size()));
		input.seekFromStart(0);
		REQUIRE(restored.game.load(&input));
		const auto &loaded = restored.game.teams[0]->stats;
		CHECK(loaded.labourCoverageStartTick == stats.labourCoverageStartTick);
		CHECK(loaded.measurementHistory == stats.measurementHistory);
		CHECK(std::equal(std::begin(loaded.measurements.labour), std::end(loaded.measurements.labour),
						 std::begin(stats.measurements.labour)));
		CHECK(std::equal(&loaded.measurements.filling[0][0], &loaded.measurements.filling[0][0] + 16,
						 &stats.measurements.filling[0][0]));
	}

	TEST_CASE("older saves load with labour coverage starting at the loaded tick [save-format]")
	{
		glob2test::HeadlessGlobals globals;
		const std::string bytes = glob2test::readFile(glob2test::inflated("team-stats/version129-muka-1100.game.gz"));
		GameGUI gui(false);
		GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(), bytes.size()));
		input.seekFromStart(0);
		REQUIRE(gui.game.load(&input));
		REQUIRE(gui.game.mapHeader.getVersionMinor() == 129);
		for (int t = 0; t < gui.game.teamsCount(); ++t)
		{
			CAPTURE(t);
			const auto &stats = gui.game.teams[t]->stats;
			// Packed history rows of the old, shorter record size read back intact.
			REQUIRE(stats.measurementHistory.size() == 3);
			for (unsigned i = 0; i < 3; ++i)
			{
				CHECK(stats.measurementHistory[i].tick == 512 * i);
				CHECK(workerTicks(stats.measurementHistory[i]) == 0);
			}
			CHECK(stats.measurementHistory[2].harvested[WHEAT] > 0);
			CHECK(stats.labourCoverageStartTick == stats.measurements.tick);
			CHECK(workerTicks(stats.measurements) == 0);
			// No defence snapshot yet: it is taken at the next sample.
			CHECK(stats.measurements.defenceTick < stats.labourCoverageStartTick);
		}
	}

	TEST_CASE("place lookup cost against a scan of every building slot [benchmark]")
	{
		glob2test::HeadlessGlobals globals;
		// Eight teams at war with each other, about a thousand buildings on 512x512.
		glob2test::HeadlessGame world(glob2test::GameOptions{
			.wDec = 9, .hDec = 9, .teams = 8, .clearImmobile = true, .loadDefaultRace = true});
		Game &game = world.game;
		for (int t = 0; t < 8; ++t)
			game.teams[t]->enemies = 0xFFu & ~game.teams[t]->me;
		std::mt19937 random(99);
		int placed = 0;
		const int inn = globalContainer->buildingsTypes.getTypeNum("inn", 0, false);
		const auto *bt = globalContainer->buildingsTypes.get(inn);
		for (int attempts = 0; placed < 1000 && attempts < 20000; ++attempts)
		{
			const int team = random() % 8;
			// Each team builds around its own colony centre.
			const int cx = 64 + (team % 4) * 128, cy = 128 + (team / 4) * 256;
			const int x = (cx + int(random() % 96) - 48) & 511, y = (cy + int(random() % 96) - 48) & 511;
			if (!game.map.isFreeForBuilding(x, y, bt->width, bt->height))
				continue;
			world.addBuilding("inn", x, y, 0, team);
			++placed;
		}
		REQUIRE(placed == 1000);
		sampleAll(game);
		// The original #343 attribution: wrapped distance to every building's centre.
		auto slotScan = [&](const Team *team, int x, int y)
		{
			int own = 1 << 30, enemy = 1 << 30;
			for (int t = 0; t < game.teamsCount(); ++t)
				for (int i = 0; i < Building::MAX_COUNT; ++i)
				{
					Building *b = game.teams[t]->myBuildings[i];
					if (!b || b->type->isVirtual)
						continue;
					const int d = game.map.warpDistMax(x, y, b->getMidX(), b->getMidY());
					(game.teams[t] == team ? own : enemy) = std::min(game.teams[t] == team ? own : enemy, d);
				}
			return own <= placeRadius ? M::HOME : enemy <= placeRadius ? M::AWAY : M::FIELD;
		};
		std::vector<std::pair<int, int>> points;
		for (int i = 0; i < 20000; ++i)
			points.emplace_back(random() % 512, random() % 512);
		auto time = [&](auto &&lookup)
		{
			int sink = 0;
			const auto start = std::chrono::steady_clock::now();
			for (const auto &[x, y] : points)
				sink += lookup(game.teams[0], x, y);
			const auto ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count();
			CHECK(sink >= 0);
			return ns / points.size();
		};
		const double ring = time([](const Team *t, int x, int y) { return TeamStats::placeOf(t, x, y); });
		const double scan = time(slotScan);
		MESSAGE("place lookup: coverage mask " << ring << " ns/call, slot scan " << scan << " ns/call");
	}
}
