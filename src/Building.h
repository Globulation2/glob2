/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

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
#include "Order.h"

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
	///This is the buildings basic state of existance.
	enum BuildingState
	{
		DEAD=0,
		ALIVE=1,
		WAITING_FOR_DESTRUCTION=2,
		WAITING_FOR_CONSTRUCTION=3,
		WAITING_FOR_CONSTRUCTION_ROOM=4
	};
	
	///If the building is undergoing any construction,
	///this state designates what
	enum ConstructionResultState
	{
		NO_CONSTRUCTION=0,
		NEW_BUILDING=1,
		UPGRADE=2,
		REPAIR=3
	};

public:
	Building(GAGCore::InputStream *stream, BuildingsTypes *types, Team *owner, Sint32 versionMinor);
	Building(int x, int y, Uint16 gid, Sint32 typeNum, Team *team, BuildingsTypes *types, Sint32 unitWorking, Sint32 unitWorkingFuture);
	virtual ~Building(void);
	void freeGradients();

	void load(GAGCore::InputStream *stream, BuildingsTypes *types, Team *owner, Sint32 versionMinor);
	void save(GAGCore::OutputStream *stream);
	void loadCrossRef(GAGCore::InputStream *stream, BuildingsTypes *types, Team *owner, Sint32 versionMinor);
	void saveCrossRef(GAGCore::OutputStream *stream);

	bool isRessourceFull(void);
	int neededRessource(void);
	void neededRessources(int needs[MAX_NB_RESSOURCES]);
	void wishedRessources(int needs[MAX_NB_RESSOURCES]);
	///Wished ressources are any ressources that are needed, and not being carried by a unit already.
	void computeWishedRessources();
	int neededRessource(int r);
	int totalWishedRessource();

	///Launches construction. Provided with the number of units that should be working during the construction,
	///and the number of units that should be working after the construction is finished.
	void launchConstruction(Sint32 unitWorking, Sint32 unitWorkingFuture);
	///Cancels construction of a building, returning it to a normal state.
	void cancelConstruction(Sint32 unitWorking);
	///Causes a building to be put on the waiting list for deletion. It is deleted by team after all units
	///that are inside the building leave.
	void launchDelete(void);
	///Cancels the deletion of a building, returning it to normal.
	void cancelDelete(void);
	
	///This function updates the call lists that the Building is on. A call list is a list
	///of buildings in Team that need units for work, or can have units "inside"
	void updateCallLists(void);
	///When a building is waiting for room, this will make sure that the building is in the 
	///Team::buildingsTryToBuildingSiteRoom list. It will also check for hardspace, etc if
	///ressources grow into the space or a building is placed, it becomes impossible
	///to upgrade and the construction is cancelled.
	void updateConstructionState(void);
	///Updates the construction state when undergoing construction. If the ressources are full,
	///construction has completed.
	void updateBuildingSite(void);
	///This function updates the units working at this building. If there are too many units, it
	///fires some.
	void updateUnitsWorking(void);
	///This function is called after important events in order to update the building
	void update(void);

	///Sets the area arround the building to be discovered, and visible by the building
	void setMapDiscovered(void);

	///Gets the ammount of ressources for each type of ressource that are needed to repair the building.
	void getRessourceCountToRepair(int ressources[BASIC_COUNT]);

	///Attempts to find room for a building site. If room is found, the building site is established,
	///and it returns true.
	bool tryToBuildingSiteRoom(void);

	///This function puts hidden forbidden area arround a new building site. This dispereses units so that
	///the building isn't waiting for space when there are lots of units.
	void addForbiddenZoneToUpgradeArea(void);
	///This function removes the hidden forbidden area placed by addForbiddenToUpgradeArea
	///It must be done before any type or position state is changed.
	void removeForbiddenZoneFromUpgradeArea(void);
	
	///Checks if there is hard space for a building. Non hard space is any space occupied by something that
	///won't move. Units will move, so they are ignored. If there is space for the building site, then this
	///returns true.
	bool isHardSpaceForBuildingSite(void);
	bool isHardSpaceForBuildingSite(ConstructionResultState constructionResultState);

	///Designates whether we are full inside. For Inns, takes into account how much wheat is left
	///and whether there is enough wheat for more units.
	bool fullInside(void);

	///This function tells the number of workers that should be working at this building.
	///If, for example, the building doesn't need any ressources, then this function will
	///return 0, because if its already full, it doesn't need any units.
	int desiredNumberOfWorkers(void);

	///This is called every step. The building updates the desiredMaxUnitWorking variable using
	///the function desiredNumberOfWorkers
	void step(void);
	///This function subscribes any building that needs ressources carried to it with units.
	///It is considered greedy, hiring as many units as it needs in order of its preference
	void subscribeToBringRessourcesStep(void);
	///This function subscribes any flag that needs units for a with units.
	///It is considered greedy, hiring as many units as it needs in order of its preference
	void subscribeForFlagingStep();
	/// Subscribes a unit to go inside the building.
	void subscribeUnitForInside(Unit* unit);
	///This is a step for swarms. Swarms heal themselves and create new units
	void swarmStep(void);
	/// This function searches for enemies, computes the best target, and fires a bullet
	void turretStep(void);
	/// Kills the building, removing all units that are working or inside the building,
	/// changing the state and adding it to the list of buildings to be deleted
	void kill(void);

	/// Tells whether a particular unit can work at this building. Takes into account this buildings level,
	/// the units type and level, and whether this building is a flag, because flags get a couple of special
	/// rules.
	bool canUnitWorkHere(Unit* unit);

	/// This function removes the unit from the list of units working on the building. Units will remove themselves
	/// when they run out of food, for example. This does not handle units state, just the buildings.
	void removeUnitFromWorking(Unit* unit);

	/// Remove unit from inside. This function removes the unit from being inside the building. Like removeUnitFromWorking,
	/// it does not update the units state.
	void removeUnitFromInside(Unit* unit);

	/// This function updates the ressources pointer. The variable ressources can either point to local ressources
	/// or team ressoureces, depending on the BuildingType. 
	void updateRessourcesPointer();
	
	/// This function is called when a Unit places a ressource into the building.
	void addRessourceIntoBuilding(int ressourceType);
	/// This function is called when a Unit takes a ressource from a building, such as a market
	void removeRessourceFromBuilding(int ressourceType);

	///Gets the middle x cordinate relative to posX
	int getMidX(void);
	///Gets the middle y cordinate relative to posY
	int getMidY(void);

	/// When a unit leaves a building, this function will find an open spot for that unit to leave,
	/// and provides the x and y cordinates, along with the direction the unit should be travelling
	/// when it leaves.
	bool findGroundExit(int *posX, int *posY, int *dx, int *dy, bool canSwim);
	/// When a unit leaves a building, this function will find an open spot for that unit to leave,
	/// and provides the x and y cordinates, along with the direction the unit should be travelling
	/// when it leaves.
	bool findAirExit(int *posX, int *posY, int *dx, int *dy);

	/// Returns the script level number. Construction sites are odd numbers and completed buildings
	/// even, from 0 to 5
	int getLongLevel(void);

	/// get flag from units attached to flag.
	void computeFlagStatLocal(int *goingTo, int *onSpot);

	/// Eats one wheat and one of each of the available fruit from the building.
	/// Return the number of differents fruits in this building. If mask is non-null,
	/// set masks value to the mask as well
	Uint32 eatOnce(Uint32 *mask=NULL);
	
	/// Returns the maximum happyness level that this building can provide, taking into account the
	/// units that are already in it.
	int availableHappynessLevel();

	static Sint32 GIDtoID(Uint16 gid);
	static Sint32 GIDtoTeam(Uint16 gid);
	static Uint16 GIDfrom(Sint32 id, Sint32 team);

	void integrity();
	Uint32 checkSum(std::vector<Uint32> *checkSumsVector);
	int verbose;
	std::list<Order *> orderQueue;
	
public:
	// type
	Sint32 typeNum; // number in BuildingTypes
	///This is the typenum from IntBuildingType
	int shortTypeNum;
	BuildingType *type;

	// construction state
	BuildingState buildingState;
	ConstructionResultState constructionResultState;

	// units
	Sint32 maxUnitWorkingLocal;
	Sint32 maxUnitWorking;  // (Uint16)
	Sint32 maxUnitWorkingFuture;
	Sint32 maxUnitWorkingPreferred;
	///This is a constantly updated number that indicates the buildings desired number of units,
	///say for example that the building is full, it needs no units, so this is 0
	Sint32 desiredMaxUnitWorking;
	///This is the list of units activly working on the building.
	std::list<Unit *> unitsWorking;
	///The subscribeToBringRessourcesStep and subscribeForFlagingStep operate every 32 ticks
	Sint32 subscriptionWorkingTimer;
	Sint32 maxUnitInside;
	std::list<Unit *> unitsInside;
	Sint32 clearingFlagUpdateTimer;
	
	// optimisation and consistency
	Sint32 canFeedUnit; // Included in {0: unknow, 1:allready in owner->canFeedUnit, 2:not in owner->canFeedUnit}
	Sint32 canHealUnit; // Included in {0: unknow, 1:allready in owner->canHealUnit, 2:not in owner->canHealUnit}
	Sint32 upgrade[NB_ABILITY]; // Included in {0: unknow, 1:allready in owner->upgrade[i], 2:not in owner->upgrade[i]}
	/// This variable indicates whether this building is already in the team call list
	/// to recieve units. A 1 indicates its already in the call list, and 0 indicates
	/// that it is not.
	Uint8 callListState;

	// identity
	Uint16 gid; // for reservation see GIDtoID() and GIDtoTeam().
	Team *owner;

	// position
	Sint32 posX, posY; // (Uint16)
	Sint32 posXLocal, posYLocal;

	// Flag usefull :
	Sint32 unitStayRange; // (Uint8)
	Sint32 unitStayRangeLocal;
	bool clearingRessources[BASIC_COUNT]; // true if the ressource has to be cleared.
	bool clearingRessourcesLocal[BASIC_COUNT];
	Sint32 minLevelToFlag;
	Sint32 minLevelToFlagLocal;

	// Building specific :
	/// Ammount stocked, or used for building building. Local ressources stores the ressources this particular building contains
	/// in the event that the building type designates using global ressources instead of local ressources, the ressources pointer
	/// will be changed to point to the global ressources Team::teamRessources instead of localRessources.
	Sint32* ressources;
	Sint32 wishedResources[MAX_NB_RESSOURCES];
	Sint32 localRessource[MAX_NB_RESSOURCES];

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

protected:
	FILE *logFile;
};

#endif
 
