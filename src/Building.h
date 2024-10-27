/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
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

#include "BuildingUtils.h"
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
class Order;

class Building : public BuildingUtils
{
public:
	static const int MAX_COUNT=1024;
	///This is the buildings basic state of existence.
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

	///The state of a unit in certain lists.
	enum InListState
	{
		LS_UNKNOWN=0,
		LS_IN=1,
		LS_OUT=2
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

	bool isResourceFull(void);
	int neededResource(void);
	/**
	 * calls neededResource(int res) for all possible resources.
	 * @param array of needs that will be filled by this function
	 */
	void neededResources(int needs[MAX_NB_RESOURCES]);
	void computedNeededResources(int needs[MAX_NB_RESOURCES]);
	/**
	 * @param res The resource type
	 * @return count of resources needed of type res. In case of higher multiplicity
	 * of the requested resource (fruits have 10) the value is reduced by (multiplicity-1)
	 * and clipped to >= 0
	 */
	int neededResource(int res);
	///Wished resources are any resources that are needed, and not being carried by a unit already.
	void computeWishedResources();
	int totalWishedResource();

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
	///Team::buildingsTryToBuildingSiteRoom list. It will also check for hard space, etc if
	///resources grow into the space or a building is placed, it becomes impossible
	///to upgrade and the construction is cancelled.
	void updateConstructionState(void);
	///Updates the construction state when undergoing construction. If the resources are full,
	///construction has completed.
	void updateBuildingSite(void);
	///This function updates the units working at this building. If there are too many units, it
	///fires some.
	void updateUnitsWorking(void);

	/// This function updates the units harvesting at this building. In
	/// particular, it unsubscribes them when the building is being destroyed or
	/// turns invisible when for example the other teams switches the view for
	/// its markets.
private:void updateUnitsHarvesting(void);

	///This function is called after important events in order to update the building
public:void update(void);

	///Sets the area around the building to be discovered, and visible by the building
	void setMapDiscovered(void);

	///Gets the amount of resources for each type of resource that are needed to repair the building.
public:void getResourceCountToRepair(int resources[BASIC_COUNT]);

	///Attempts to find room for a building site. If room is found, the building site is established,
	///and it returns true.
	bool tryToBuildingSiteRoom(void);

	///This function puts hidden forbidden area around a new building site. This disperses units so that
	///the building isn't waiting for space when there are lots of units.
private:void addForbiddenZoneToUpgradeArea(void);
	///This function removes the hidden forbidden area placed by addForbiddenToUpgradeArea
	///It must be done before any type or position state is changed.
	void removeForbiddenZoneFromUpgradeArea(void);

	///Checks if there is hard space for a building. Non hard space is any space occupied by something that
	///won't move. Units will move, so they are ignored. If there is space for the building site, then this
	///returns true.
	bool isHardSpaceForBuildingSite(void);
public:bool isHardSpaceForBuildingSite(ConstructionResultState constructionResultState);

	///Designates whether we are full inside. For Inns, takes into account how much wheat is left
	///and whether there is enough wheat for more units.
private:bool fullInside(void);

	///This function tells the number of workers that should be working at this building.
	///If, for example, the building doesn't need any resources, then this function will
	///return 0, because if its already full, it doesn't need any units.
	int desiredNumberOfWorkers(void);

	///This is called every step. The building updates the desiredMaxUnitWorking variable using
	///the function desiredNumberOfWorkers.
public:void step(void);
	///This function subscribes any building that needs resources carried to it by units.
	///It is considered greedy, hiring as many units as it needs in the order of its preference.
	///Returns true if a unit was hired
	bool subscribeToBringResourcesStep(void);
	///This function subscribes any flag that needs units.
	///It is considered greedy, hiring as many units as it needs in the order of its preference.
	///Returns true if a unit was hired
	bool subscribeForFlagingStep();
	/// Subscribes a unit to go inside the building.
	void subscribeUnitForInside(Unit* unit);
	/// This is a step for swarms. Swarms heal themselves and create new units.
	void swarmStep(void);
	/// This function searches for enemies, computes the best target, and fires a bullet.
	void turretStep(Uint32 stepCounter);
	/// This step updates clearing flag gradients. When there are no more resources remaining, units are to
	/// be fired. When resources grow back, units have to be rehired.
	void clearingFlagStep();
	/// Kills the building, removing all units that are working or inside the building,
	/// changing the state and adding it to the list of buildings to be deleted.
	void kill(void);

	/// Tells whether a particular unit can work at this building. Takes into account this buildings level,
	/// the units type and level, and whether this building is a flag, because flags get a couple of special
	/// rules.
private:bool canUnitWorkHere(Unit* unit);

	/// This function removes the unit from the list of units working on the building. Units will remove themselves
	/// when they run out of food, for example. This does not handle units state, just the buildings.
public:void removeUnitFromWorking(Unit* unit);
	
	/// Insert into the harvesting unit, when the unit has decided to do so.
	/// This does not handle units state, just the buildings.
	void insertUnitToHarvesting(Unit* unit);
	
	/// This function removes the unit from the list of units harvesting from the building. Units will remove themselves
	/// when they run out of food, for example. This does not handle units state, just the buildings.
	/// It is safe to call this function even if the unit is not harvesting at the building.
	void removeUnitFromHarvesting(Unit* unit);

	/// Remove unit from inside. This function removes the unit from being inside the building. Like removeUnitFromWorking,
	/// it does not update the units state.
	void removeUnitFromInside(Unit* unit);

	/// This function updates the resources pointer. The variable resources can either point to local resources
	/// or team resources, depending on the BuildingType.
private:void updateResourcesPointer();

	/// This function is called when a Unit places a resource into the building.
public:void addResourceIntoBuilding(int resourceType);
	
	/// This function is called when a Unit takes a resource from a building, such as a market
	void removeResourceFromBuilding(int resourceType);

	///Gets the middle x coordinate relative to posX
	int getMidX(void);
	///Gets the middle y coordinate relative to posY
	int getMidY(void);

	/// When a unit leaves a building, this function will find an open spot for that unit to leave,
	/// and provides the x and y coordinates, along with the direction the unit should be traveling
	/// when it leaves.
	bool findGroundExit(int *posX, int *posY, int *dx, int *dy, bool canSwim);
	/// When a unit leaves a building, this function will find an open spot for that unit to leave,
	/// and provides the x and y coordinates, along with the direction the unit should be traveling
	/// when it leaves.
	bool findAirExit(int *posX, int *posY, int *dx, int *dy);
private:
	/// check style found this block of 26 lines being repeated 4 times.
	void checkGroundExitQuality(
		const int testX,
		const int testY,
		const int extraTestX,
		const int extraTestY,
		int & exitX,
		int & exitY,
		int & exitQuality,
		int & oldQuality,
		bool canSwim);
	/// Returns the script level number. Construction sites are odd numbers and completed buildings
	/// even, from 0 to 5
public:int getLongLevel(void);

	/// get flag from units attached to flag.
	void computeFlagStatLocal(int *goingTo, int *onSpot);

	/// Eats one wheat and one of each of the available fruit from the building.
	/// Return the number of different fruits in this building. If mask is non-null,
	/// set masks value to the mask as well
	Uint32 eatOnce(Uint32 *mask=NULL);

	/// Returns the maximum happyness level that this building can provide, taking into account the
	/// units that are already in it.
	int availableHappynessLevel();

	/// Returns if this Building (Inn) can convert a hostile Unit. To avoid conversion
	/// once the capacities of the own inns are hit, conversion is limited.
	bool canConvertUnit(void);

	bool integrity();
	Uint32 checkSum(std::vector<Uint32> *checkSumsVector);
int verbose;
private:std::list<Order *> orderQueue;

	static std::string getBuildingName(int type);
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
private:Sint32 maxUnitWorkingFuture;
public:Sint32 maxUnitWorkingPreferred;
private:Sint32 maxUnitWorkingPrevious;
	///This is a constantly updated number that indicates the buildings desired number of units,
	///say for example that the building is full, it needs no units, so this is 0
public:Sint32 desiredMaxUnitWorking;
	///This is the list of units actively working on the building.
	std::list<Unit *> unitsWorking;
	///The subscribeToBringResourcesStep and subscribeForFlagingStep operate every 32 ticks
private:Sint32 subscriptionWorkingTimer;
public:Sint32 maxUnitInside;
	///This counts the number of units that failed the requirements for the building, but where free
	std::list<Unit *> unitsInside;
	///This stores the priority of the building, 0 is normal, -1 is low, +1 is high
	Sint32 priority;
	Sint32 priorityLocal;
	///This stores the old priority, so that if the priority changes, this building will be updated in Teams
private:Sint32 oldPriority;

	///This is the list of units harvesting from the building (if it is a market for instance)
private:std::list<Unit *> unitsHarvesting;
	
private:
	// optimisation and consistency
	InListState inCanFeedUnit;
	Uint8 canNotConvertUnitTimer; //counts down 150 frames after the building was last unable to feed a unit
	InListState inCanHealUnit;
	InListState inUpgrade[NB_ABILITY];
	/// This variable indicates whether this building is already in the team call list
	/// to receive units. A 1 indicates its already in the call list, and 0 indicates
	/// that it is not.
	Uint8 callListState;

public:
	// identity
	Uint16 gid; // for reservation see GIDtoID() and GIDtoTeam().
	Team *owner;

	// position
	Sint32 posX, posY; // (Uint16)
	Sint32 posXLocal, posYLocal;

	// Counts down 240 frames from when a unit was attacked
	Uint8 underAttackTimer;


	// Flag useful :
	Sint32 unitStayRange; // (Uint8)
	Sint32 unitStayRangeLocal;
	bool clearingResources[BASIC_COUNT]; // true if the resource has to be cleared.
	bool clearingResourcesLocal[BASIC_COUNT];
	Sint32 minLevelToFlag;
	Sint32 minLevelToFlagLocal;

	// Building specific :
	/// Amount stocked, or used for building building. Local resources stores the resources this particular building contains
	/// in the event that the building type designates using global resources instead of local resources, the resources pointer
	/// will be changed to point to the global resources Team::teamResources instead of localResources.
	Sint32* resources;
	Sint32 wishedResources[MAX_NB_RESOURCES];
private:Sint32 localResource[MAX_NB_RESOURCES];

	// quality parameters
public:Sint32 hp; // (Uint16)

	// swarm building parameters
	Sint32 productionTimeout;
private:Sint32 totalRatio;
public:Sint32 ratio[NB_UNIT_TYPE];
	Sint32 ratioLocal[NB_UNIT_TYPE];
private:Sint32 percentUsed[NB_UNIT_TYPE];

	// exchange building parameters
public:Uint32 receiveResourceMask;
	Uint32 sendResourceMask;
	Uint32 receiveResourceMaskLocal;
	Uint32 sendResourceMaskLocal;

	// turrets building parameters
private:Uint32 shootingStep;
private:Sint32 shootingCooldown;
public:Sint32 bullets;

	// A true bit meant that the corresponding team can see this building, under FOW or not.
	Uint32 seenByMask;

	bool dirtyLocalGradient[2];
	Uint8 localGradient[2][1024];
	Uint8 *globalGradient[2];
	bool locked[2]; //True if the building is not reachable.
	Uint32 lastGlobalGradientUpdateStepCounter[2];

	Uint8 *localResources[2];
	int localResourcesCleanTime[2]; // The time since the localResources[x] has not been updated.
	int anyResourceToClear[2]; // Which localResources[x] gradient has any resource. {0: unknow, 1:true, 2:false}

	// shooting eye-candy data, not net synchronised
	Uint32 lastShootStep;
	Sint32 lastShootSpeedX;
	Sint32 lastShootSpeedY;


	enum UnitCantWorkReason
	{
		UnitNotAvailable=0,
		UnitTooLowLevel=1,
		UnitCantAccessBuilding=2,
		UnitTooFarFromBuilding=3,
		UnitCantAccessResource=4,
		UnitCantAccessFruit=5,
		UnitTooFarFromResource=6,
		UnitTooFarFromFruit=7,
		UnitCantWorkReasonSize,
	};

	Uint32 unitsFailingRequirements[UnitCantWorkReasonSize];

protected:
	FILE *logFile;
};

#endif

