// SPDX-License-Identifier: GPL-3.0-or-later
#define SDL_MAIN_HANDLED
#ifdef main
#undef main
#endif
#include "GlobalContainer.h"
#include "Game.h"
#include "GameGUI.h"
#include "GameGUIKeyActions.h"
#include "MapEditKeyActions.h"
#include "FileManager.h"
#include "Version.h"
#include <SDL.h>
#include <fstream>
#include <string>
#include <utility>
#include <initializer_list>
#include <stdexcept>
#include "TextStream.h"
#include "Unit.h"
#include "Bullet.h"
#include "Sector.h"
#include "EndGameScreen.h"
#include "ReplayReader.h"
#include "OrderMessages.h"
#include "Order.h"
#include "Player.h"
#include "Utilities.h"
#include "AIImplementation.h"
#include <set>
#include "Toolkit.h"
#include "StringTable.h"
#include <SDL_image.h>
#include <filesystem>
#include "BinaryStream.h"
#include "StreamBackend.h"
#include <cstdio>
#include <cstdlib>
#include <memory>

GlobalContainer* globalContainer = nullptr;

static void require(bool ok, const char* message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static void compare(TeamStats& expected, TeamStats& actual)
{
	require(expected.measurements == actual.measurements,
			"new measurement totals and snapshots survive loading");
	require(expected.coverageStartTick == actual.coverageStartTick &&
				expected.extendedCoverageStartTick == actual.extendedCoverageStartTick &&
				expected.coverageBuildingTick == actual.coverageBuildingTick &&
				expected.coverageBuildingGeneration == actual.coverageBuildingGeneration &&
				expected.coverageBuildings == actual.coverageBuildings &&
				expected.measurementHistory == actual.measurementHistory,
			"timestamped measurement coverage and history survive loading");
	const auto &a = *expected.getLatestStat();
	const auto& b = *actual.getLatestStat();
    require(a.totalUnit == b.totalUnit && a.totalHP == b.totalHP,
            "latest population and health survive loading and sampling");
    require(a.needFood == b.needFood && a.needFoodCritical == b.needFoodCritical &&
            a.needFoodNoInns == b.needFoodNoInns && a.needHeal == b.needHeal &&
            a.needNothing == b.needNothing, "Numbi medical-demand snapshot is unchanged");
    require(a.totalFree == b.totalFree && a.totalNeeded == b.totalNeeded,
            "smoothed labor totals are unchanged");
    for (int type = 0; type < NB_UNIT_TYPE; ++type)
        require(expected.getTotalUnits(type) == actual.getTotalUnits(type) &&
                expected.getFreeUnits(type) == actual.getFreeUnits(type), "per-type population and smoothing match");
    for (int level = 0; level < NB_UNIT_LEVELS; ++level)
        require(expected.getWorkersLevel(level) == actual.getWorkersLevel(level) &&
                a.totalNeededPerLevel[level] == b.totalNeededPerLevel[level], "per-level labor matches");
    require(expected.getWorkersBalance() == actual.getWorkersBalance(), "labor balance matches");
    const auto& historyA = expected.getEndOfGameStats();
    const auto& historyB = actual.getEndOfGameStats();
    require(historyA.size() == historyB.size(), "loading does not append an end-game sample");
    for (size_t i = 0; i < historyA.size(); ++i)
        for (int field = 0; field < EndOfGameStat::TYPE_NB_STATS; ++field)
            require(historyA[i].value[field] == historyB[i].value[field], "end-game history matches");
}

static void sample(Game& game, unsigned tick)
{
    auto* unit = game.teams[0]->myUnits[0];
    unit->hungry = (tick % 11 < 5) ? 0 : unit->trigHungry + 100;
    unit->medical = (tick % 17 < 8) ? Unit::MED_DAMAGED : Unit::MED_FREE;
    game.stepCounter = tick;
    game.teams[0]->stats.step(game.teams[0]);
}

static std::unique_ptr<GameGUI> roundTrip(Game& game)
{
    auto* bytes = new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream writer(bytes);
    game.save(&writer, false, "team statistics regression");
    auto* copy = new GAGCore::MemoryStreamBackend(*bytes);
    copy->seekFromStart(0);
    GAGCore::BinaryInputStream reader(copy);
    auto loaded = std::make_unique<GameGUI>();
    require(loaded->game.load(&reader), "binary game save loads");
    return loaded;
}

class LocatedOutput : public GAGCore::BinaryOutputStream
{
public:
  size_t statsPosition = 0, smoothingPosition = 0, coveragePosition = 0,
		 measurementCountPosition = 0;
  void writeUint32(Uint32 value, const std::string name) override
  {
	  if (name == "coverageStartTick")
		  coveragePosition = getPosition();
	  if (name == "measurementCount")
		  measurementCountPosition = getPosition();
	  BinaryOutputStream::writeUint32(value, name);
  }
  explicit LocatedOutput(GAGCore::StreamBackend *backend) : BinaryOutputStream(backend) {}
  void writeSint32(Sint32 value, const std::string name) override
  {
	  if (name == "statsIndex")
		  statsPosition = getPosition();
	  if (name == "smoothedIndex")
		  smoothingPosition = getPosition();
	  BinaryOutputStream::writeSint32(value, name);
  }
};

static void malformedStats(Game& game)
{
    auto* bytes = new GAGCore::MemoryStreamBackend;
    LocatedOutput writer(bytes);
    game.save(&writer, false, "invalid statistics regression");
    require(writer.statsPosition && writer.smoothingPosition, "new statistics indices are in the save");
    bytes->seekFromEnd(0);
    const std::string original(bytes->getBuffer(), bytes->getPosition());
    const std::pair<size_t, int> cases[] = {{writer.statsPosition, -1}, {writer.statsPosition, 128},
        {writer.smoothingPosition, -1}, {writer.smoothingPosition, 32}};
    for (const auto& entry : cases)
    {
        auto* corrupt = new GAGCore::MemoryStreamBackend(original.data(), original.size());
        GAGCore::BinaryOutputStream patch(corrupt);
        patch.seekFromStart(entry.first);
        patch.writeSint32(entry.second, "invalidIndex");
        auto* copy = new GAGCore::MemoryStreamBackend(*corrupt);
        copy->seekFromStart(0);
        GAGCore::BinaryInputStream reader(copy);
        GameGUI loaded;
        bool rejected = false;
        try { loaded.game.load(&reader); }
        catch (const std::runtime_error& error)
        { rejected = std::string(error.what()) == "Invalid team statistics sampling index"; }
        require(rejected, "invalid sampling index reaches the statistics validator");
    }
	for (size_t position : {writer.coveragePosition, writer.measurementCountPosition})
	{
		auto *corrupt = new GAGCore::MemoryStreamBackend(original.data(), original.size());
		GAGCore::BinaryOutputStream patch(corrupt);
		patch.seekFromStart(position);
		patch.writeUint32(0xffffffff, "invalidMeasurement");
		auto *copy = new GAGCore::MemoryStreamBackend(*corrupt);
		copy->seekFromStart(0);
		GAGCore::BinaryInputStream reader(copy);
		GameGUI loaded;
		bool rejected = false;
		try
		{
			loaded.game.load(&reader);
		}
		catch (const std::runtime_error &error)
		{
			rejected = std::string(error.what()) == "Invalid gameplay statistics coverage";
		}
		require(rejected, "malformed measurement coverage/count is rejected");
	}
	for (const size_t length :
		 {writer.statsPosition + 2, writer.smoothingPosition + 2, writer.smoothingPosition + 6,
		  writer.coveragePosition + 2, writer.measurementCountPosition + 2,
		  writer.measurementCountPosition + 12})
	{
		GAGCore::BinaryInputStream reader(new GAGCore::MemoryStreamBackend(original.data(), length));
        reader.seekFromStart(0);
        GameGUI loaded;
        bool rejected = false;
        try { loaded.game.load(&reader); }
        catch (const std::runtime_error& error)
        { rejected = std::string(error.what()).find("Incomplete binary field:") == 0; }
        require(rejected, "truncated statistics reach the checked field reader");
	}
}

static void textRoundTrip()
{
    // Exercise the new named fields independently of the legacy end-game text labels.
    GameGUI gui;
    Game& game = gui.game;
    game.map.setSize(5, 5, GRASS);
    game.map.setGame(&game);
    game.addTeam();
    game.teams[0]->race.loadDefault();
    require(game.addUnit(5, 5, 0, WORKER, 0, 0, 0, 0) != nullptr, "text fixture worker exists");
    for (unsigned tick = 1; tick <= 77; ++tick) sample(game, tick);
    auto* bytes = new GAGCore::MemoryStreamBackend;
    GAGCore::TextOutputStream writer(bytes);
    game.teams[0]->save(&writer);
    writer.flush();
    auto* copy = new GAGCore::MemoryStreamBackend(*bytes);
    copy->seekFromStart(0);
    GAGCore::TextInputStream reader(copy);
    GameGUI loaded;
    loaded.game.map.setSize(5, 5, GRASS);
    loaded.game.map.setGame(&loaded.game);
    loaded.game.addTeam();
    require(loaded.game.teams[0]->load(&reader, &globalContainer->buildingsTypes, VERSION_MINOR), "text team save loads");
    compare(game.teams[0]->stats, loaded.game.teams[0]->stats);
    const unsigned start = game.stepCounter;
    for (unsigned i = 1; i <= 65; ++i)
    {
        sample(game, start + i);
        sample(loaded.game, start + i);
        compare(game.teams[0]->stats, loaded.game.teams[0]->stats);
    }
}

using Measurements = GameplayMeasurements;

struct TeamStatsMeasurementFixture
{
	static void allowConversion(Building *b) { b->canNotConvertUnitTimer = 0; }
	static void fire(Building *b)
	{
		b->shootingCooldown = 0;
		b->bullets = 1;
		b->turretStep(0);
	}
	static void medical(Unit *u) { u->handleMedical(); }
	static void activity(Unit *u) { u->handleActivity(); }
	static void magic(Unit *u) { u->handleMagic(); }
	static void displacement(Unit *u) { u->handleDisplacement(); }
	static void clear(Unit *u) { u->tryClaimClearingAreaForHarvesting(); }
	static void partial(Unit *u) { u->applyPartialInsideBenefit(); }
	static void ammunition(Building *b) { b->convertStoneToBullet(); }

	GameGUI gui;
	Game &game = gui.game;
	TeamStatsMeasurementFixture()
	{
		game.map.setSize(5, 5, GRASS);
		game.map.setGame(&game);
		for (int t = 0; t < 2; ++t)
		{
			game.addTeam(t);
			game.teams[t]->race.loadDefault();
		}
	}
	Building *building(const char *name, int x = 8, int y = 8, int team = 0, bool site = false,
					   int level = 0)
	{
		int type = globalContainer->buildingsTypes.getTypeNum(name, level, site);
		require(type >= 0, "scenario building type exists");
		auto *b = game.addBuilding(x, y, type, team);
		require(b != nullptr, "scenario building exists");
		game.map.setBuilding(x, y, b->type->width, b->type->height, b->gid);
		return b;
	}
	Unit *unit(int type = WORKER, int x = 20, int y = 20, int team = 0)
	{
		auto *u = game.addUnit(x, y, team, type, 0, 0, 0, 0);
		require(u != nullptr, "scenario unit exists");
		return u;
	}
	void inside(Unit *u, Building *b, int purpose, int timeout = 0)
	{
		u->clearOccupiedMapSlot();
		u->posX = b->getMidX();
		u->posY = b->getMidY();
		u->attachedBuilding = b;
		u->setTargetBuilding(b);
		u->activity = Unit::ACT_UPGRADING;
		u->displacement = Unit::DIS_INSIDE;
		u->movement = Unit::MOV_INSIDE;
		u->destinationPurpose = purpose;
		u->insideTimeout = timeout;
		b->unitsInside.push_back(u);
	}
};

static void measurementScenarios()
{
	{
		TeamStatsMeasurementFixture w;
		auto *u = w.unit(WORKER,20,20);
		u->hp = u->performance[HP] / 4;
		u->hungry = Unit::HUNGRY_MAX / 2;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
				if (dx || dy) w.game.map.setGroundUnit(20+dx,20+dy,u->gid);
		auto &stats = w.game.teams[0]->stats;
		stats.sampleTraps(w.game.teams[0]);
		require(stats.measurements.trappedUnits[0][WORKER] == 0 &&
			stats.measurements.trappedUnits[1][WORKER] == 1,
			"temporary occupancy is separate from structural blockage");
		require(stats.measurements.lowHP[0][WORKER] == 1 &&
			stats.measurements.lowFood[1][WORKER] == 1 &&
			stats.measurements.lowFood[0][WORKER] == 0,
			"health and food cutoffs are inclusive and independent");
	}
	{
		TeamStatsMeasurementFixture w;
		Building *b = w.building("inn",8,8);
		auto &stats = w.game.teams[0]->stats;
		stats.sampleTraps(w.game.teams[0]);
		w.game.map.rebuildGrowthCoverage();
		for (int distance : {8,9})
		{
			const int x = b->posX + b->type->width - 1 + distance;
			require(w.game.map.incResource(x,8,WHEAT,0), "fixture resource seeded");
			w.game.map.recordNaturalGrowth(x,8,WHEAT,NO_RES_TYPE,0);
		}
		require(stats.measurements.growthGlobal[0][WHEAT] == 2 &&
			stats.measurements.growthAmount[0][WHEAT] == 1 &&
			stats.measurements.growthAmount[1][WHEAT] == 2 &&
			stats.measurements.growthAmount[2][WHEAT] == 2,
			"growth distance boundary is inclusive and global events remain exact");
		w.game.stepCounter = 77;
		auto loaded = roundTrip(w.game);
		require(stats.coverageBuildings == loaded->game.teams[0]->stats.coverageBuildings &&
			stats.coverageBuildingGeneration == loaded->game.teams[0]->stats.coverageBuildingGeneration,
			"mid-interval building coverage anchors survive save/load");
		const int nextX = b->posX + b->type->width + 8;
		for (Game *g : {&w.game, &loaded->game})
		{
			g->map.rebuildGrowthCoverage();
			require(g->map.incResource(nextX,8,WHEAT,0), "continuation resource seeded");
			g->map.recordNaturalGrowth(nextX,8,WHEAT,NO_RES_TYPE,0);
		}
		require(stats.measurements == loaded->game.teams[0]->stats.measurements,
			"mid-interval growth telemetry continues identically after load");
	}
	{
		TeamStatsMeasurementFixture w;
		auto &stats = w.game.teams[0]->stats;
		const TeamStats::CoverageBuilding anchor{8,8,2,2};
		stats.coverageBuildings = {anchor,anchor};
		++stats.coverageBuildingGeneration;
		w.game.map.rebuildGrowthCoverage();
		const size_t tile = size_t(8) * w.game.map.getW() + 8;
		require((w.game.map.growthCoverage[tile] & 1) != 0,
			"overlapping anchors mark their shared tile");
		stats.coverageBuildings.pop_back();
		++stats.coverageBuildingGeneration;
		w.game.map.rebuildGrowthCoverage();
		require((w.game.map.growthCoverage[tile] & 1) != 0,
			"removing one overlapping anchor preserves coverage");
		stats.coverageBuildings.clear();
		++stats.coverageBuildingGeneration;
		w.game.map.rebuildGrowthCoverage();
		require((w.game.map.growthCoverage[tile] & 1) == 0,
			"removing the last anchor clears coverage");
	}
	{
		TeamStatsMeasurementFixture w;
		auto *swarm = w.building("swarm");
		auto *t = w.game.teams[0];
		for (int i = 0; i < NB_UNIT_TYPE; ++i)
			w.unit(i, 20 + i, 20);
		require(TeamStats::graphValue(t->stats.measurements, 0) == 0,
				"starting/editor units are not births");
		require(TeamStats::graphValue(t->stats.measurements, 6) == 0,
				"starting/editor buildings are not completions");
		swarm->ratio[0] = 1;
		swarm->ratio[1] = swarm->ratio[2] = 0;
		swarm->resources[WHEAT] = swarm->type->resourceForOneUnit;
		swarm->productionTimeout = -1;
		swarm->swarmStep();
		require(t->stats.measurements.births[WORKER] == 1, "successful swarm birth");
		require(t->stats.measurements.consumed[Measurements::SPAWNING][WHEAT] ==
					Uint64(swarm->type->resourceForOneUnit),
				"spawning wheat cost");
		for (int y = 0; y < 32; ++y)
			for (int x = 0; x < 32; ++x)
				w.game.map.setGroundUnit(x, y, 0);
		swarm->resources[WHEAT] = swarm->type->resourceForOneUnit;
		swarm->productionTimeout = -1;
		swarm->swarmStep();
		require(t->stats.measurements.births[WORKER] == 1, "blocked birth produces no event");
		require(swarm->resources[WHEAT] == swarm->type->resourceForOneUnit,
				"blocked birth consumes nothing");
	}
	{
		TeamStatsMeasurementFixture w;
		auto *inn = w.building("inn");
		auto &m = w.game.teams[0]->stats.measurements;
		inn->resources[WHEAT] = inn->type->maxResource[WHEAT] - 1;
		inn->addResourceIntoBuilding(WHEAT);
		inn->addResourceIntoBuilding(WHEAT);
		require(m.delivered[WHEAT] == 1, "delivery uses accepted capacity-clamped amount");
		inn->resources[CHERRY] = 1;
		inn->eatOnce(nullptr);
		require(m.meals == 1 && m.consumed[Measurements::MEAL][WHEAT] == 1 &&
					m.consumed[Measurements::MEAL][CHERRY] == 1,
				"meal wheat and fruit consumed");
		auto *market = w.building("market", 14, 8);
		w.building("market", 20, 8);
		market->resources[CHERRY] = 7;
		w.game.teams[0]->stats.refreshMeasurements(w.game.teams[0]);
		require(m.stock[CHERRY] == 7, "shared storage counted once across two markets");
		market->removeResourceFromBuilding(CHERRY);
		require(m.withdrawn[CHERRY] ==
					Uint64(std::min(7, market->type->multiplierResource[CHERRY])),
				"market withdrawal in resource units");
		require(m.withdrawn[CHERRY] == m.transferredOut[CHERRY] && m.harvested[CHERRY] == 0,
				"withdrawal is transfer, not harvest");
		auto *tower = w.building("defencetower", 8, 16);
		tower->resources[STONE] = 1;
		tower->bullets = 0;
		TeamStatsMeasurementFixture::ammunition(tower);
		require(m.consumed[Measurements::AMMUNITION][STONE] == 1,
				"ammunition counts stone conversion");
	}
	{
		TeamStatsMeasurementFixture w;
		auto *worker = w.unit();
		auto *inn = w.building("inn");
		auto &m = w.game.teams[0]->stats.measurements;
		w.game.map.incResource(21, 20, WHEAT, 0);
		worker->attachedBuilding = inn;
		worker->setTargetBuilding(inn);
		worker->activity = Unit::ACT_FILLING;
		worker->displacement = Unit::DIS_HARVESTING;
		worker->movement = Unit::MOV_HARVESTING;
		worker->destinationPurpose = WHEAT;
		worker->dx = 1;
		worker->dy = 0;
		TeamStatsMeasurementFixture::displacement(worker);
		require(m.harvested[WHEAT] == 1 && worker->carriedResource == WHEAT,
				"harvest completion counts a load");
		worker->standardRandomActivity();
		worker->carriedResource = -1;
		w.game.map.incResource(21, 20, WOOD, 0);
		worker->movement = Unit::MOV_HARVESTING;
		worker->medical = Unit::MED_FREE;
		const int resourceType = w.game.map.getResource(21, 20).type;
		TeamStatsMeasurementFixture::clear(worker);
		require(m.cleared[resourceType] == 1, "clearing completion counts an operation");
	}
	{
		TeamStatsMeasurementFixture w;
		auto *attacker = w.unit(WARRIOR, 20, 20, 0);
		auto *victim = w.unit(WORKER, 21, 20, 1);
		attacker->action = ATTACK_SPEED;
		attacker->speed = 1;
		attacker->delta = UNIT_ATTACK_HIT_DELTA;
		attacker->dx = 1;
		attacker->dy = 0;
		victim->hp = 1;
		attacker->performance[ATTACK_STRENGTH] = 1000;
		attacker->syncStep();
		require(w.game.teams[0]->stats.measurements.damageDealt[Measurements::MELEE]
															   [Measurements::UNIT] == 1,
				"melee overkill is capped to HP removed");
		require(victim->diagnosticDeathCause == Measurements::COMBAT,
				"first lethal transition attributed to combat");
		attacker->delta = UNIT_ATTACK_HIT_DELTA;
		attacker->syncStep();
		require(w.game.teams[0]->stats.measurements.damageDealt[Measurements::MELEE]
															   [Measurements::UNIT] == 1,
				"repeated damage before death removes zero HP");
		victim->hungry = 0;
		TeamStatsMeasurementFixture::medical(victim);
		TeamStatsMeasurementFixture::medical(victim);
		require(w.game.teams[1]->stats.measurements.deaths[WORKER][Measurements::COMBAT] == 1,
				"death counted once and starvation cannot overwrite cause");
	}
	{
		TeamStatsMeasurementFixture w;
		auto *explorer = w.unit(EXPLORER, 20, 20, 0);
		auto *victim = w.unit(WORKER, 21, 20, 1);
		victim->hp = 3;
		explorer->performance[MAGIC_ATTACK_GROUND] = 1000;
		explorer->magicActionTimeout = 0;
		TeamStatsMeasurementFixture::magic(explorer);
		require(w.game.teams[0]->stats.measurements.damageDealt[Measurements::MAGIC]
															   [Measurements::UNIT] == 3,
				"magic damage excludes overkill");
		require(w.game.teams[1]->stats.measurements.damageReceived[Measurements::MAGIC]
																  [Measurements::UNIT] == 3,
				"magic received damage");
	}
	{
		TeamStatsMeasurementFixture w;
		auto *victim = w.unit(WORKER, 20, 20, 1);
		auto *bullet = new Bullet(0, 0, 1, 1, 0, 1000, 20, 20, 0, 0, 0, 0);
		bullet->sourceTeam = 0;
		auto *sector = w.game.map.getSector(20, 20);
		sector->bullets.push_front(bullet);
		victim->hp = 2;
		sector->step();
		require(w.game.teams[0]->stats.measurements.damageDealt[Measurements::TOWER]
															   [Measurements::UNIT] == 2,
				"tower projectile source attribution");
		require(victim->diagnosticDeathCause == Measurements::COMBAT, "tower lethal attribution");
		auto *target = w.building("inn", 8, 8, 1);
		target->hp = 2;
		bullet = new Bullet(0, 0, 1, 1, 0, 1000, 8, 8, 0, 0, 0, 0);
		bullet->sourceTeam = 0;
		sector->bullets.push_front(bullet);
		sector->step();
		require(w.game.teams[0]->stats.measurements.damageDealt[Measurements::TOWER]
															   [Measurements::BUILDING] == 2,
				"tower building overkill excluded");
		require(w.game.teams[1]->stats.measurements.removed[Measurements::DESTROYED]
														   [target->type->shortTypeNum]
														   [target->getLongLevel()] == 1,
				"combat building removal");
		target->kill();
		require(w.game.teams[1]->stats.measurements.removed[Measurements::OTHER]
														   [target->type->shortTypeNum]
														   [target->getLongLevel()] == 0,
				"repeated kill does not count removal twice");
	}
	for (int cause : {Measurements::STARVATION, Measurements::CLEARING, Measurements::UNKNOWN})
	{
		TeamStatsMeasurementFixture w;
		auto *u = w.unit();
		u->hp = 0;
		if (cause == Measurements::STARVATION)
			u->hungry = 0;
		else if (cause == Measurements::CLEARING)
		{
			w.game.map.incResource(21, 20, WOOD, 0);
			u->dx = 1;
			u->dy = 0;
			u->movement = Unit::MOV_HARVESTING;
			u->medical = Unit::MED_FREE;
			TeamStatsMeasurementFixture::clear(u);
		}
		else
			u->hp = -1;
		TeamStatsMeasurementFixture::medical(u);
		require(w.game.teams[0]->stats.measurements.deaths[WORKER][cause] == 1,
				"noncombat death cause");
	}
	{
		TeamStatsMeasurementFixture w;
		auto *hospital = w.building("hospital");
		auto *patient = w.unit();
		w.inside(patient, hospital, HEAL, -hospital->type->timeToHealUnit / 2);
		patient->hp = patient->performance[HP] - 20;
		TeamStatsMeasurementFixture::partial(patient);
		auto &m = w.game.teams[0]->stats.measurements;
		require(m.hpRestored == 10 && m.healingVisits == 0,
				"partial healing credits actual restored HP, not a completed visit");
		patient->insideTimeout = 0;
		TeamStatsMeasurementFixture::displacement(patient);
		require(m.hpRestored == 20 && m.healingVisits == 1,
				"completed healing credits remaining HP");
		TeamStatsMeasurementFixture::displacement(patient);
		require(m.healingVisits == 1, "exit polling does not count another healing");
	}
	{
		TeamStatsMeasurementFixture w;
		auto *school = w.building("school");
		auto *student = w.unit();
		w.inside(student, school, BUILD);
		auto &m = w.game.teams[0]->stats.measurements;
		TeamStatsMeasurementFixture::displacement(student);
		require(m.trainingVisits[WORKER] == 1 && m.abilityGains[WORKER][BUILD] == 1 &&
					m.abilityGains[WORKER][HARVEST] == 1,
				"one training visit upgrades both worker abilities");
		TeamStatsMeasurementFixture::displacement(student);
		require(m.trainingVisits[WORKER] == 1, "training completion counted once");
	}
	for (int kind : {Measurements::NEW_BUILDING, Measurements::UPGRADED, Measurements::REPAIRED})
	{
		TeamStatsMeasurementFixture w;
		auto *site = w.building("inn", 8, 8, 0, true, kind == Measurements::UPGRADED ? 1 : 0);
		site->constructionResultState = kind == Measurements::NEW_BUILDING ? Building::NEW_BUILDING
										: kind == Measurements::UPGRADED   ? Building::UPGRADE
																		   : Building::REPAIR;
		for (int r = 0; r < MAX_RESOURCES; ++r)
			site->resources[r] = site->type->maxResource[r];
		int level = site->type->level, shortType = site->type->shortTypeNum;
		const int wheatCost = site->type->maxResource[WHEAT];
		site->update();
		site->update();
		auto &m = w.game.teams[0]->stats.measurements;
		require(m.completed[kind][shortType][level] == 1,
				"construction/upgrade/repair completion counted once");
		if (kind != Measurements::REPAIRED)
			require(
				m.consumed[kind == Measurements::UPGRADED ? Measurements::UPGRADE
														  : Measurements::CONSTRUCTION][WHEAT] ==
					Uint64(wheatCost),
				"completed construction resource purpose");
	}
	{
		TeamStatsMeasurementFixture w;
		auto *inn = w.building("inn", 8, 8, 1);
		inn->resources[WHEAT] = 10;
		inn->resources[CHERRY] = 10;
		inn->updateCallLists();
		TeamStatsMeasurementFixture::allowConversion(inn);
		w.game.teams[1]->sharedVisionFood |= w.game.teams[0]->me;
		w.game.teams[1]->allies &= ~w.game.teams[0]->me;
		auto *u = w.unit(EXPLORER, 12, 8);
		u->hungry = u->trigHungry;
		u->medical = Unit::MED_HUNGRY;
		u->needToRecheckMedical = true;
		TeamStatsMeasurementFixture::activity(u);
		require(u->owner == w.game.teams[1], "foreign inn converts hungry explorer");
		require(w.game.teams[0]->stats.measurements.conversionsOut[EXPLORER] == 1 &&
					w.game.teams[1]->stats.measurements.conversionsIn[EXPLORER] == 1,
				"successful conversion tracked separately");
		require(TeamStats::graphValue(w.game.teams[1]->stats.measurements, 0) == 0,
				"conversion is not a birth");
	}
	{
		TeamStatsMeasurementFixture w;
		auto *inn = w.building("inn");
		auto *u = w.unit();
		w.inside(u, inn, FEED, -10);
		for (int y = 0; y < 32; ++y)
			for (int x = 0; x < 32; ++x)
				w.game.map.setGroundUnit(x, y, u->gid);
		inn->kill(Measurements::DESTROYED);
		require(u->isDead &&
					w.game.teams[0]->stats.measurements.deaths[WORKER][Measurements::TRAPPED] == 1,
				"trapped occupant counted once");
	}
	{
		TeamStatsMeasurementFixture w;
		auto *site = w.building("inn", 8, 8, 0, true);
		int type = site->type->shortTypeNum, level = site->getLongLevel();
		site->launchDelete();
		w.game.teams[0]->syncStep();
		require(
			w.game.teams[0]->stats.measurements.removed[Measurements::DEMOLISHED][type][level] == 1,
			"site cancellation counted as demolition");
		require(TeamStats::graphValue(w.game.teams[0]->stats.measurements, 6) == 0,
				"cancelled site is not a completion");
	}
	{
		TeamStatsMeasurementFixture w;
		auto *attacker = w.unit(WARRIOR, 7, 8, 0);
		auto *target = w.building("inn", 8, 8, 1);
		target->hp = 1;
		attacker->action = ATTACK_SPEED;
		attacker->speed = 1;
		attacker->delta = UNIT_ATTACK_HIT_DELTA;
		attacker->dx = 1;
		attacker->dy = 0;
		attacker->performance[ATTACK_STRENGTH] = 1000;
		attacker->syncStep();
		require(w.game.teams[0]->stats.measurements.damageDealt[Measurements::MELEE]
															   [Measurements::BUILDING] == 1,
				"melee building damage is capped");
	}
	{
		TeamStatsMeasurementFixture w;
		auto *tower = w.building("defencetower");
		auto *target = w.unit(WORKER, 11, 8, 1);
		target->speed = 1;
		target->delta = 0;
		TeamStatsMeasurementFixture::fire(tower);
		require(w.game.teams[0]->stats.measurements.shots[Measurements::TOWER] == 1,
				"real turret firing counts one launched projectile");
	}
	{
		TeamStatsMeasurementFixture w;
		auto *barracks = w.building("barracks");
		auto *student = w.unit(WARRIOR);
		require(barracks->type->upgradeInParallel, "barracks trains abilities in parallel");
		w.inside(student, barracks, ATTACK_SPEED);
		TeamStatsMeasurementFixture::displacement(student);
		const auto &m = w.game.teams[0]->stats.measurements;
		require(m.trainingVisits[WARRIOR] == 1 && m.abilityGains[WARRIOR][ATTACK_SPEED] == 1 &&
					m.abilityGains[WARRIOR][ATTACK_STRENGTH] == 1,
				"parallel training counts one visit and each ability gain");
	}
	{
		TeamStatsMeasurementFixture w;
		auto *site = w.building("inn", 8, 8, 0, true);
		site->constructionResultState = Building::REPAIR;
		site->resources[WOOD] = 0;
		site->addResourceIntoBuilding(WOOD);
		require(w.game.teams[0]->stats.measurements.repairDelivered[WOOD] ==
					Uint64(site->type->multiplierResource[WOOD]),
				"repair deliveries recorded separately");
	}
	{
		TeamStatsMeasurementFixture w;
		w.game.gameHeader.setHungerDisabled(true);
		auto *u = w.unit();
		u->hungry = 0;
		const int hp = u->hp;
		TeamStatsMeasurementFixture::medical(u);
		require(u->hp == hp && !u->isDead, "no hunger rule preserves health");
		require(w.game.teams[0]->stats.measurements.deaths[WORKER][Measurements::STARVATION] == 0,
				"no hunger produces no starvation event");
	}
	{
		TeamStatsMeasurementFixture w;
		w.game.gameHeader.setUnitUpgradesDisabled(true);
		auto *b = w.building("barracks");
		auto *u = w.unit(WARRIOR);
		const int level = u->level[ATTACK_SPEED];
		w.inside(u, b, ATTACK_SPEED);
		TeamStatsMeasurementFixture::displacement(u);
		const auto &m = w.game.teams[0]->stats.measurements;
		require(u->level[ATTACK_SPEED] == level && m.trainingVisits[WARRIOR] == 1 &&
				m.abilityGains[WARRIOR][ATTACK_SPEED] == 0,
				"disabled upgrades still complete a visit without a level gain");
	}
	{
		TeamStatsMeasurementFixture w;
		w.game.gameHeader.setInstantConstructionEnabled(true);
		auto *b = w.building("inn", 8, 8, 0, true);
		b->update();
		const auto &m = w.game.teams[0]->stats.measurements;
		require(!b->type->isBuildingSite, "instant site completes");
		for (int r = 0; r < MAX_RESOURCES; ++r)
			require(m.consumed[Measurements::CONSTRUCTION][r] == 0,
					"instant construction consumes no undelivered resources");
	}
	std::puts("Gameplay measurement engine scenarios passed");
}

static void measurementAttributionFields()
{
	Bullet bullet(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);
	bullet.sourceTeam = 1;
	auto *storage = new GAGCore::MemoryStreamBackend;
	GAGCore::BinaryOutputStream writer(storage);
	bullet.save(&writer);
	const std::string bytes(storage->getBuffer(), storage->getPosition());
	// All pre-105 projectile fields are an unchanged prefix.
	GAGCore::BinaryInputStream oldReader(
		new GAGCore::MemoryStreamBackend(bytes.data(), bytes.size() - 4));
	oldReader.seekFromStart(0);
	Bullet old(&oldReader, 100);
	require(old.sourceTeam == -1 && old.shootDamage == 6,
			"old projectiles retain unknown attribution");
	for (bool truncated : {false, true})
	{
		auto *input =
			new GAGCore::MemoryStreamBackend(bytes.data(), bytes.size() - (truncated ? 2 : 0));
		if (!truncated)
		{
			GAGCore::BinaryOutputStream patch(input);
			patch.seekFromStart(bytes.size() - 4);
			patch.writeSint32(Team::MAX_COUNT, "invalidSource");
			input = new GAGCore::MemoryStreamBackend(*input);
		}
		input->seekFromStart(0);
		GAGCore::BinaryInputStream reader(input);
		bool rejected = false;
		try
		{
			Bullet invalid(&reader, VERSION_MINOR);
		}
		catch (const std::runtime_error &)
		{
			rejected = true;
		}
		require(rejected, "invalid/truncated projectile source rejected");
	}
}

static void measurementReplayBoundaries()
{
	require(REPLAY_MINIMUM_VERSION_MINOR == 99 && NET_PROTOCOL_VERSION == 33 &&
				YOG_MIN_CLIENT_NET_PROTOCOL_VERSION == 33,
			"diagnostic save fields preserve replay floor and network gates");
	for (int version : {98, 99, 100, 101, 102, 103, 104, 105, 106, 107, VERSION_MINOR, VERSION_MINOR+1})
	{
		auto *bytes = new GAGCore::MemoryStreamBackend;
		GAGCore::BinaryOutputStream writer(bytes);
		writer.writeUint16(VERSION_MAJOR, "versionMajor");
		writer.writeUint16(version, "versionMinor");
		writer.writeUint32(0, "replayStepCounter");
		NetSendOrder message(std::make_shared<NullOrder>());
		message.encodeData(&writer);
		auto *copy = new GAGCore::MemoryStreamBackend(*bytes);
		copy->seekFromStart(0);
		ReplayReader reader;
		require(reader.loadReplay(new GAGCore::BinaryInputStream(copy), false) ==
					(version >= 99 && version <= VERSION_MINOR),
				"replay acceptance boundary is unchanged except current writer version");
	}
}

static void measurementContinuation()
{
	TeamStatsMeasurementFixture w;
	auto *u = w.unit(WORKER, 20, 20, 1);
	u->hp = -2;
	u->diagnosticDeathCause = Measurements::CLEARING;
	auto *bullet = new Bullet(0, 0, 1, 1, 10, 1000, 20, 20, 0, 0, 0, 0);
	bullet->sourceTeam = 0;
	w.game.map.getSector(20, 20)->bullets.push_front(bullet);
	w.game.stepCounter = 711;
	for (int t = 0; t < 2; ++t)
		w.game.teams[t]->stats.initializeMeasurements(700);
	auto &stats = w.game.teams[0]->stats;
	stats.measurements.births[WORKER] = (Uint64(1) << 40) + 17;
	stats.measurements.abilityGains[EXPLORER][MAGIC_ATTACK_GROUND] = (Uint64(1) << 50) + 1;
	auto loaded = roundTrip(w.game);
	require(loaded->game.teams[0]->stats.measurements == stats.measurements,
			"large counters survive save/load");
	auto *restored = loaded->game.teams[1]->myUnits[0];
	require(restored->diagnosticDeathCause == Measurements::CLEARING,
			"pending lethal cause survives loading");
	auto *restoredSector = loaded->game.map.getSector(20, 20);
	require(restoredSector->bullets.front()->sourceTeam == 0,
			"in-flight projectile source survives loading");
	for (int i = 0; i < 12; ++i)
	{
		w.game.map.getSector(20, 20)->step();
		restoredSector->step();
	}
	TeamStatsMeasurementFixture::medical(u);
	TeamStatsMeasurementFixture::medical(restored);
	for (int t = 0; t < 2; ++t)
		require(w.game.teams[t]->stats.measurements == loaded->game.teams[t]->stats.measurements,
				"projectile and pending death continuation totals match");
}

static void aiTelemetryContinuation(const char *path)
{
	FILE *file = std::fopen(path, "rb");
	require(file != nullptr, "AI continuation fixture opens");
	GAGCore::BinaryInputStream input(new GAGCore::FileStreamBackend(file));
	GameGUI gui;
	require(gui.game.load(&input), "all-AI initial state loads");
	auto step = [](Game &g)
	{
		for (int p = 0; p < g.gameHeader.getNumberOfPlayers(); ++p)
			if (g.players[p] && g.players[p]->ai)
			{
				auto order = g.players[p]->ai->getOrder(false);
				order->sender = p;
				g.executeOrder(order, p);
			}
		g.syncStep(0);
	};
	auto checks = [](Game &g)
	{
		std::vector<Uint32> c, b, u;
		g.checkSum(&c, &b, &u, true);
		c.erase(c.begin());
		c.insert(c.end(), b.begin(), b.end());
		c.insert(c.end(), u.begin(), u.end());
		return c;
	};
	for (int t = 0; t < 500; ++t)
		step(gui.game);
	auto loaded = roundTrip(gui.game);
	// Legacy controllers deliberately rebuild/reset some unsaved internal state.
    // Compare two continuations of the same saved state, not different AI states.
    auto reference = roundTrip(gui.game);
    const auto random = syncRandEngine();
	std::vector<std::vector<Uint32>> expected;
	std::vector<std::vector<AITelemetry::Sample>> samples;
	for (int t = 0; t < 700; ++t)
	{
		step(reference->game);
		expected.push_back(checks(reference->game));
		std::vector<AITelemetry::Sample> row;
		for (int p = 0; p < reference->game.gameHeader.getNumberOfPlayers(); ++p)
			if (reference->game.players[p]->ai)
				row.push_back(reference->game.players[p]->ai->telemetrySeries->current);
		samples.push_back(std::move(row));
	}
	syncRandEngine() = random;
	for (int t = 0; t < 700; ++t)
	{
		step(loaded->game);
		if (checks(loaded->game) != expected[t])
		{
			std::fprintf(stderr, "AI continuation simulation differs at tick %u\n",
						 loaded->game.stepCounter);
			std::exit(1);
		}
		unsigned ai = 0;
		for (int p = 0; p < loaded->game.gameHeader.getNumberOfPlayers(); ++p)
			if (loaded->game.players[p]->ai)
			{
				const auto &actual = loaded->game.players[p]->ai->telemetrySeries->current;
				const auto &want = samples[t][ai++];
				if (actual != want)
				{
					const auto &series = *loaded->game.players[p]->ai->telemetrySeries;
					for (unsigned f = 0; f < actual.values.size(); ++f)
						if (actual.values[f] != want.values[f])
							std::fprintf(stderr, "AI %d telemetry %s differs at tick %u\n",
										 series.implementation, series.fields[f].name.c_str(),
										 actual.tick);
					std::exit(1);
				}
			}
	}
	for (int p = 0; p < reference->game.gameHeader.getNumberOfPlayers(); ++p)
		if (reference->game.players[p]->ai)
			require(reference->game.players[p]->ai->telemetrySeries->history ==
						loaded->game.players[p]->ai->telemetrySeries->history,
					"all-AI sampled history continues across saves");
	std::puts("PASS all-AI repeated load continuation: 700 ticks of simulation components, current telemetry and "
			  "retained history");
}

static void aiTelemetryScenarios()
{
	using namespace AITelemetry;
	// Every controller exposes an independent, self-describing schema.
	TeamStatsMeasurementFixture w;
	auto &g = w.game;
	g.gameHeader.setNumberOfPlayers(2);
	for (int p = 0; p < 2; ++p)
	{
		g.gameHeader.getBasePlayer(p) =
			BasePlayer(p, "same AI", 0, BasePlayer::playerTypeFromImplementationID(AI::NONE));
		g.players[p] = new Player(p, "same AI", g.teams[0],
								  BasePlayer::playerTypeFromImplementationID(AI::NONE));
		g.players[p]->ai->bindTelemetry();
	}
	for (int i = 0; i < AI::SIZE; ++i)
	{
		// Runtime settings belong to a controller implementation on master.
		g.gameHeader.setAIConfig(0, "");
		AI controller(static_cast<AI::ImplementationID>(i), g.players[0]);
		require(controller.aiImplementation->telemetrySchema() == schema(i),
				"every AI publishes its schema through the standard interface");
		require(schema(i).size() >= Specific, "common fields exist for every AI");
		std::set<std::string> names;
		for (const auto &f : schema(i))
			require(names.insert(f.name).second, "schema names are unique");
	}
	g.gameHeader.setAIConfig(0, "");
	auto a = g.players[0]->ai->telemetrySeries, b = g.players[1]->ai->telemetrySeries;
	require(a != b && a->player == 0 && b->player == 1,
			"same AI and team retain distinct player series");
	auto checksum = g.checkSum(nullptr, nullptr, nullptr);
	g.players[0]->ai->getOrder(true);
	require(a->current.values[Polls].bits == 0,
			"paused polls do not execute or count an implementation");
	g.players[0]->ai->getOrder(false);
	require(a->current.values[Polls].bits == 1 && a->current.values[NullOrders].bits == 1 &&
				b->current.values[Polls].bits == 0,
			"inactive AI returns are counted once for the correct player");
	require(g.checkSum(nullptr, nullptr, nullptr) == checksum,
			"diagnostic collection is excluded from checksums");
	g.players[0]->ai->aiImplementation->telemetry.setUnsigned(Orders, (Uint64(1) << 55) + 3);
	g.stepCounter = 512;
	capture(g.teams[0], true, false);
	capture(g.teams[0], true, false);
	require(a->history.size() == 1 && a->history[0].tick == 512, "one sample at its actual tick");
	g.stepCounter = 711;
	auto loaded = roundTrip(g);
	const auto &restored = loaded->game.teams[0]->stats.aiTelemetry;
	require(restored.size() == 2 && restored[0]->current == a->current &&
				restored[0]->history == a->history,
			"AI state and large counters survive game saves between samples");
	require(loaded->game.players[0]->ai->telemetrySeries == restored[0],
			"loaded AI rebinds the preserved series");
	class ScalarAIOutput : public GAGCore::BinaryOutputStream
	{
	  public:
		using BinaryOutputStream::BinaryOutputStream;
	};
	const auto saveRecords = [&](bool scalar)
	{
		auto *memory = new GAGCore::MemoryStreamBackend();
		std::unique_ptr<GAGCore::BinaryOutputStream> stream;
		if (scalar)
			stream = std::make_unique<ScalarAIOutput>(memory);
		else
			stream = std::make_unique<GAGCore::BinaryOutputStream>(memory);
		AITelemetry::save(stream.get(), restored);
		stream->flush();
		return std::string(memory->getBuffer(), memory->getPosition());
	};
	require(saveRecords(false) == saveRecords(true),
			"packed AI samples preserve scalar bytes across chunks and large counters");
	const auto before = a->current.values;
	globalContainer->replaying = true;
	g.stepCounter = 1024;
	capture(g.teams[0], true, false);
	require(a->history.size() == 1 && !a->current.available && a->current.values == before,
			"replay does not poll AI, fabricate history, or advance diagnostic values");
	globalContainer->replaying = false;
	g.players[0]->makeItAI(AI::NONE);
	g.players[0]->ai->bindTelemetry();
	require(!a->active && g.players[0]->ai->telemetrySeries->generation == 1,
			"controller replacement starts a new generation");
	g.players[1]->setTeam(g.teams[1]);
	g.players[1]->ai->bindTelemetry();
	require(!b->active && g.teams[1]->stats.aiTelemetry.size() == 1,
			"team reassignment closes the former series");

	// Self-described numeric fields survive without requiring a live controller/schema.
	std::vector<std::shared_ptr<Series>> records;
	auto r = std::make_shared<Series>();
	r->fields = schema(0);
	r->fields.push_back({"signed", "value", "signed round trip", Signed, Gauge});
	r->fields.push_back({"real", "value", "double round trip", Real, Gauge});
	r->current.tick = 99;
	r->current.available = true;
	r->current.values.resize(r->fields.size());
	Sink sink;
	sink.series = r.get();
	sink.tick = 99;
	sink.set(Specific, -12345678901234LL);
	sink.setReal(Specific + 1, 0.125);
	records.push_back(r);
	auto *bytes = new GAGCore::MemoryStreamBackend;
	GAGCore::BinaryOutputStream writer(bytes);
	save(&writer, records);
	const std::string original(bytes->getBuffer(), bytes->getPosition());
	for (int truncate : {0, 1, 17, 100})
	{
		auto *input = new GAGCore::MemoryStreamBackend(original.data(), original.size() - truncate);
		input->seekFromStart(0);
		GAGCore::BinaryInputStream reader(input);
		std::vector<std::shared_ptr<Series>> copy;
		bool rejected = false;
		try
		{
			load(&reader, copy);
		}
		catch (const std::runtime_error &)
		{
			rejected = true;
		}
		if (truncate)
			require(rejected, "truncated AI telemetry rejected");
		else
			require(copy.size() == 1 && copy[0]->fields == r->fields &&
						copy[0]->current == r->current,
					"signed and real fields preserve exact saved representation");
	}
	std::puts("AI telemetry schemas, identity, sampling, checksums, replay availability, save "
			  "continuation and numeric corruption tests passed");
}

static void measurementScreenshots(const std::string &directory)
{
	std::filesystem::create_directories(directory);
	GameGUI gui;
	gui.init();
	auto &game = gui.game;
	game.map.setSize(5, 5, GRASS);
	game.map.setGame(&game);
	game.gameHeader.setNumberOfPlayers(Team::MAX_COUNT);
	for (int t = 0; t < Team::MAX_COUNT; ++t)
	{
		game.addTeam(t);
		game.gameHeader.getBasePlayer(t) =
			BasePlayer(t, "Long colony name " + std::to_string(t + 1), t, BasePlayer::P_LOCAL);
		auto &stats = game.teams[t]->stats;
		stats.initializeMeasurements(t % 2 ? 1024 : 0);
		for (unsigned tick = 0; tick <= 4096; tick += 512)
		{
			game.stepCounter = tick;
			stats.measurements.births[WORKER] = (Uint64(1) << 40) + (t + 1) * tick;
			stats.measurements.harvested[WHEAT] = (t + 1) * tick;
			stats.measurements.completed[Measurements::NEW_BUILDING][0][0] = (t + 1) * tick / 512;
			stats.measurements.damageDealt[Measurements::MELEE][Measurements::UNIT] =
				(t + 1) * tick;
			stats.step(game.teams[t]);
		}
		if (t % 2)
			stats.measurementHistory.erase(stats.measurementHistory.begin(),
										   stats.measurementHistory.begin() + 2);
		for (auto &sample : stats.measurementHistory)
		{
			sample.growthGlobal[1][WHEAT] = Uint64(t+1) * sample.tick;
			sample.growthAmount[1][WHEAT] = Uint64(t+1) * sample.tick / 2;
			sample.trappedUnits[1][WORKER] = t+1;
			sample.lowFood[0][WORKER] = t+2;
		}
		stats.measurements.growthGlobal[1][WHEAT] = Uint64(t+1) * 4096;
		stats.measurements.growthAmount[1][WHEAT] = Uint64(t+1) * 2048;
		stats.measurements.trappedUnits[1][WORKER] = t+1;
		stats.measurements.lowFood[0][WORKER] = t+2;
	}
	gui.localTeamNo = 0;
	gui.localPlayer = 0;
	gui.adjustLocalTeam();
	for (const auto size : {std::pair{640, 480}, std::pair{1024, 768}})
	{
		globalContainer->gfx->setRes(size.first, size.second, 0);
		const std::string suffix =
			std::to_string(size.first) + "x" + std::to_string(size.second) + ".png";
		{
			struct CaptureScreen : EndGameScreen
			{
				explicit CaptureScreen(GameGUI *gui) : EndGameScreen(gui)
				{
					gfx = globalContainer->gfx;
					dispatchInit();
				}
			} screen(&gui);
			for (int page = 0; page < 6; ++page)
			{
				screen.dispatchPaint();
				require(IMG_SavePNG(globalContainer->gfx->getSDLSurface(),
									(directory + "/graphs-" + std::to_string(page) + "-" + suffix)
										.c_str()) == 0,
						"save graph screenshot");
				screen.onAction(nullptr, GAGGUI::BUTTON_SHORTCUT, EndGameScreen::STAT_PAGE, 0);
			}
		}
		globalContainer->gfx->drawFilledRect(0, 0, size.first, size.second, 0, 0, 32);
		globalContainer->gfx->drawString(size.first - 140, 195, globalContainer->littleFont,
										 Toolkit::getStringTable()->getString("[Stats page two]"));
		game.teams[0]->stats.drawMeasurements(size.first - 144, 211);
		require(IMG_SavePNG(globalContainer->gfx->getSDLSurface(),
							(directory + "/live-" + suffix).c_str()) == 0,
			"save live panel screenshot");
		globalContainer->gfx->drawFilledRect(0, 0, size.first, size.second, 0, 0, 32);
		globalContainer->gfx->drawString(size.first - 140, 195, globalContainer->littleFont,
									 Toolkit::getStringTable()->getString("[Stats page three]"));
		game.teams[0]->stats.drawExpandedMeasurements(size.first - 144, 211);
		require(IMG_SavePNG(globalContainer->gfx->getSDLSurface(),
							(directory + "/live-expanded-" + suffix).c_str()) == 0,
			"save expanded live panel screenshot");
	}
}

int main(int argc, char** argv)
{
    SDL_SetMainReady();
    require(argc == 3 || argc == 5, "usage: harness PROFILE ROOT [--write-fixture FILE | --legacy FILE]");
    require(std::string(argv[1]).find("glob2-save-test-") == 0, "disposable profile required");
    GlobalContainer globals(argv[1]);
    globals.fileManager->addDir(argv[2]);
    globalContainer = &globals;
	if (argc == 5 && std::string(argv[3]) == "--screenshots")
	{
		globals.runNoX = false;
		globals.settings.screenWidth = 640;
		globals.settings.screenHeight = 480;
		globals.settings.screenFlags = 0;
		globals.settings.mute = 1;
		globals.settings.rememberUnit = false;
		globals.load();
		measurementScreenshots(argv[4]);
		return 0;
	}
	globals.runNoX = true;
    globals.settings.rememberUnit = false;
    globals.buildingsTypes.init();
    IntBuildingType::init();
    GameGUIKeyActions::init();
    MapEditKeyActions::init();
	if (argc == 5 && std::string(argv[3]) == "--ai-continuation")
	{
		aiTelemetryContinuation(argv[4]);
		return 0;
	}
	if (argc == 5 && std::string(argv[3]) == "--legacy")
    {
        FILE* file = std::fopen(argv[4], "rb");
        require(file != nullptr, "open legacy fixture");
        GAGCore::BinaryInputStream reader(new GAGCore::FileStreamBackend(file));
        GameGUI restored;
        require(restored.game.load(&reader), "legacy save loads");
        Game& game = restored.game;
		for (int t = 0; t < game.mapHeader.getNumberOfTeams(); ++t)
		{
			require(game.teams[t]->stats.coverageStartTick == game.stepCounter,
					"old measurements start at loaded tick");
			require(game.teams[t]->stats.measurementHistory.empty(),
					"old new-history is unavailable, not zero samples");
			require(TeamStats::graphValue(game.teams[t]->stats.measurements, 0) == 0,
					"loading does not count births");
		}
		require(game.mapHeader.getVersionMinor() <= 88, "fixture is a genuine legacy format");
		std::printf("LEGACY version=%d teams=%d\n", game.mapHeader.getVersionMinor(), game.mapHeader.getNumberOfTeams());
        for (unsigned step = 0; step < 65; ++step)
        {
            for (int t = 0; t < game.mapHeader.getNumberOfTeams(); ++t)
            {
                auto& stats = game.teams[t]->stats;
                const auto& stat = *stats.getLatestStat();
                std::printf("%u %d %d %d %d %d %d %d %d %zu\n", step, t,
                    stat.totalUnit, stat.totalHP, stat.needFood, stat.needFoodCritical,
                    stat.needHeal, stat.totalFree, stat.totalNeeded, stats.getEndOfGameStats().size());
                stats.step(game.teams[t]);
            }
            ++game.stepCounter;
        }
        return 0;
    }
    GameGUI gui;
    Game& game = gui.game;
    game.map.setSize(5, 5, GRASS);
    game.map.setGame(&game);
    game.addTeam();
    game.teams[0]->race.loadDefault();
    require(game.addUnit(5, 5, 0, WORKER, 0, 0, 0, 0) != nullptr, "fixture worker exists");
    // Fill and wrap the 128-snapshot ring before testing every smoothing phase.
    for (unsigned tick = 0; tick < 4096; ++tick)
        sample(game, tick);
    if (argc == 5 && std::string(argv[3]) == "--write-fixture")
    {
        for (unsigned tick = 4096; tick < 4107; ++tick) sample(game, tick);
        auto* bytes = new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream writer(bytes);
        game.save(&writer, false, "team statistics legacy fixture");
        bytes->seekFromEnd(0);
        std::ofstream file(argv[4], std::ios::binary);
        file.write(bytes->getBuffer(), bytes->getPosition());
        file.close();
        require(!file.fail(), "write saved fixture");
        std::printf("Wrote save format %d fixture\n", VERSION_MINOR);
        return 0;
    }
    for (unsigned phase = 0; phase < 32; ++phase)
    {
        auto loaded = roundTrip(game);
        compare(game.teams[0]->stats, loaded->game.teams[0]->stats);
        // Repeated loads must not advance the sampling position either.
        auto reloaded = roundTrip(loaded->game);
        compare(game.teams[0]->stats, reloaded->game.teams[0]->stats);
        const unsigned start = game.stepCounter;
        for (unsigned delta = 1; delta <= 65; ++delta)
        {
            sample(game, start + delta);
            sample(loaded->game, start + delta);
            sample(reloaded->game, start + delta);
            compare(game.teams[0]->stats, loaded->game.teams[0]->stats);
            compare(game.teams[0]->stats, reloaded->game.teams[0]->stats);
        }
    }
	aiTelemetryScenarios();
	measurementScenarios();
	measurementContinuation();
	std::printf("Measurement snapshot memory: %zu bytes per team/sample\n", sizeof(GameplayMeasurements));
	measurementAttributionFields();
	measurementReplayBoundaries();
	malformedStats(game);
	textRoundTrip();
    std::puts("Team statistics save regressions passed: 32 sampling phases, ring wrap, repeated loads, text streams and corruption controls");
    return 0;
}
