// SPDX-License-Identifier: GPL-3.0-or-later
// Offense target-team telemetry (docs/ai/telemetry.md, "Offense target team"):
// the fields name the enemy actually targeted, survive saves, and never change
// what the simulation computes.
#include "EngineFixtures.h"
#include "AI.h"
#include "AITelemetryFields.h"
#include "Order.h"
#include "Player.h"
#include "AINumbi.h"
#include "ai/cortex/AICortex.h"
#include "ai/cortex/CortexObservation.h"
#include "ai/cortex/CortexPlacement.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <memory>
#include <vector>

namespace
{
// The tested AI controls team 0 against `enemies` passive local teams, one colony
// per quadrant of a discovered 64x64 torus: swarm, inn, workers, warriors, wheat.
struct World
{
	glob2test::HeadlessGame world;
	explicit World(AI::ImplementationID id, int enemies = 1, Uint32 seed = 713, int warriors = 4)
		: world(glob2test::GameOptions{.wDec = 6, .hDec = 6, .teams = 1 + enemies, .discovered = true,
									   .clearImmobile = true, .loadDefaultRace = true})
	{
		const int teams = 1 + enemies;
		GameHeader header;
		header.setNumberOfPlayers(teams);
		header.getBasePlayer(0) = BasePlayer(0, "tested AI", 0, BasePlayer::playerTypeFromImplementationID(id));
		for (int t = 1; t < teams; ++t)
			header.getBasePlayer(t) = BasePlayer(t, "opponent", t, BasePlayer::P_LOCAL);
		header.setRandomSeed(seed);
		world.game.setGameHeader(header, true);
		world.game.setWaitingOnMask(0);
		for (int team = 0; team < teams; ++team)
		{
			const int ox = (team & 1) * 32, oy = (team >> 1) * 32;
			world.game.teams[team]->startPosX = 4 + ox;
			world.game.teams[team]->startPosY = 4 + oy;
			world.game.teams[team]->startPosSet = Team::START_POS_FROM_UNIT;
			for (auto *b : {world.addBuilding("swarm", 4 + ox, 4 + oy, 0, team),
							world.addBuilding("inn", 10 + ox, 4 + oy, 0, team)})
			{
				b->materials[WHEAT] = b->type->maxMaterial[WHEAT];
				b->update();
				// Seen by everyone, so Cortex may rank enemy buildings as targets.
				b->seenByMask = ~0u;
			}
			for (int unit = 0; unit < 8 + warriors; ++unit)
				world.addUnit(unit < 8 ? WORKER : WARRIOR, 4 + ox + unit, 12 + oy, team);
			for (int y = 18 + oy; y < 24 + oy; ++y)
				for (int x = 4 + ox; x < 20 + ox; ++x)
					world.game.map.setResourceByIndex(x, y, WHEAT, 1);
		}
		world.game.map.setMapDiscovered();
		for (int team = 0; team < teams; ++team)
			world.game.teams[team]->stats.step(world.game.teams[team]);
	}
	Game &game() { return world.game; }
	AI &ai() { return *world.game.players[0]->ai; }
};

void tick(Game &game)
{
	auto order = game.players[0]->ai->getOrder(false);
	REQUIRE(order != nullptr);
	order->sender = 0;
	game.executeOrder(order, 0);
	game.syncStep(0);
}

std::vector<Uint32> state(Game &game)
{
	std::vector<Uint32> result, buildings, units;
	game.checkSum(&result, &buildings, &units, true);
	result.insert(result.end(), buildings.begin(), buildings.end());
	result.insert(result.end(), units.begin(), units.end());
	return result;
}

const AITelemetry::Value &field(AI &ai, unsigned index)
{
	return ai.telemetrySeries->current.values[index];
}

std::string saved(Game &game)
{
	auto *backend = new GAGCore::MemoryStreamBackend;
	GAGCore::BinaryOutputStream out(backend);
	game.save(&out, false, "target telemetry");
	out.flush();
	return backend->takeContents();
}

std::unique_ptr<GameGUI> loaded(const std::string &bytes)
{
	auto restored = std::make_unique<GameGUI>(false);
	GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(), bytes.size()));
	input.seekFromStart(0);
	REQUIRE(restored->game.load(&input));
	restored->game.setWaitingOnMask(0);
	return restored;
}

struct TargetField
{
	AI::ImplementationID id;
	unsigned index;
	const char *name;
	bool attacksQuickly; // reaches an enemy target in this small world within the soak
};
const TargetField targetFields[] = {
	{AI::NUMBI, AITrace::AI1::AINumbi_mayAttack_enemy_team, "AINumbi.mayAttack.enemy_team", false},
	{AI::WARRUSH, AITrace::AI3::AIWarrush_placeGuardAreas_last_team, "AIWarrush.placeGuardAreas.last_team", true},
	{AI::CORTEX, AITrace::AI6::offense_target_team, "offense.target_team", true},
	{AI::CABINO, AITrace::AI8::module_PrioritizedBuildingAttack_target_team,
	 "module.PrioritizedBuildingAttack.target_team", true},
};
} // namespace

TEST_SUITE("AITargetTelemetrySave")
{
	TEST_CASE("target fields are declared in schema version 2")
	{
		glob2test::HeadlessGlobals globals;
		for (const auto &f : targetFields)
		{
			const std::string name = f.name;
			CAPTURE(name);
			World w(f.id);
			const auto &series = *w.ai().telemetrySeries;
			REQUIRE(f.index < series.fields.size());
			CHECK(series.fields[f.index].name == f.name);
			CHECK(series.schemaVersion == 2);
		}
	}

	TEST_CASE("Cortex flag targets carry the team that owns each building")
	{
		glob2test::HeadlessGlobals globals;
		World w(AI::CORTEX, 3);
		Cortex::BuildCandidate out[Cortex::CORTEX_FLAG_TARGETS];
		Sint32 outTeam[Cortex::CORTEX_FLAG_TARGETS];
		const int count = Cortex::placeFlagTargets(&w.game(), w.game().teams[0], out, outTeam);
		REQUIRE(count > 0);
		std::vector<bool> seen(4, false);
		for (int i = 0; i < Cortex::CORTEX_FLAG_TARGETS; ++i)
		{
			CAPTURE(i);
			if (i >= count)
			{
				CHECK(outTeam[i] == -1);
				continue;
			}
			const Uint16 gid = w.game().map.getBuilding(out[i].x, out[i].y);
			REQUIRE(gid != NOGBID);
			CHECK(outTeam[i] == Building::GIDtoTeam(gid));
			CHECK(outTeam[i] != 0);
			seen[outTeam[i]] = true;
		}
		CHECK((seen[1] && seen[2] && seen[3]));
	}

	TEST_CASE("Cortex records its committed target until the offense stands down, across saves")
	{
		glob2test::HeadlessGlobals globals;
		World w(AI::CORTEX, 2);
		auto &ai = *static_cast<AICortex *>(w.ai().aiImplementation);
		const unsigned index = AITrace::AI6::offense_target_team;
		CHECK_FALSE(field(w.ai(), index).valid);

		auto obs = Cortex::makeEmptyObservation();
		obs.valid = 1;
		obs.tick = w.game().stepCounter;
		obs.flagTargets[3].valid = 1;
		obs.flagTargets[3].x = 4 + 32; // team 1's swarm
		obs.flagTargets[3].y = 4;
		obs.flagTargetTeam[3] = 1;
		ai.translateAction(Cortex::makeWarFlagAction(3, 4, 4, 0), obs);
		CHECK(field(w.ai(), index).valid);
		CHECK(Sint64(field(w.ai(), index).bits) == 1);

		// The value is saved with the series: a reload sees the commit.
		auto restored = loaded(saved(w.game()));
		CHECK(Sint64(restored->game.players[0]->ai->telemetrySeries->current.values[index].bits) == 1);

		// Every way the offense stands down clears the target.
		ai.translateActionClearFlags();
		CHECK(Sint64(field(w.ai(), index).bits) == -1);
		ai.translateAction(Cortex::makeWarFlagAction(3, 4, 4, 0), obs);
		CHECK(Sint64(field(w.ai(), index).bits) == 1);
		ai.translateAction(Cortex::makeWarFlagAction(-1, 4, 4, 0), obs);
		CHECK(Sint64(field(w.ai(), index).bits) == -1);
	}

	TEST_CASE("Numbi reports the highest-numbered enemy its attack searches")
	{
		glob2test::HeadlessGlobals globals;
		World w(AI::NUMBI, 3);
		auto &numbi = *static_cast<AINumbi *>(w.ai().aiImplementation);
		const unsigned index = AITrace::AI1::AINumbi_mayAttack_enemy_team;
		// Already attacking with warriors to spare: the next call searches for an enemy.
		numbi.attackPhase = 1;
		numbi.mayAttack(0, 0, 1);
		CHECK(Sint64(field(w.ai(), index).bits) == 3);
		w.game().teams[0]->enemies = 0;
		numbi.mayAttack(0, 0, 1);
		CHECK(Sint64(field(w.ai(), index).bits) == -1);
	}

	TEST_CASE("target telemetry names only real enemies and leaves the simulation unchanged")
	{
		glob2test::HeadlessGlobals globals;
		for (const auto &f : targetFields)
		{
			const std::string name = f.name;
			CAPTURE(name);
			// Two copies of one saved state; without a series every telemetry write
			// is a no-op, so the detached copy is the reference run.
            // Exercise targeting without depending on births or a famine to
            // reach Cortex's existing eight-warrior normal offense threshold.
            World world(f.id,1,713,f.id==AI::CORTEX?8:4);
			const auto bytes = saved(world.game());
			auto collectedGame = loaded(bytes), detachedGame = loaded(bytes);
			Game &collected = collectedGame->game, &detached = detachedGame->game;
			AI &ai = *collected.players[0]->ai;
			detached.players[0]->ai->aiImplementation->telemetry.series = nullptr;
			REQUIRE(state(collected) == state(detached));
			int firstTarget = -1;
			for (int t = 0; t < 6000; ++t)
			{
				// Exercise the targeting/telemetry boundary with visible enemies;
				// reaching them through a particular scouting strategy is separate.
				for (Game* game : {&collected, &detached})
					game->map.setMapDiscovered(32, 0, 32, 32, game->teams[0]->me);
				tick(collected);
				tick(detached);
				REQUIRE(state(collected) == state(detached));
				const auto &value = field(ai, f.index);
				if (value.valid)
				{
					const Sint64 team = Sint64(value.bits);
					REQUIRE((team == -1 || team == 1));
					if (team == 1 && firstTarget < 0)
						firstTarget = t;
				}
			}
			MESSAGE(name << " first targeted the enemy at tick " << firstTarget);
			if (f.attacksQuickly)
				CHECK(firstTarget >= 0);
		}
	}
}
