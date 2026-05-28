// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <Stream.h>
#include <array>
#include <sstream>

#include "AINumbi.h"
#include "Game.h"
#include "GlobalContainer.h"
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
	assert(goodLoad);
}

void AINumbi::init(Player *player)
{
	timer=0;
	phase=0;
	phaseTime=AI_NUMBI_PHASE_TIME_DEFAULT_TICKS;
	attackPhase=0;
	critticalWarriors=AI_NUMBI_CRITICAL_WARRIORS_DEFAULT;
	critticalTime=AI_NUMBI_CRITICAL_TIME_DEFAULT_TICKS;
	attackTimer=0;
	for (int i=0; i<IntBuildingType::NB_BUILDING; i++)
		mainBuilding[i]=0;
	
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
	
	stream->readEnterSection("AINumbi");
	
	phase            = stream->readSint32("phase");
	attackPhase      = stream->readSint32("attackPhase");
	phaseTime        = stream->readSint32("phaseTime");
	critticalWarriors= stream->readSint32("critticalWarriors");
	critticalTime    = stream->readSint32("critticalTime");
	attackTimer      = stream->readSint32("attackTimer");

	for (int bi=0; bi<IntBuildingType::NB_BUILDING; bi++)
	{
		std::ostringstream oss;
		oss << "mainBuilding[" << bi << "]";
		mainBuilding[bi] = stream->readSint32(oss.str().c_str());
	}
	
	stream->readLeaveSection();
	
	return true;
}

void AINumbi::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("AINumbi");
	
	stream->writeSint32(phase, "phase");
	stream->writeSint32(attackPhase, "attackPhase");
	stream->writeSint32(phaseTime, "phaseTime");
	stream->writeSint32(critticalWarriors, "critticalWarriors");
	stream->writeSint32(critticalTime, "critticalTime");
	stream->writeSint32(attackTimer, "attackTimer");

	for (int bi=0; bi<IntBuildingType::NB_BUILDING; bi++)
	{
		std::ostringstream oss;
		oss << "mainBuilding[" << bi << "]";
		stream->writeSint32(mainBuilding[bi], oss.str().c_str());
	}
	
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
				return adjustBuildings(AI_NUMBI_PHASE0_INN_NUMBERS, AI_NUMBI_PHASE0_INN_NUMBERS_INC, AI_NUMBI_PHASE0_INN_WORKERS, IntBuildingType::FOOD_BUILDING);
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
				return adjustBuildings(AI_NUMBI_PHASE1_INN_NUMBERS, AI_NUMBI_PHASE1_INN_NUMBERS_INC, AI_NUMBI_PHASE1_INN_WORKERS, IntBuildingType::FOOD_BUILDING);
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
				return adjustBuildings(AI_NUMBI_PHASE2_INN_NUMBERS, AI_NUMBI_PHASE2_INN_NUMBERS_INC, AI_NUMBI_PHASE2_INN_WORKERS, IntBuildingType::FOOD_BUILDING);
			case 2:
				return adjustBuildings(AI_NUMBI_PHASE2_HEAL_NUMBERS, AI_NUMBI_PHASE2_HEAL_NUMBERS_INC, AI_NUMBI_PHASE2_HEAL_WORKERS, IntBuildingType::HEAL_BUILDING);
			case 3:
				return adjustBuildings(AI_NUMBI_PHASE2_SCIENCE_NUMBERS, AI_NUMBI_PHASE2_SCIENCE_NUMBERS_INC, AI_NUMBI_PHASE2_SCIENCE_WORKERS, IntBuildingType::SCIENCE_BUILDING);
			case 4:
				return adjustBuildings(AI_NUMBI_PHASE2_RACETRACK_NUMBERS, AI_NUMBI_PHASE2_RACETRACK_NUMBERS_INC, AI_NUMBI_PHASE2_RACETRACK_WORKERS, IntBuildingType::WALKSPEED_BUILDING);
			case 5:
				return adjustBuildings(AI_NUMBI_PHASE2_BARRACKS_NUMBERS, AI_NUMBI_PHASE2_BARRACKS_NUMBERS_INC, AI_NUMBI_PHASE2_BARRACKS_WORKERS, IntBuildingType::ATTACK_BUILDING);
			case 6:
				return adjustBuildings(AI_NUMBI_PHASE2_DEFENSE_NUMBERS, AI_NUMBI_PHASE2_DEFENSE_NUMBERS_INC, AI_NUMBI_PHASE2_DEFENSE_WORKERS, IntBuildingType::DEFENSE_BUILDING);
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
				return adjustBuildings(AI_NUMBI_PHASE4_INN_NUMBERS, AI_NUMBI_PHASE4_INN_NUMBERS_INC, AI_NUMBI_PHASE4_INN_WORKERS, IntBuildingType::FOOD_BUILDING);
			case 2:
				return adjustBuildings(AI_NUMBI_PHASE4_HEAL_NUMBERS, AI_NUMBI_PHASE4_HEAL_NUMBERS_INC, AI_NUMBI_PHASE4_HEAL_WORKERS, IntBuildingType::HEAL_BUILDING);
			case 3:
				return adjustBuildings(AI_NUMBI_PHASE4_SCIENCE_NUMBERS, AI_NUMBI_PHASE4_SCIENCE_NUMBERS_INC, AI_NUMBI_PHASE4_SCIENCE_WORKERS, IntBuildingType::SCIENCE_BUILDING);
			case 4:
				return adjustBuildings(AI_NUMBI_PHASE4_DEFENSE_NUMBERS, AI_NUMBI_PHASE4_DEFENSE_NUMBERS_INC, AI_NUMBI_PHASE4_DEFENSE_WORKERS, IntBuildingType::DEFENSE_BUILDING);
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
				return adjustBuildings(AI_NUMBI_PHASE6_INN_NUMBERS, AI_NUMBI_PHASE6_INN_NUMBERS_INC, AI_NUMBI_PHASE6_INN_WORKERS, IntBuildingType::FOOD_BUILDING);
			case 2:
				return adjustBuildings(AI_NUMBI_PHASE6_HEAL_NUMBERS, AI_NUMBI_PHASE6_HEAL_NUMBERS_INC, AI_NUMBI_PHASE6_HEAL_WORKERS, IntBuildingType::HEAL_BUILDING);
			case 3:
				return adjustBuildings(AI_NUMBI_PHASE6_SCIENCE_NUMBERS, AI_NUMBI_PHASE6_SCIENCE_NUMBERS_INC, AI_NUMBI_PHASE6_SCIENCE_WORKERS, IntBuildingType::SCIENCE_BUILDING);
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
				return adjustBuildings(AI_NUMBI_PHASE8_INN_NUMBERS, AI_NUMBI_PHASE8_INN_NUMBERS_INC, AI_NUMBI_PHASE8_INN_WORKERS, IntBuildingType::FOOD_BUILDING);
			case 2:
				return adjustBuildings(AI_NUMBI_PHASE8_HEAL_NUMBERS, AI_NUMBI_PHASE8_HEAL_NUMBERS_INC, AI_NUMBI_PHASE8_HEAL_WORKERS, IntBuildingType::HEAL_BUILDING);
			case 3:
				return adjustBuildings(AI_NUMBI_PHASE8_SCIENCE_NUMBERS, AI_NUMBI_PHASE8_SCIENCE_NUMBERS_INC, AI_NUMBI_PHASE8_SCIENCE_WORKERS, IntBuildingType::SCIENCE_BUILDING);
			case 4:
				return adjustBuildings(AI_NUMBI_PHASE8_RACETRACK_NUMBERS, AI_NUMBI_PHASE8_RACETRACK_NUMBERS_INC, AI_NUMBI_PHASE8_RACETRACK_WORKERS, IntBuildingType::WALKSPEED_BUILDING);
			case 5:
				return adjustBuildings(AI_NUMBI_PHASE8_DEFENSE_NUMBERS, AI_NUMBI_PHASE8_DEFENSE_NUMBERS_INC, AI_NUMBI_PHASE8_DEFENSE_WORKERS, IntBuildingType::DEFENSE_BUILDING);
			case 6:
				return adjustBuildings(AI_NUMBI_PHASE8_BARRACKS_NUMBERS, AI_NUMBI_PHASE8_BARRACKS_NUMBERS_INC, AI_NUMBI_PHASE8_BARRACKS_WORKERS, IntBuildingType::ATTACK_BUILDING);
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
				return adjustBuildings(AI_NUMBI_PHASE10_INN_NUMBERS, AI_NUMBI_PHASE10_INN_NUMBERS_INC, AI_NUMBI_PHASE10_INN_WORKERS, IntBuildingType::FOOD_BUILDING);
			case 2:
				return adjustBuildings(AI_NUMBI_PHASE10_HEAL_NUMBERS, AI_NUMBI_PHASE10_HEAL_NUMBERS_INC, AI_NUMBI_PHASE10_HEAL_WORKERS, IntBuildingType::HEAL_BUILDING);
			case 3:
				return adjustBuildings(AI_NUMBI_PHASE10_SCIENCE_NUMBERS, AI_NUMBI_PHASE10_SCIENCE_NUMBERS_INC, AI_NUMBI_PHASE10_SCIENCE_WORKERS, IntBuildingType::SCIENCE_BUILDING);
			case 4:
				return adjustBuildings(AI_NUMBI_PHASE10_RACETRACK_NUMBERS, AI_NUMBI_PHASE10_RACETRACK_NUMBERS_INC, AI_NUMBI_PHASE10_RACETRACK_WORKERS, IntBuildingType::WALKSPEED_BUILDING);
			case 5:
				return adjustBuildings(AI_NUMBI_PHASE10_DEFENSE_NUMBERS, AI_NUMBI_PHASE10_DEFENSE_NUMBERS_INC, AI_NUMBI_PHASE10_DEFENSE_WORKERS, IntBuildingType::DEFENSE_BUILDING);
			case 6:
				return adjustBuildings(AI_NUMBI_PHASE10_BARRACKS_NUMBERS, AI_NUMBI_PHASE10_BARRACKS_NUMBERS_INC, AI_NUMBI_PHASE10_BARRACKS_WORKERS, IntBuildingType::ATTACK_BUILDING);
			case 7:
				return mayAttack(critticalWarriors, critticalTime, AI_NUMBI_WAR_FLAG_UNITS);
			case 8:
				return checkoutExpands(AI_NUMBI_PHASE10_EXPAND_NUMBERS, AI_NUMBI_PHASE10_EXPAND_WORKERS);
			case 9:
				return mayUpgrade(AI_NUMBI_PHASE10_UPGRADE_PTRIGGER, AI_NUMBI_PHASE10_UPGRADE_NTRIGGER);
		}
	}

	return shared_ptr<Order>(new NullOrder);
}

int AINumbi::estimateFood(Building *building)
{
	int rx, ry, dist;
	bool found;
	if (map->ressourceAvailableUpdate(team->teamNumber, CORN, 0, building->posX-1, building->posY-1, &rx, &ry, &dist))
		found=true;
	else if (map->ressourceAvailableUpdate(team->teamNumber, CORN, 0, building->posX+building->type->width+1, building->posY-1, &rx, &ry, &dist))
		found=true;
	else if (map->ressourceAvailableUpdate(team->teamNumber, CORN, 0, building->posX+building->type->width+1, building->posY+building->type->height+1, &rx, &ry, &dist))
		found=true;
	else if (map->ressourceAvailableUpdate(team->teamNumber, CORN, 0, building->posX-1, building->posY+building->type->height+1, &rx, &ry, &dist))
		found=true;
	else
		found=false;

	if (found)
	{
		rx+=map->getW();
		ry+=map->getH();

		int w=0;
		int h=0;
		int i;
		int rxl, rxr, ryt, ryb;
		int hole;

		hole=AI_NUMBI_CORN_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_CORN_SCAN_MAX_RADIUS; i++)
			if (map->isRessourceTakeable(rx+i, ry, CORN)||map->isRessourceTakeable(rx+i, ry-1, CORN))
				w++;
			else if (hole--<0)
				break;
		rxr=rx+i;
		hole=AI_NUMBI_CORN_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_CORN_SCAN_MAX_RADIUS; i++)
			if (map->isRessourceTakeable(rx-i, ry, CORN)||map->isRessourceTakeable(rx-i, ry-1, CORN))
				w++;
			else if (hole--<0)
				break;
		rxl=rx-i;

		rx=((rxr+rxl)>>1);

		hole=AI_NUMBI_CORN_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_CORN_SCAN_MAX_RADIUS; i++)
			if (map->isRessourceTakeable(rx, ry+i, CORN)||map->isRessourceTakeable(rx-1, ry+i, CORN))
				h++;
			else if (hole--<0)
				break;
		ryb=ry+i;
		hole=AI_NUMBI_CORN_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_CORN_SCAN_MAX_RADIUS; i++)
			if (map->isRessourceTakeable(rx, ry-i, CORN)||map->isRessourceTakeable(rx-1, ry-i, CORN))
				h++;
			else if (hole--<0)
				break;
		ryt=ry-i;

		ry=((ryb+ryt)>>1);


		hole=AI_NUMBI_CORN_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_CORN_SCAN_MAX_RADIUS; i++)
			if (map->isRessourceTakeable(rx, ry+i, CORN)||map->isRessourceTakeable(rx+1, ry+i, CORN))
				h++;
			else if (hole--<0)
				break;
		ryb=ry+i;
		hole=AI_NUMBI_CORN_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_CORN_SCAN_MAX_RADIUS; i++)
			if (map->isRessourceTakeable(rx, ry-i, CORN)||map->isRessourceTakeable(rx+1, ry-i, CORN))
				h++;
			else if (hole--<0)
				break;
		ryt=ry-i;

		ry=((ryt+ryb)>>1);
		w=0;
		hole=AI_NUMBI_CORN_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_CORN_SCAN_MAX_RADIUS; i++)
			if (map->isRessourceTakeable(rx+i, ry, CORN)||map->isRessourceTakeable(rx+i, ry+1, CORN))
				w++;
			else if (hole--<0)
				break;
		hole=AI_NUMBI_CORN_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_CORN_SCAN_MAX_RADIUS; i++)
			if (map->isRessourceTakeable(rx-i, ry, CORN)||map->isRessourceTakeable(rx-i, ry+1, CORN))
				w++;
			else if (hole--<0)
				break;
		
		//printf("r=(%d, %d), w=%d, h=%d, s=%d.\n", rx, ry, w, h, w*h);

		return (w*h);
	}
	else
		return 0;
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

std::shared_ptr<Order>AINumbi::swarmsForWorkers(const int minSwarmNumbers, const int nbWorkersFator, const int workers, const int explorers, const int warriors)
{
	std::list<Building *> swarms=team->swarms;
	int ss=swarms.size();
	Sint32 numberRequested=1+(nbWorkersFator/(ss+1));
	int nbu=countUnits();

	for (std::list<Building *>::iterator it=swarms.begin(); it!=swarms.end(); ++it)
	{
		Building *b=*it;
		if ((b->ratio[WORKER]!=workers)||(b->ratio[EXPLORER]!=explorers)||(b->ratio[WARRIOR]!=warriors))
		{
			// Stack buffer for the order payload — the per-viewer GUI shadow
			// that used to back this lives in BuildingGuiState now and is
			// off-limits to AI code.
			Sint32 newRatio[NB_UNIT_TYPE];
			newRatio[WORKER]=workers;
			newRatio[EXPLORER]=explorers;
			newRatio[WARRIOR]=warriors;
			return shared_ptr<Order>(new OrderModifySwarm(b->gid, newRatio));
		}

		int f=estimateFood(b);
		int numberRequestedTemp=numberRequested;
		int numberRequestedLoca=b->maxUnitWorking;
		if (f<(nbu*AI_NUMBI_LOW_FOOD_PER_UNIT-1))
			numberRequestedTemp=0;
		else if (numberRequestedLoca==0)
			if (f<(nbu*AI_NUMBI_HIGH_FOOD_PER_UNIT+1))
				numberRequestedTemp=0;
		
		if (numberRequestedLoca!=numberRequestedTemp)
		{
			//printf("AI: (%d) numberRequested changed to (nrt=%d) (nrl=%d)(f=%d) (nbu=%d).\n", b->UID, numberRequestedTemp, numberRequestedLoca, f, nbu);
			return shared_ptr<Order>(new OrderModifyBuilding(b->gid, numberRequestedTemp));
		}
	}
	if (ss<minSwarmNumbers)
	{
		//printf("AI: not enough swarms (%d<%d).\n", ss, minSwarmNumbers);
		// TODO !
		// assert(false);
		/*int x, y;
		if (findNewEmplacement(IntBuildingType::SWARM_BUILDING, &x, &y))
		{
			Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum("swarm", 0, true);
			int teamNumber=player->team->teamNumber;
			return shared_ptr<Order>(new OrderCreate(teamNumber, x, y, typeNum));
		}*/
	}
	return shared_ptr<Order>(new NullOrder);
}

void AINumbi::nextMainBuilding(const int buildingType)
{
	//printf("AI: nextMainBuilding(%d)\n", buildingType);
	Building **myBuildings=team->myBuildings;
	Building *b=myBuildings[mainBuilding[buildingType]];
	if (b==NULL)
	{
		for (int i=1; i<Building::MAX_COUNT; i++)
			if ((myBuildings[i])/*&&((myBuildings[i]->type->shortTypeNum==buildingType)||(myBuildings[i]->type->shortTypeNum==0))*/)
			{
				b=myBuildings[i];
				break;
			}
		if (b==NULL)
		{
			mainBuilding[buildingType]=0;
			//printf("AI: no more building !.\n");
		}
		else
			mainBuilding[buildingType]=Building::GIDtoID(b->gid);
	}
	else
	{
		//printf("AI: nextMainBuilding uid=%d\n", b->UID);
		int id=Building::GIDtoID(b->gid);
		// [POSSIBLE BUG H1] The mask AI_NUMBI_BUILDING_INDEX_MASK (=0xFF, i.e. 255)
		// is hardcoded but the loop bound is Building::MAX_COUNT (=1024). When
		// (i+id) exceeds 255 the index wraps within the first 256 building slots,
		// missing buildings 256..1023. The constant intentionally does NOT alias
		// `Building::MAX_COUNT - 1` — renaming would change behavior. Preserved
		// verbatim; flagged for fix-time review (do not "fix" here).
		for (int i=1; i<Building::MAX_COUNT; i++)
			if ((myBuildings[(i+id)&AI_NUMBI_BUILDING_INDEX_MASK])/*&&((myBuildings[(i+id)&AI_NUMBI_BUILDING_INDEX_MASK]->type->shortTypeNum==buildingType)||(myBuildings[(i+id)&AI_NUMBI_BUILDING_INDEX_MASK]->type->shortTypeNum==0))*/)
			{
				b=myBuildings[(i+id)&AI_NUMBI_BUILDING_INDEX_MASK];
				break;
			}
		mainBuilding[buildingType]=Building::GIDtoID(b->gid);
		//printf("AI: nextMainBuilding newuid=%d\n", b->UID);
	}
}

int AINumbi::nbFreeAround(const int buildingType, int posX, int posY, int width, int height)
{
	int px=posX+map->getW();
	int py=posY+map->getH();
	int x, y;
	
	int valid=AI_NUMBI_PLACEMENT_SCORE_INIT;
	int r;
	for (r=AI_NUMBI_OUTER_MARGIN_R_MIN; r<=AI_NUMBI_OUTER_MARGIN_R_MAX; r++)
	{
		y=py-r;
		int ew=1;
		for (x=px-ew; x<px+width+ew; x++)
			if (!map->isFreeForBuilding(x, y))
			{
				valid-=AI_NUMBI_OUTER_EDGE_PENALTY+(r-AI_NUMBI_OUTER_MARGIN_R_MIN)*AI_NUMBI_OUTER_EDGE_PENALTY;
				break;
			}
		y=py+height-1+r;
		for (x=px-ew; x<px+width+ew; x++)
			if (!map->isFreeForBuilding(x, y))
			{
				valid-=AI_NUMBI_OUTER_EDGE_PENALTY+(r-AI_NUMBI_OUTER_MARGIN_R_MIN)*AI_NUMBI_OUTER_EDGE_PENALTY;
				break;
			}

		x=px-r;
		for (y=py-ew; y<py+height+ew; y++)
			if (!map->isFreeForBuilding(x, y))
			{
				valid-=AI_NUMBI_OUTER_EDGE_PENALTY+(r-AI_NUMBI_OUTER_MARGIN_R_MIN)*AI_NUMBI_OUTER_EDGE_PENALTY;
				break;
			}
		x=px+width-1+r;
		for (y=py-ew; y<py+height+ew; y++)
			if (!map->isFreeForBuilding(x, y))
			{
				valid-=AI_NUMBI_OUTER_EDGE_PENALTY+(r-AI_NUMBI_OUTER_MARGIN_R_MIN)*AI_NUMBI_OUTER_EDGE_PENALTY;
				break;
			}
	}
	for (r=1; r<=1; r++)
	{
		y=py-r;
		for (x=px; x<px+width; x++)
			if (!map->isFreeForBuilding(x, y))
			{
				valid-=AI_NUMBI_INNER_EDGE_PENALTY;
				break;
			}
		y=py+height-1+r;
		for (x=px; x<px+width; x++)
			if (!map->isFreeForBuilding(x, y))
			{
				valid-=AI_NUMBI_INNER_EDGE_PENALTY;
				break;
			}

		x=px-r;
		for (y=py; y<py+height; y++)
			if (!map->isFreeForBuilding(x, y))
			{
				valid-=AI_NUMBI_INNER_EDGE_PENALTY;
				break;
			}
		x=px+width-1+r;
		for (y=py; y<py+height; y++)
			if (!map->isFreeForBuilding(x, y))
			{
				valid-=AI_NUMBI_INNER_EDGE_PENALTY;
				break;
			}
	}
	
	for (r=1; r<=AI_NUMBI_FREE_REGION_SCAN_RANGE; r++)
	{
		y=py-r;
		bool anyBuild=false;
		for (x=px; x<px+width; x++)
			if (!map->isFreeForBuilding(x, y))
			{
				anyBuild=true;
				break;
			}
		if (!anyBuild)
			break;
	}
	int wu=r;
	for (r=1; r<=AI_NUMBI_FREE_REGION_SCAN_RANGE; r++)
	{
		y=py+height-1+r;
		bool anyBuild=false;
		for (x=px; x<px+width; x++)
			if (!map->isFreeForBuilding(x, y))
			{
				anyBuild=true;
				break;
			}
		if (!anyBuild)
			break;
	}
	wu+=r;
	for (r=1; r<=AI_NUMBI_FREE_REGION_SCAN_RANGE; r++)
	{
		bool anyBuild=false;
		x=px-r;
		for (y=py; y<py+height; y++)
			if (!map->isFreeForBuilding(x, y))
			{
				anyBuild=true;
				break;
			}
		if (!anyBuild)
			break;
	}
	int hu=r;
	for (r=1; r<=AI_NUMBI_FREE_REGION_SCAN_RANGE; r++)
	{
		bool anyBuild=false;
		x=px+width-1+r;
		for (y=py; y<py+height; y++)
			if (!map->isFreeForBuilding(x, y))
			{
				anyBuild=true;
				break;
			}
		if (!anyBuild)
			break;
	}
	hu+=r;
	
	valid-=(wu)*(hu);
	
	return valid;
}

bool AINumbi::parseBuildingType(const int buildingType)
{
	return (buildingType==IntBuildingType::DEFENSE_BUILDING);
}

void AINumbi::squareCircleScann(int &dx, int &dy, int &sx, int &sy, int &x, int &y, int &mx, int &my)
{
	if (x>=mx)
	{
		dx=0;
		dy=1;
		mx++;
	}
	else if (y>=my)
	{
		dx=-1;
		dy=0;
		my++;
	}
	else if (x<=sx)
	{
		dx=0;
		dy=-1;
		sx--;
	}
	else if (y<=sy)
	{
		dx=1;
		dy=0;
		sy--;
	}
	x+=dx;
	y+=dy;
}

bool AINumbi::findNewEmplacement(const int buildingType, int *posX, int *posY)
{
	Building **myBuildings=team->myBuildings;
	Building *b=myBuildings[mainBuilding[buildingType]];
	if (b==NULL)
	{
		nextMainBuilding(buildingType);
		b=myBuildings[mainBuilding[buildingType]];
	}
	if (b==NULL)
	{
		for (int i=0; i<IntBuildingType::NB_BUILDING; i++)
		{
			if (myBuildings[mainBuilding[i]])
			{
				b=myBuildings[mainBuilding[i]];
				break;
			}
		}
	}
	if (b==NULL)
	{
		// TODO : scan the units and find a ressoucefull place.
		return false;
	}
	int typeNum=globalContainer->buildingsTypes.getTypeNum(IntBuildingType::typeFromShortNumber(buildingType), 0, true);
	BuildingType *bt=globalContainer->buildingsTypes.get(typeNum);
	int width=bt->width;
	int height=bt->height;
	
	int valid=nbFreeAround(buildingType, b->posX, b->posY, width, height);
	//printf("AI: findNewEmplacement(%d) valid=(%d), uid=(%d), s=(%d, %d).\n", buildingType, valid, b->UID, width, height);
	if (valid>AI_NUMBI_PLACEMENT_SCORE_MIN)
	{
		// [POSSIBLE BUG L9] `maxr` is computed below but never read — the spiral
		// scan further down uses AI_NUMBI_SCAN_ITERATIONS (=4096) directly.
		// Preserved verbatim for replay determinism; do not "fix".
		[[maybe_unused]] int maxr;
		if (b->type->shortTypeNum==0)
			maxr=AI_NUMBI_SWARM_SEARCH_RADIUS;
		else
			maxr=AI_NUMBI_NONSWARM_SEARCH_RADIUS;
		//for (int r=0; r<=maxr; r++)
		//	for (int d=0; d<8; d++)

		int dx, dy, sx, sy, px, py, mx, my;
		int margin;
		if (b->type->shortTypeNum)
			margin=0;
		else
			margin=AI_NUMBI_SWARM_MARGIN;

		int bposX=b->posX+map->getW();
		int bposY=b->posY+map->getH();
		
		sx=bposX-width-margin;
		sy=bposY-height-margin;

		px=sx;
		py=sy;
		
		mx=bposX+b->type->width+margin;
		my=bposY+b->type->height+margin;
		
		sy--;
		px++;
		dx=1;
		dy=0;
		
		int bestValid=-1;
		// Note: AI_NUMBI_SCAN_ITERATIONS is intentionally NOT derived from `maxr`
		// above (see L9 comment); it is the original literal preserved as-is.
		for (int i=0; i<AI_NUMBI_SCAN_ITERATIONS; i++)
		{
			squareCircleScann(dx, dy, sx, sy, px, py, mx, my);
			//printf("AI:i=%d, d=(%d, %d), s=(%d, %d), p=(%d, %d), m=(%d, %d).\n", i, dx, dy, sx, sy, px, py, mx, my);

			//int dx, dy;
			//Unit::dxDyFromDirection(d, &dx, &dy);

			//int px=b->posX+dx*(width+r);
			//int py=b->posY+dy*(height+r);
			if (map->isFreeForBuilding(px, py, width, height))
			{
				int valid=nbFreeAround(buildingType, px, py, width, height);
				if ((valid>AI_NUMBI_PLACEMENT_SCORE_MIN)&&(game->checkRoomForBuilding(px, py, bt, player->team->teamNumber)))
				{
					int rx, ry, dist;
					bool nr=map->ressourceAvailableUpdate(team->teamNumber, CORN, 0, px, py, &rx, &ry, &dist);
					if (nr)
					{
						//int dist=map->warpDistSquare(px+1, py+1, rx, ry);
						if (((dist<=(AI_NUMBI_CORN_DISTANCE_BIAS+width*height))&&(buildingType<=AI_NUMBI_NEAR_CORN_TYPE_CUTOFF))||((dist>=(AI_NUMBI_CORN_DISTANCE_BIAS+width*height))&&(buildingType>AI_NUMBI_NEAR_CORN_TYPE_CUTOFF)))
						{
							//printf("AI: findNewEmplacement d=%d valid=%d.\n", d, valid);
							if (valid>bestValid)
							{
								*posX=px;
								*posY=py;
								bestValid=valid;
								if ((b->type->shortTypeNum==0)||(parseBuildingType(buildingType)))
									nextMainBuilding(buildingType);
							}
						}
					}
					else if (buildingType!=AI_NUMBI_NEAR_CORN_TYPE_CUTOFF)
					{
						//printf("AI: findNewEmplacement d=%d valid=%d.\n", d, valid);
						if (valid>bestValid)
						{
							*posX=px;
							*posY=py;
							bestValid=valid;
							if ((b->type->shortTypeNum==0)||(parseBuildingType(buildingType)))
								nextMainBuilding(buildingType);
						}
					}
				}
			}
		}
		if (bestValid>-1)
			return true;
		nextMainBuilding(buildingType);
		return false;
	}
	nextMainBuilding(buildingType);
	return false;
}

std::shared_ptr<Order>AINumbi::mayAttack(int critticalMass, int critticalTimeout, Sint32 numberRequested)
{
	Unit **myUnits=team->myUnits;
	int ft=0;
	for (int i=0; i<Unit::MAX_COUNT; i++)
		if ((myUnits[i])&&(myUnits[i]->performance[ATTACK_SPEED])&&(myUnits[i]->medical==0))
			ft++;

	if (attackPhase==0)
	{
		if (ft>=critticalMass)
		{
			//printf("AI:(crittical mass)new attack with %d units.\n", ft);
			attackPhase=1;
		}
		attackTimer++;
		if ((attackTimer>=critticalTimeout)&&(ft>numberRequested))
		{
			attackTimer=0;
			//printf("AI:(timeout)new attack with %d units.\n", ft);
			attackPhase=1;
		}
		return shared_ptr<Order>(new NullOrder);
	}
	else if (attackPhase==1)
	{
		if (ft<=(critticalMass/AI_NUMBI_STOP_ATTACK_DIVISOR))
		{
			attackPhase=3;
			//printf("AI:stop attack.\n");
			return shared_ptr<Order>(new NullOrder);
		}

		int teamNumber=player->team->teamNumber;

		for (std::list<Building *>::iterator bit=team->virtualBuildings.begin(); bit!=team->virtualBuildings.end(); ++bit)
			if ((*bit)->type->shortTypeNum==IntBuildingType::WAR_FLAG)
			{
				Building *b=*bit;
				int gbid=map->getBuilding(b->posX, b->posY);
				if (gbid==NOGBID || Building::GIDtoTeam(gbid)==teamNumber)
					return shared_ptr<Order>(new OrderDelete(b->gid)); // The target has beed successfully killed.

				if (b->maxUnitWorking!=numberRequested)
				{
					//printf("AI: OrderModifyBuilding(%d, %d)\n", b->gid, numberRequested);
					return shared_ptr<Order>(new OrderModifyBuilding(b->gid, numberRequested));
				}
			}

		// We look for a specific enemy:
		Uint32 enemies=player->team->enemies;
		int e=-1;
		for (int i=0; i<game->mapHeader.getNumberOfTeams(); i++)
			if (game->teams[i]->me & enemies)
				e=i;
		if (e==-1)
			return shared_ptr<Order>(new NullOrder);

		int ex=-1, ey=-1;
		int count=0;
		bool found=false;
		for (int i=0; i<Building::MAX_COUNT; i++)
		{
			Building *b=game->teams[e]->myBuildings[i];
			if (b)
			{
				ex=b->posX;
				ey=b->posY;

				if ((syncRand()&AI_NUMBI_ENEMY_FLAG_CHANCE_MASK)==0)
				{
					bool already=false;
					count=0;
					for (std::list<Building *>::iterator bit=team->virtualBuildings.begin(); bit!=team->virtualBuildings.end(); ++bit)
						if ((*bit)->type->shortTypeNum==IntBuildingType::WAR_FLAG)
						{
							count++;
							if ((*bit)->posX==ex &&(*bit)->posY==ey)
							{
								already=true;
								break;
							}
						}
					if (!already)
					{
						found=true;
						break;
					}
				}
			}
		}

		if (ex!=-1 && ey!=-1 && found && count<AI_NUMBI_MAX_WAR_FLAGS)
		{
			Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum("warflag", 0, false);
			//printf("AI: OrderCreateWarFlag(%d, %d)\n", ex, ey);
			return shared_ptr<Order>(new OrderCreate(teamNumber, ex, ey, typeNum, AI_NUMBI_WAR_FLAG_INIT_UNITS_WORKING, AI_NUMBI_WAR_FLAG_INIT_FLAG_RADIUS));
		}
		else
			return shared_ptr<Order>(new NullOrder);
	}
	else if (attackPhase==2)
	{
		assert(false);
		return shared_ptr<Order>(new NullOrder);
	}
	else if (attackPhase==3)
	{
		for (std::list<Building *>::iterator bit=team->virtualBuildings.begin(); bit!=team->virtualBuildings.end(); ++bit)
			if ((*bit)->type->shortTypeNum==IntBuildingType::WAR_FLAG)
				return shared_ptr<Order>(new OrderDelete((*bit)->gid));
		attackPhase=0;
		critticalWarriors*=AI_NUMBI_ATTACK_BACKOFF_MULTIPLIER;
		critticalTime*=AI_NUMBI_ATTACK_BACKOFF_MULTIPLIER;
		return shared_ptr<Order>(new NullOrder);
	}
	else
	{
		assert(false);
		return shared_ptr<Order>(new NullOrder);
	}
	
}

std::shared_ptr<Order>AINumbi::adjustBuildings(const int numbers, const int numbersInc, const int workers, const int buildingType)
{
	Building **myBuildings=team->myBuildings;
	//Unit **myUnits=player->team->myUnits;
	int fb=0;
	
	for (int i=0; i<Building::MAX_COUNT; i++)
	{
		Building *b=myBuildings[i];
		if ((b)&&(b->type->shortTypeNum==buildingType))
		{
			fb++;
			int w=workers;
			if ((b->maxUnitWorking!=w)&&(b->type->maxUnitWorking))
				return shared_ptr<Order>(new OrderModifyBuilding(b->gid, w));
		}
	}
	
	int wr=countUnits();
	
	if (buildingType==IntBuildingType::FOOD_BUILDING)
		wr+=AI_NUMBI_HUNGRY_INN_DEMAND_MULT*countUnits(Unit::MED_HUNGRY);
	else if (buildingType==IntBuildingType::HEAL_BUILDING)
		wr+=AI_NUMBI_DAMAGED_HEAL_DEMAND_MULT*countUnits(Unit::MED_DAMAGED);
	
	if (fb<((wr/numbers)+numbersInc))
	{
		//printf("AI: findNewEmplacement(%d), fb=%d, wr=%d, numbers=%d, numbersInc=%d, nn=%d.\n", buildingType, fb, wr, numbers, numbersInc, ((wr/numbers)+numbersInc));
		int x, y;
		if (findNewEmplacement(buildingType, &x, &y))
		{
			Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum(IntBuildingType::typeFromShortNumber(buildingType), 0, true);
			int teamNumber=team->teamNumber;
			return shared_ptr<Order>(new OrderCreate(teamNumber, x, y, typeNum, AI_NUMBI_BUILD_ORDER_UNITS_WORKING, AI_NUMBI_BUILD_ORDER_FLAG_RADIUS));
		}
		//printf("AI: findNewEmplacement(%d) failed.\n", buildingType);
		return shared_ptr<Order>(new NullOrder);
	}
	else
		return shared_ptr<Order>(new NullOrder);
}

std::shared_ptr<Order>AINumbi::checkoutExpands(const int numbers, const int workers)
{
	//std::list<Building *> swarms=team->swarms;
	//int ss=swarms.size();
	
	Building **myBuildings=team->myBuildings;
	int ss=0;
	for (int i=0; i<Building::MAX_COUNT; i++)
	{
		Building *b=myBuildings[i];
		if ((b)&&(b->type->shortTypeNum==0))
			ss++;
	}
	
	int wr=countUnits();

	if (ss<=(wr/numbers))
	{
		//printf("AI: checkoutExpands(%d<%d=(%d/%d)).\n", ss, (wr/numbers), wr, numbers);
		int x, y;
		if (findNewEmplacement(IntBuildingType::SWARM_BUILDING, &x, &y))
		{
			Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum("swarm", 0, true);
			int teamNumber=team->teamNumber;
			return shared_ptr<Order>(new OrderCreate(teamNumber, x, y, typeNum, AI_NUMBI_BUILD_ORDER_UNITS_WORKING, AI_NUMBI_BUILD_ORDER_FLAG_RADIUS));
		}
		return shared_ptr<Order>(new NullOrder);
	}
	else
		return shared_ptr<Order>(new NullOrder);
}

namespace {

// The five building kinds AINumbi considers for level upgrades. Iteration
// order is the upgrade-priority order — food first, defense last — and is
// part of the deterministic order stream; do not reorder without rebaselining.
enum UpgradeKind
{
	UK_FOOD = 0,
	UK_HEAL,
	UK_ATTACK,
	UK_SCIENCE,
	UK_DEFENSE,
	NB_UPGRADE_KINDS
};

// Extra in-flight upgrades tolerated at each kind's rung threshold. Only
// SCIENCE carries a non-zero value; the original C++ added
// AI_NUMBI_SCIENCE_UPGRADE_TOLERANCE inline in two of ten copy-pasted
// conditionals.
constexpr int kUpgradeKindTolerance[NB_UPGRADE_KINDS] = {
	0,                                  // UK_FOOD
	0,                                  // UK_HEAL
	0,                                  // UK_ATTACK
	AI_NUMBI_SCIENCE_UPGRADE_TOLERANCE, // UK_SCIENCE
	0,                                  // UK_DEFENSE
};

struct UpgradeInventory
{
	int number[NB_UNIT_LEVELS] = {};      // completed (non-site) buildings per level
	int upgrading[NB_UNIT_LEVELS] = {};   // sites currently upgrading to this level
	Building *exemplar[NB_UNIT_LEVELS] = {}; // a chosen instance per level, or null
};

int upgradeKindFor(int shortTypeNum)
{
	switch (shortTypeNum)
	{
		case IntBuildingType::FOOD_BUILDING:    return UK_FOOD;
		case IntBuildingType::HEAL_BUILDING:    return UK_HEAL;
		case IntBuildingType::ATTACK_BUILDING:  return UK_ATTACK;
		case IntBuildingType::SCIENCE_BUILDING: return UK_SCIENCE;
		case IntBuildingType::DEFENSE_BUILDING: return UK_DEFENSE;
		default:                                return -1;
	}
}

// Walks every building owned by `team` and tallies, per (kind, level): the
// count of completed buildings, the count of upgrading sites, and one
// "exemplar" — a deterministically chosen building used as the target for
// the next upgrade order. The exemplar is selected by an unbiased syncRand
// coin flip on each completed building, so for k buildings at one (kind,
// level) the last one wins with probability 1/2, the previous with 1/4,
// etc. syncRand() is the lockstep RNG, so the result is identical across
// networked clients.
std::array<UpgradeInventory, NB_UPGRADE_KINDS> collectUpgradeInventory(Team *team)
{
	std::array<UpgradeInventory, NB_UPGRADE_KINDS> inv{};
	Building **myBuildings = team->myBuildings;
	for (int i = 0; i < Building::MAX_COUNT; i++)
	{
		Building *b = myBuildings[i];
		if (!b)
			continue;
		const int kind = upgradeKindFor(b->type->shortTypeNum);
		if (kind < 0)
			continue;
		const int l = b->type->level;
		if (b->type->isBuildingSite)
			inv[kind].upgrading[l]++;
		else
		{
			inv[kind].number[l]++;
			if (syncRand() & 1)
				inv[kind].exemplar[l] = b;
		}
	}
	return inv;
}

// Tries one ladder rung: for each upgradeable kind in priority order,
// checks whether the colony has more completed level-srcLevel buildings
// than are currently being upgraded to level srcLevel+1 (plus the per-kind
// tolerance). Returns an OrderConstruction targeting the first eligible
// kind's exemplar at srcLevel, or nullptr if none.
//
// Pre BH-220, the C++ original passed exemplar[0] for both rungs (level
// 0→1 and 1→2), so the level-1→2 path always re-issued level-0→1 upgrades
// and AINumbi's tech tree stalled at level 1. This helper reads
// exemplar[srcLevel] uniformly, fixing that behavior.
std::shared_ptr<Order> tryUpgradeRung(
	const std::array<UpgradeInventory, NB_UPGRADE_KINDS> &inv,
	int srcLevel)
{
	for (int kind = 0; kind < NB_UPGRADE_KINDS; ++kind)
	{
		const UpgradeInventory &slot = inv[kind];
		if (slot.number[srcLevel] > slot.upgrading[srcLevel + 1] + kUpgradeKindTolerance[kind])
		{
			Building *b = slot.exemplar[srcLevel];
			if (b)
				return std::make_shared<OrderConstruction>(b->gid, AI_NUMBI_UPGRADE_ORDER_LEVEL, AI_NUMBI_UPGRADE_ORDER_REPAIR);
		}
	}
	return nullptr;
}

} // namespace

// Issues one building-upgrade order if (a) the colony has enough free or
// schooled units to staff higher-level buildings — gated against ptrigger
// (potential = working units at higher levels, weighted by SCIENCE stock)
// and ntrigger (now = free units at higher levels) — and (b) there is a
// completed building of an upgradeable kind that is not already saturated
// with in-flight upgrades. Tries level 0→1 first, then 1→2; returns
// NullOrder if neither rung is eligible.
std::shared_ptr<Order> AINumbi::mayUpgrade(const int ptrigger, const int ntrigger)
{
	const auto inv = collectUpgradeInventory(team);

	Unit **myUnits = team->myUnits;
	int wun[NB_UNIT_LEVELS] = {}; // working units per BUILD level
	int fun[NB_UNIT_LEVELS] = {}; // free (ACT_RANDOM) units per BUILD level
	for (int i = 0; i < Unit::MAX_COUNT; i++)
	{
		Unit *u = myUnits[i];
		if (!u)
			continue;
		const int l = u->level[BUILD];
		if (u->activity == Unit::ACT_RANDOM)
			fun[l]++;
		wun[l]++;
	}

	const UpgradeInventory &science = inv[UK_SCIENCE];

	// Level 0 → 1 rung.
	{
		const int sciencePool = science.number[0] + science.number[1] + science.number[2] + science.number[3];
		const int potential = wun[1] + wun[2] + wun[3] + AI_NUMBI_SCHOOL_POTENTIAL_WEIGHT * sciencePool;
		const int now = fun[1] + fun[2] + fun[3];
		if (potential > ptrigger && now > ntrigger)
		{
			if (auto order = tryUpgradeRung(inv, 0))
				return order;
		}
	}

	// Level 1 → 2 rung.
	{
		const int sciencePool = science.number[1] + science.number[2] + science.number[3];
		const int potential = wun[2] + wun[3] + AI_NUMBI_SCHOOL_POTENTIAL_WEIGHT * sciencePool;
		const int now = fun[2] + fun[3];
		if (potential > ptrigger && now > ntrigger)
		{
			if (auto order = tryUpgradeRung(inv, 1))
				return order;
		}
	}

	return std::make_shared<NullOrder>();
}


