/*
  Copyright (C) 2001-2006 Stephane Magnenat & Luc-Olivier de Charriere
  for any question or comment contact us at nct at ysagoon dot com or
  nuage at ysagoon dot com

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef __BUILDING_H
#define __BUILDING_H

#include <list>
#include <vector>

#include "Bullet.h"
#include "Ressource.h"
#include "UnitConsts.h"

namespace GAGCore
{
	class InputStream;
	class OutputStream;
}


class Unit;
class Team;
class BuildingType;
class BuildingsTypes;

class Building
{
public:

	enum BuildingState
	{
		DEAD=0,
		ALIVE=1,
		WAITING_FOR_DESTRUCTION=2,
		WAITING_FOR_CONSTRUCTION=3,
		WAITING_FOR_CONSTRUCTION_ROOM=4
	};
	
	enum ConstructionResultState
	{
		NO_CONSTRUCTION=0,
		NEW_BUILDING=1,
		UPGRADE=2,
		REPAIR=3
	};
	
	enum MatchPriority
	{
		MP_LOW    = 0x10000000,
		MP_MEDIUM = 0x20000000,
		MP_HIGH   = 0x30000000,
	};

	// type
	Sint32 typeNum; // number in BuildingTypes
	int shortTypeNum;
	BuildingType *type;

	// construction state
	BuildingState buildingState;
	ConstructionResultState constructionResultState;

	// units
	Sint32 maxUnitWorkingLocal;
	Sint32 maxUnitWorking;  // (Uint16)
	Sint32 maxUnitWorkingPreferred;
	std::list<Unit *> unitsWorking;
	std::list<Unit *> unitsWorkingSubscribe;
	Sint32 subscriptionWorkingTimer;
	Sint32 maxUnitInside;
	std::list<Unit *> unitsInside;
	std::list<Unit *> unitsInsideSubscribe;
	Sint32 subscriptionInsideTimer;
	MatchPriority matchPriority;
	std::vector<Unit *> newlyFillingUnits; //!< filling units recently matched that must optimally be allocated to resources this building need
	
	// optimisation and consistency
	// Included in {0: unknow, 1:already in owner-><same name>, 2:not in owner-><same name>
	Sint32 subscribeForInside, subscribeToBringRessources, subscribeForFlaging; 
	Sint32 canFeedUnit; // Included in {0: unknow, 1:already in owner->canFeedUnit, 2:not in owner->canFeedUnit}
	Sint32 canHealUnit; // Included in {0: unknow, 1:already in owner->canHealUnit, 2:not in owner->canHealUnit}
	Sint32 fillable; // Included in {0: unknow, 1:already in owner->fillable, 2:not in owner->fillable
	Sint32 zonableWorkers[2]; // Included in {0: unknow, 1:already in owner->zonableWorkers[x], 2:not in owner->zonableWorkers[x]}
	Sint32 zonableExplorer; // Included in {0: unknow, 1:already in owner->zonableExplorer, 2:not in owner->zonableExplorer}
	Sint32 zonableWarrior;// Included in {0: unknow, 1:already in owner->zonableWarrior, 2:not in owner->zonableWarrior}
	Sint32 upgrade[NB_ABILITY]; // Included in {0: unknow, 1:already in owner->upgrade[i], 2:not in owner->upgrade[i]}
	
	// identity
	Uint16 gid; // for reservation see GIDtoID() and GIDtoTeam().
	Team *owner;

	// position
	Sint32 posX, posY; // (Uint16)
	Sint32 posXLocal, posYLocal;

	// Flag usefull :
	Uint32 unitStayRange; // (Uint8)
	Uint32 unitStayRangeLocal;
	bool clearingRessources[BASIC_COUNT]; // true if the resource has to be cleared.
	bool clearingRessourcesLocal[BASIC_COUNT];
	Sint32 minLevelToFlag;
	Sint32 minLevelToFlagLocal;

	// Building specific :
	Sint32 ressources[MAX_NB_RESSOURCES]; //!< Amount stocked, or used for building building.
	Sint32 wishedResources[MAX_NB_RESSOURCES]; //!< number of resource we need, weighted by the need. Recomputed on computeWishedRessources()
	Sint32 neededResources[MAX_NB_RESSOURCES]; //!< number of resource we do not have. Recomputed on computeWishedRessources()

	// quality parameters
	Sint32 hp; // (Uint16)

	// swarm building parameters
	Sint32 productionTimeout;
	Sint32 totalRatio;
	Sint32 ratio[NB_UNIT_TYPE];
	Sint32 ratioLocal[NB_UNIT_TYPE];
	Sint32 percentUsed[NB_UNIT_TYPE];

	// exchange building parameters
	Uint32 receiveRessourceMask;
	Uint32 sendRessourceMask;
	Uint32 receiveRessourceMaskLocal;
	Uint32 sendRessourceMaskLocal;

	// turrets building parameters
	Uint32 shootingStep;
	Sint32 shootingCooldown;
	Sint32 bullets;
	
	// A true bit meant that the corresponding team can see this building, under FOW or not.
	Uint32 seenByMask;
	
	bool dirtyLocalGradient[2];
	Uint8 localGradient[2][1024];
	Uint8 *globalGradient[2];
	bool locked[2]; //True if the building is not reachable.
	Uint32 lastGlobalGradientUpdateStepCounter[2];
	
	Uint8 *localRessources[2];
	int localRessourcesCleanTime[2]; // The time since the localRessources[x] has not been updated.
	int anyRessourceToClear[2]; // Which localRessources[x] gradient has any ressource. {0: unknow, 1:true, 2:false}

public:
	Building(GAGCore::InputStream *stream, BuildingsTypes *types, Team *owner, Sint32 versionMinor);
	Building(int x, int y, Uint16 gid, int typeNum, Team *team, BuildingsTypes *types);
	virtual ~Building(void);
	void freeGradients();
	
	void load(GAGCore::InputStream *stream, BuildingsTypes *types, Team *owner, Sint32 versionMinor);
	void save(GAGCore::OutputStream *stream);
	void loadCrossRef(GAGCore::InputStream *stream, BuildingsTypes *types, Team *owner);
	void saveCrossRef(GAGCore::OutputStream *stream);

	bool isRessourceFull(void);
	int neededRessource(void);
	void neededRessources(int needs[MAX_NB_RESSOURCES]);
	void wishedRessources(int needs[MAX_NB_RESSOURCES]);
	void computeWishedRessources();
	int neededRessource(int r);
	void launchConstruction(void);
	void cancelConstruction(void);
	void launchDelete(void);
	void cancelDelete(void);
	
	void updateClearingFlag(bool canSwim);
	void updateMatcher(void);
	void updateConstructionState(void);
	void updateBuildingSite(void);
	void update(void);
	
	void allocateNewlyFillingUnitsToResources();

	void setMapDiscovered(void);

	void getRessourceCountToRepair(int ressources[BASIC_COUNT]);

	bool tryToBuildingSiteRoom(void); //Returns true if room is found.
	
	bool isHardSpaceForBuildingSite(void);
	bool isHardSpaceForBuildingSite(ConstructionResultState constructionResultState);
	void step(void);
	void removeSubscribers(void);
	bool fullWorking(void);
	bool enoughWorking(void);
	bool fullInside(void);
	void subscribeToBringRessourcesStep(void);
	void subscribeForFlagingStep();
	void subscribeForInsideStep(void);
	void addUnitWorking(Unit *unit);
	void addUnitInside(Unit *unit);
	void removeUnitWorking(Unit *unit);
	void removeUnitInside(Unit *unit);
	
	void swarmStep(void);
	void turretStep(void);
	void clearingFlagsStep(void);
	void kill(void);

	int getMidX(void);
	int getMidY(void);
	int getMaxUnitStayRange(void);
	bool findGroundExit(int *posX, int *posY, int *dx, int *dy, bool canSwim);
	bool findAirExit(int *posX, int *posY, int *dx, int *dy);
	int getLongLevel(void);

	void computeFlagStatLocal(int *goingTo, int *onSpot);

	Uint32 eatOnce(Uint32 *mask=NULL);
	
	int availableHappynessLevel(bool guarantee);

	void integrity();
	Uint32 checkSum(std::vector<Uint32> *checkSumsVector);
	int verbose;

protected:
	FILE *logFile;
};

#endif
 
