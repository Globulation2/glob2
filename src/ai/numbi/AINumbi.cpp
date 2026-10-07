// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

// AINumbi is split across several translation units by responsibility:
//   AINumbi.cpp          — lifecycle (ctors/init/load/save), getOrder dispatch, countUnits
//   AINumbiPlacement.cpp — building-site scanning and emplacement search
//   AINumbiEconomy.cpp   — food estimation, swarms, building adjustment, expansion
//   AINumbiMilitary.cpp  — attack management and level upgrades
// All share the single AINumbi declaration in AINumbi.h.

#include <Stream.h>
#include <sstream>

#include "AINumbi.h"
#include "AIStateSerialization.h"
#include "FileFormatVersions.h"
#include "Game.h"
#include "Order.h"
#include "Player.h"
#include "Utilities.h"
#include "Unit.h"

using std::shared_ptr;

AINumbi::AINumbi(Player *player)
{
	init(player);
}

AINumbi::AINumbi(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	bool goodLoad=load(stream, player, versionMinor);
	if (!goodLoad) throw std::runtime_error("Invalid saved AI");
}

void AINumbi::init(Player *player)
{
	timer=0;
	phase=0;
	phaseTime=AI_NUMBI_PHASE_TIME_DEFAULT_TICKS;
	attackPhase=0;
	criticalWarriors=AI_NUMBI_CRITICAL_WARRIORS_DEFAULT;
	criticalTime=AI_NUMBI_CRITICAL_TIME_DEFAULT_TICKS;
	attackTimer=0;
	mainBuilding.fill(0);

	assert(player);

	this->player=player;
	this->team=player->team;
	this->game=player->game;
	this->map=player->map;

	assert(this->team);
	assert(this->game);
	assert(this->map);
}

AINumbi::~AINumbi()
{
}

bool AINumbi::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	init(player);
	GAGCore::BinaryInputStream::CheckedReads checked(stream);

	stream->readEnterSection("AINumbi");

	phase            = stream->readSint32("phase");
	attackPhase      = stream->readSint32("attackPhase");
	phaseTime        = stream->readSint32("phaseTime");
	criticalWarriors= stream->readSint32("critticalWarriors");
	criticalTime    = stream->readSint32("critticalTime");
	attackTimer      = stream->readSint32("attackTimer");

	// Pre-catalog saves persisted placement anchors in the old family order.
	// This mapping imports those anchors only; planning never classifies by ID.
	static constexpr Intent legacyAnchors[] = {
		Intent::ProduceWorker, Intent::Feed, Intent::Heal, Intent::TrainWalk,
		Intent::TrainSwim, Intent::TrainAttackStrength, Intent::TrainConstruction,
		Intent::ProjectileDefense, Intent::AttractExplorers, Intent::AttractWarriors,
		Intent::ClearResources, Intent::Count, Intent::ExchangeResources};
	const bool semantic = versionMinor >= FILE_FORMAT_VERSION_BUILDING_CATALOG;
	const unsigned count = semantic ? mainBuilding.size() : std::size(legacyAnchors);
	for (unsigned i = 0; i < count; ++i)
	{
		const int anchor = stream->readSint32(("mainBuilding[" + std::to_string(i) + "]").c_str());
		if (anchor < 0 || anchor >= Building::MAX_COUNT) return false;
		const Intent intent = semantic ? static_cast<Intent>(i) : legacyAnchors[i];
		if (intent != Intent::Count) mainBuilding[static_cast<unsigned>(intent)] = anchor;
	}

	if (versionMinor >= AI_NUMBI_SAVE_FORMAT_CONTINUATION)
		timer = AIStateSerialization::readSint32(stream, "timer");
	stream->readLeaveSection();

	if (versionMinor >= AI_NUMBI_SAVE_FORMAT_CONTINUATION &&
		(phaseTime < 0 || timer < 0 || timer > phaseTime)) return false;
	return stream->isValid();
}

void AINumbi::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("AINumbi");

	stream->writeSint32(phase, "phase");
	stream->writeSint32(attackPhase, "attackPhase");
	stream->writeSint32(phaseTime, "phaseTime");
	stream->writeSint32(criticalWarriors, "critticalWarriors");
	stream->writeSint32(criticalTime, "critticalTime");
	stream->writeSint32(attackTimer, "attackTimer");

	for (unsigned bi=0; bi<mainBuilding.size(); bi++)
	{
		std::ostringstream oss;
		oss << "mainBuilding[" << bi << "]";
		stream->writeSint32(mainBuilding[bi], oss.str().c_str());
	}

	stream->writeSint32(timer, "timer");
	stream->writeLeaveSection();
}


std::shared_ptr<Order>AINumbi::getOrder(void)
{
	timer++;

	if (timer>phaseTime)
	{
		timer-=timer;
		phase++;
		//printf("AI: new phase %d.\n", phase);
	}
	if (phase==0)
	{
		// rush for food building, explore for room.
		switch (timer&AI_NUMBI_DECISION_SLOT_MASK)
		{
			case 0:
				return swarmsForWorkers(AI_NUMBI_PHASE0_SWARM_MIN, AI_NUMBI_PHASE0_SWARM_FACTOR, AI_NUMBI_PHASE0_SWARM_WORKERS, AI_NUMBI_PHASE0_SWARM_EXPLORER, AI_NUMBI_PHASE0_SWARM_WARRIOR);
			case 1:
				return adjustBuildings(AI_NUMBI_PHASE0_INN_NUMBERS, AI_NUMBI_PHASE0_INN_NUMBERS_INC, AI_NUMBI_PHASE0_INN_WORKERS, Intent::Feed);
		}
	}
	else if (phase==1)
	{
		// rush for food building
		switch (timer&AI_NUMBI_DECISION_SLOT_MASK)
		{
			case 0:
				return swarmsForWorkers(AI_NUMBI_PHASE1_SWARM_MIN, AI_NUMBI_PHASE1_SWARM_FACTOR, AI_NUMBI_PHASE1_SWARM_WORKERS, AI_NUMBI_PHASE1_SWARM_EXPLORER, AI_NUMBI_PHASE1_SWARM_WARRIOR);
			case 1:
				return adjustBuildings(AI_NUMBI_PHASE1_INN_NUMBERS, AI_NUMBI_PHASE1_INN_NUMBERS_INC, AI_NUMBI_PHASE1_INN_WORKERS, Intent::Feed);
		}
	}
	else if (phase<AI_NUMBI_MID_GAME_PHASE)
	{
		// mainly produce units, improve health and science if possible
		switch (timer&AI_NUMBI_DECISION_SLOT_MASK)
		{
			case 0:
				return swarmsForWorkers(AI_NUMBI_PHASE2_SWARM_MIN, AI_NUMBI_PHASE2_SWARM_FACTOR, AI_NUMBI_PHASE2_SWARM_WORKERS, AI_NUMBI_PHASE2_SWARM_EXPLORER, AI_NUMBI_PHASE2_SWARM_WARRIOR);
			case 1:
				return adjustBuildings(AI_NUMBI_PHASE2_INN_NUMBERS, AI_NUMBI_PHASE2_INN_NUMBERS_INC, AI_NUMBI_PHASE2_INN_WORKERS, Intent::Feed);
			case 2:
				return adjustBuildings(AI_NUMBI_PHASE2_HEAL_NUMBERS, AI_NUMBI_PHASE2_HEAL_NUMBERS_INC, AI_NUMBI_PHASE2_HEAL_WORKERS, Intent::Heal);
			case 3:
				return adjustBuildings(AI_NUMBI_PHASE2_SCIENCE_NUMBERS, AI_NUMBI_PHASE2_SCIENCE_NUMBERS_INC, AI_NUMBI_PHASE2_SCIENCE_WORKERS, Intent::TrainConstruction);
			case 4:
				return adjustBuildings(AI_NUMBI_PHASE2_RACETRACK_NUMBERS, AI_NUMBI_PHASE2_RACETRACK_NUMBERS_INC, AI_NUMBI_PHASE2_RACETRACK_WORKERS, Intent::TrainWalk);
			case 5:
				return adjustBuildings(AI_NUMBI_PHASE2_BARRACKS_NUMBERS, AI_NUMBI_PHASE2_BARRACKS_NUMBERS_INC, AI_NUMBI_PHASE2_BARRACKS_WORKERS, Intent::TrainAttackStrength);
			case 6:
				return adjustBuildings(AI_NUMBI_PHASE2_DEFENSE_NUMBERS, AI_NUMBI_PHASE2_DEFENSE_NUMBERS_INC, AI_NUMBI_PHASE2_DEFENSE_WORKERS, Intent::ProjectileDefense);
		}
	}
	else if (phase<AI_NUMBI_LATE_MID_PHASE)
	{
		// mainly produce units, improve health and science if possible
		switch (timer&AI_NUMBI_DECISION_SLOT_MASK)
		{
			case 0:
				return swarmsForWorkers(AI_NUMBI_PHASE4_SWARM_MIN, AI_NUMBI_PHASE4_SWARM_FACTOR, AI_NUMBI_PHASE4_SWARM_WORKERS, AI_NUMBI_PHASE4_SWARM_EXPLORER, AI_NUMBI_PHASE4_SWARM_WARRIOR);
			case 1:
				return adjustBuildings(AI_NUMBI_PHASE4_INN_NUMBERS, AI_NUMBI_PHASE4_INN_NUMBERS_INC, AI_NUMBI_PHASE4_INN_WORKERS, Intent::Feed);
			case 2:
				return adjustBuildings(AI_NUMBI_PHASE4_HEAL_NUMBERS, AI_NUMBI_PHASE4_HEAL_NUMBERS_INC, AI_NUMBI_PHASE4_HEAL_WORKERS, Intent::Heal);
			case 3:
				return adjustBuildings(AI_NUMBI_PHASE4_SCIENCE_NUMBERS, AI_NUMBI_PHASE4_SCIENCE_NUMBERS_INC, AI_NUMBI_PHASE4_SCIENCE_WORKERS, Intent::TrainConstruction);
			case 4:
				return adjustBuildings(AI_NUMBI_PHASE4_DEFENSE_NUMBERS, AI_NUMBI_PHASE4_DEFENSE_NUMBERS_INC, AI_NUMBI_PHASE4_DEFENSE_WORKERS, Intent::ProjectileDefense);
			case 5:
				return mayUpgrade(AI_NUMBI_PHASE4_UPGRADE_PTRIGGER, AI_NUMBI_PHASE4_UPGRADE_NTRIGGER);
		}
	}
	else if (phase<AI_NUMBI_SCIENCE_PHASE)
	{
		// improve science now
		switch (timer&AI_NUMBI_DECISION_SLOT_MASK)
		{
			case 0:
				return swarmsForWorkers(AI_NUMBI_PHASE6_SWARM_MIN, AI_NUMBI_PHASE6_SWARM_FACTOR, AI_NUMBI_PHASE6_SWARM_WORKERS, AI_NUMBI_PHASE6_SWARM_EXPLORER, AI_NUMBI_PHASE6_SWARM_WARRIOR);
			case 1:
				return adjustBuildings(AI_NUMBI_PHASE6_INN_NUMBERS, AI_NUMBI_PHASE6_INN_NUMBERS_INC, AI_NUMBI_PHASE6_INN_WORKERS, Intent::Feed);
			case 2:
				return adjustBuildings(AI_NUMBI_PHASE6_HEAL_NUMBERS, AI_NUMBI_PHASE6_HEAL_NUMBERS_INC, AI_NUMBI_PHASE6_HEAL_WORKERS, Intent::Heal);
			case 3:
				return adjustBuildings(AI_NUMBI_PHASE6_SCIENCE_NUMBERS, AI_NUMBI_PHASE6_SCIENCE_NUMBERS_INC, AI_NUMBI_PHASE6_SCIENCE_WORKERS, Intent::TrainConstruction);
			case 4:
				return mayUpgrade(AI_NUMBI_PHASE6_UPGRADE_PTRIGGER, AI_NUMBI_PHASE6_UPGRADE_NTRIGGER);
		}
	}
	else if (phase<AI_NUMBI_DEFEND_PHASE)
	{
		// produce good units, defend too.
		switch (timer&AI_NUMBI_DECISION_SLOT_MASK)
		{
			case 0:
				return swarmsForWorkers(AI_NUMBI_PHASE8_SWARM_MIN, AI_NUMBI_PHASE8_SWARM_FACTOR, AI_NUMBI_PHASE8_SWARM_WORKERS, AI_NUMBI_PHASE8_SWARM_EXPLORER, AI_NUMBI_PHASE8_SWARM_WARRIOR);
			case 1:
				return adjustBuildings(AI_NUMBI_PHASE8_INN_NUMBERS, AI_NUMBI_PHASE8_INN_NUMBERS_INC, AI_NUMBI_PHASE8_INN_WORKERS, Intent::Feed);
			case 2:
				return adjustBuildings(AI_NUMBI_PHASE8_HEAL_NUMBERS, AI_NUMBI_PHASE8_HEAL_NUMBERS_INC, AI_NUMBI_PHASE8_HEAL_WORKERS, Intent::Heal);
			case 3:
				return adjustBuildings(AI_NUMBI_PHASE8_SCIENCE_NUMBERS, AI_NUMBI_PHASE8_SCIENCE_NUMBERS_INC, AI_NUMBI_PHASE8_SCIENCE_WORKERS, Intent::TrainConstruction);
			case 4:
				return adjustBuildings(AI_NUMBI_PHASE8_RACETRACK_NUMBERS, AI_NUMBI_PHASE8_RACETRACK_NUMBERS_INC, AI_NUMBI_PHASE8_RACETRACK_WORKERS, Intent::TrainWalk);
			case 5:
				return adjustBuildings(AI_NUMBI_PHASE8_DEFENSE_NUMBERS, AI_NUMBI_PHASE8_DEFENSE_NUMBERS_INC, AI_NUMBI_PHASE8_DEFENSE_WORKERS, Intent::ProjectileDefense);
			case 6:
				return adjustBuildings(AI_NUMBI_PHASE8_BARRACKS_NUMBERS, AI_NUMBI_PHASE8_BARRACKS_NUMBERS_INC, AI_NUMBI_PHASE8_BARRACKS_WORKERS, Intent::TrainAttackStrength);
			case 7:
				return checkoutExpands(AI_NUMBI_PHASE8_EXPAND_NUMBERS, AI_NUMBI_PHASE8_EXPAND_WORKERS);
			case 8:
				return mayUpgrade(AI_NUMBI_PHASE8_UPGRADE_PTRIGGER, AI_NUMBI_PHASE8_UPGRADE_NTRIGGER);
		}
	}
	else
	{
		// produce warriors
		switch (timer&AI_NUMBI_DECISION_SLOT_MASK)
		{
			case 0:
				return swarmsForWorkers(AI_NUMBI_PHASE10_SWARM_MIN, AI_NUMBI_PHASE10_SWARM_FACTOR, AI_NUMBI_PHASE10_SWARM_WORKERS, AI_NUMBI_PHASE10_SWARM_EXPLORER, AI_NUMBI_PHASE10_SWARM_WARRIOR);
			case 1:
				return adjustBuildings(AI_NUMBI_PHASE10_INN_NUMBERS, AI_NUMBI_PHASE10_INN_NUMBERS_INC, AI_NUMBI_PHASE10_INN_WORKERS, Intent::Feed);
			case 2:
				return adjustBuildings(AI_NUMBI_PHASE10_HEAL_NUMBERS, AI_NUMBI_PHASE10_HEAL_NUMBERS_INC, AI_NUMBI_PHASE10_HEAL_WORKERS, Intent::Heal);
			case 3:
				return adjustBuildings(AI_NUMBI_PHASE10_SCIENCE_NUMBERS, AI_NUMBI_PHASE10_SCIENCE_NUMBERS_INC, AI_NUMBI_PHASE10_SCIENCE_WORKERS, Intent::TrainConstruction);
			case 4:
				return adjustBuildings(AI_NUMBI_PHASE10_RACETRACK_NUMBERS, AI_NUMBI_PHASE10_RACETRACK_NUMBERS_INC, AI_NUMBI_PHASE10_RACETRACK_WORKERS, Intent::TrainWalk);
			case 5:
				return adjustBuildings(AI_NUMBI_PHASE10_DEFENSE_NUMBERS, AI_NUMBI_PHASE10_DEFENSE_NUMBERS_INC, AI_NUMBI_PHASE10_DEFENSE_WORKERS, Intent::ProjectileDefense);
			case 6:
				return adjustBuildings(AI_NUMBI_PHASE10_BARRACKS_NUMBERS, AI_NUMBI_PHASE10_BARRACKS_NUMBERS_INC, AI_NUMBI_PHASE10_BARRACKS_WORKERS, Intent::TrainAttackStrength);
			case 7:
				return mayAttack(criticalWarriors, criticalTime, AI_NUMBI_WAR_FLAG_UNITS);
			case 8:
				return checkoutExpands(AI_NUMBI_PHASE10_EXPAND_NUMBERS, AI_NUMBI_PHASE10_EXPAND_WORKERS);
			case 9:
				return mayUpgrade(AI_NUMBI_PHASE10_UPGRADE_PTRIGGER, AI_NUMBI_PHASE10_UPGRADE_NTRIGGER);
		}
	}

	return shared_ptr<Order>(new NullOrder);
}

int AINumbi::countUnits(void)
{
	return team->stats.getLatestStat()->totalUnit;
}

int AINumbi::countUnits(const int medicalState)
{
	if (medicalState == Unit::MED_FREE)
	{
		return team->stats.getLatestStat()->totalUnit
			- team->stats.getLatestStat()->needFoodCritical
			- team->stats.getLatestStat()->needFood
			- team->stats.getLatestStat()->needHeal;
	}
	else if (medicalState == Unit::MED_HUNGRY)
	{
		return team->stats.getLatestStat()->needFoodCritical
			+ team->stats.getLatestStat()->needFood;
	}
	else if (medicalState == Unit::MED_DAMAGED)
	{
		return team->stats.getLatestStat()->needHeal;
	}
	else
		assert(false);
	return 0;
}
