// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <climits>
#include <list>
#include <vector>

#include "BuildingUtils.h"
#include "MapInternal.h"
#include "Ressource.h"
#include "UnitConsts.h"

namespace GAGCore
{
	class InputStream;
	class OutputStream;
}


class Unit;
class Team;
struct BuildingType;
class BuildingsTypes;
class Order;

/// Sentinel for `BuildingType::prevLevel` / `nextLevel` meaning
/// "no upgrade/downgrade exists in this chain" (signed int).
/// Distinct from the wire-format `NB_BUILDING` size sentinel.
static constexpr int BUILDING_LEVEL_NONE = -1;

/// Sentinel for "no resource type chosen yet" on signed integers
/// (e.g. `Building::neededRessource()` return, `bestResource` in
/// scoring loops). Distinct from `NO_RES_TYPE` (Uint8 0xFF) used
/// on the `Ressource` value-type field.
static constexpr int RESSOURCE_TYPE_NONE = -1;

class Building : public BuildingUtils
{
public:
	static const int MAX_COUNT=1024;

	/// `lastShootStep = LAST_SHOOT_STEP_NEVER` means this turret has
	/// not fired yet this game; the field is `Uint32` step counter.
	static constexpr Uint32 LAST_SHOOT_STEP_NEVER = static_cast<Uint32>(-1);

	/// Initial value for proportion-finding loops in `neededRessource`
	/// and `swarmStep`: every real proportion compares less. Same as
	/// `INT32_MAX`; named for clarity at the call site.
	static constexpr Sint32 MIN_PROPORTION_INIT = INT32_MAX;

	/// Wished-resources scaling factor: `wishedResources = (NUM/DEN) *
	/// missing` ≈ 1.33×, so workers can be subscribed before resources
	/// are actually depleted.
	static constexpr int WISHED_RESOURCE_NUM = 4;
	static constexpr int WISHED_RESOURCE_DEN = 3;

	/// `findGroundExit` quality scoring (per-tile bonuses):
	///  - +1 when the candidate exit is next to a ressource
	///  - +2 when the candidate exit is on open ground
	/// Search aborts once any side reaches `EXIT_QUALITY_GOOD_ENOUGH`.
	static constexpr int EXIT_QUALITY_NEAR_RESSOURCE = 1;
	static constexpr int EXIT_QUALITY_OPEN_GROUND = 2;
	static constexpr int EXIT_QUALITY_GOOD_ENOUGH = 4;

	/// Sanity bound on `maxUnitInside` reads from old saves; the value
	/// is logically a `Uint16` so anything ≥ 65536 is corrupt data.
	static constexpr Sint32 MAX_UNIT_INSIDE_LIMIT = 65536;

	/// Turret rotating-shoot sprite has this many animation frames;
	/// `shootingStep` cycles `0..SHOOTING_ANIMATION_FRAMES-1`.
	static constexpr Uint32 SHOOTING_ANIMATION_FRAMES = 8;

	/// Turret types are required to be `TURRET_SIZE × TURRET_SIZE`
	/// tiles. Bullet-spawn math (e.g. `<<4` half-tile offsets) bakes
	/// in this assumption.
	static constexpr int TURRET_SIZE = 2;

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

	// ─── Public methods ─────────────────────────────────────────────

	Building(GAGCore::InputStream *stream, BuildingsTypes *types, Team *owner, Sint32 versionMinor);
	Building(int x, int y, Uint16 gid, Sint32 typeNum, Team *team, BuildingsTypes *types, Sint32 unitWorking, Sint32 unitWorkingFuture);
	virtual ~Building(void);
	void freeGradients();
	// Drop both pathfinding buffers (call after the building moves or its range changes).
	void resetPathfindGradients();
	// Drop only the local-ressources buffer (call when a tile inside the footprint changes).
	void resetLocalRessources();

	void load(GAGCore::InputStream *stream, BuildingsTypes *types, Team *owner, Sint32 versionMinor);
	void save(GAGCore::OutputStream *stream);
	void loadCrossRef(GAGCore::InputStream *stream, BuildingsTypes *types, Team *owner, Sint32 versionMinor);
	void saveCrossRef(GAGCore::OutputStream *stream);

	bool isRessourceFull(void);
	int neededRessource(void);
	/**
	 * calls neededRessource(int res) for all possible ressources.
	 * @param array of needs that will be filled by this function
	 */
	void neededRessources(int needs[MAX_NB_RESSOURCES]);
	/**
	 * @param res The resource type
	 * @return count of resources needed of type res. In case of higher multiplicity
	 * of the requested resource (fruits have 10) the value is reduced by (multiplicity-1)
	 * and clipped to >= 0
	 */
	int neededRessource(int res);
	///Wished ressources are any ressources that are needed, and not being carried by a unit already.
	///Fills `needs[]` with the result; pass `wishedResources` to refresh the cached member.
	void computeWishedRessources(int needs[MAX_NB_RESSOURCES]);
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

	///Sets the area around the building to be discovered, and visible by the building
	void setMapDiscovered(void);

	///Gets the amount of ressources for each type of ressource that are needed to repair the building.
	void getRessourceCountToRepair(int ressources[BASIC_COUNT]);

	///Attempts to find room for a building site. If room is found, the building site is established,
	///and it returns true.
	bool tryToBuildingSiteRoom(void);

	///Checks if there is hard space for a building site under the given construction result state.
	///Non hard space is any space occupied by something that won't move. Units will move, so they
	///are ignored. If there is space for the building site, then this returns true. The no-arg
	///private overload defers to this one using the building's current `constructionResultState`.
	bool isHardSpaceForBuildingSite(ConstructionResultState requestedState);

	///This is called every step. The building updates the desiredMaxUnitWorking variable using
	///the function desiredNumberOfWorkers
	void step(void);
	///This function subscribes any building that needs ressources carried to it with units.
	///It is considered greedy, hiring as many units as it needs in order of its preference
	///Returns true if a unit was hired
	bool subscribeToBringRessourcesStep(void);
	///This function subscribes any flag that needs units for a with units.
	///It is considered greedy, hiring as many units as it needs in order of its preference
	///Returns true if a unit was hired
	bool subscribeForFlagingStep();
	/// Subscribes a unit to go inside the building.
	void subscribeUnitForInside(Unit* unit);
	/// This is a step for swarms. Swarms heal themselves and create new units
	void swarmStep(void);
	/// This function searches for enemies, computes the best target, and fires a bullet
	void turretStep(Uint32 stepCounter);
	/// This step updates clearing flag gradients. When there are no more ressources remaining, units are to
	/// be fired. When ressources grow back, units have to be rehired.=
	void clearingFlagStep();
	/// Kills the building, removing all units that are working or inside the building,
	/// changing the state and adding it to the list of buildings to be deleted
	void kill(void);

	/// This function removes the unit from the list of units working on the building. Units will remove themselves
	/// when they run out of food, for example. This does not handle units state, just the buildings.
	void removeUnitFromWorking(Unit* unit);

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

	/// This function is called when a Unit places a ressource into the building.
	void addRessourceIntoBuilding(int ressourceType);

	/// This function is called when a Unit takes a ressource from a building, such as a market
	void removeRessourceFromBuilding(int ressourceType);

	///Gets the middle x cordinate relative to posX
	int getMidX(void);
	///Gets the middle y cordinate relative to posY
	int getMidY(void);

	/// When a unit leaves a building, this function will find an open spot for that unit to leave,
	/// and provides the x and y coordinates, along with the direction the unit should be travelling
	/// when it leaves.
	bool findGroundExit(int *posX, int *posY, int *dx, int *dy, bool canSwim);
	/// When a unit leaves a building, this function will find an open spot for that unit to leave,
	/// and provides the x and y coordinates, along with the direction the unit should be travelling
	/// when it leaves.
	bool findAirExit(int *posX, int *posY, int *dx, int *dy);

	/// Returns the script level number. Construction sites are odd numbers and completed buildings
	/// even, from 0 to 5
	int getLongLevel(void);

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

private:
	// ─── Private helper methods ─────────────────────────────────────

	/// This function updates the units harvesting at this building. In
	/// particular, it unsubscribes them when the building is being destroyed or
	/// turns invisible when for example the other teams switches the view for
	/// its markets.
	void updateUnitsHarvesting(void);

	///This function puts hidden forbidden area around a new building site. This dispereses units so that
	///the building isn't waiting for space when there are lots of units.
	void addForbiddenZoneToUpgradeArea(void);
	///This function removes the hidden forbidden area placed by addForbiddenToUpgradeArea
	///It must be done before any type or position state is changed.
	void removeForbiddenZoneFromUpgradeArea(void);
	///Shared body for add/remove. `add=true` adds the zone, `add=false` removes it.
	void modifyForbiddenZoneForUpgradeArea(bool add);

	///No-arg overload: defers to the public `isHardSpaceForBuildingSite(requestedState)` using the
	///building's current `constructionResultState`.
	bool isHardSpaceForBuildingSite(void);

	///Designates whether we are full inside. For Inns, takes into account how much wheat is left
	///and whether there is enough wheat for more units.
	bool fullInside(void);

	///This function tells the number of workers that should be working at this building.
	///If, for example, the building doesn't need any ressources, then this function will
	///return 0, because if its already full, it doesn't need any units.
	int desiredNumberOfWorkers(void);

	/// Tells whether a particular unit can work at this building. Takes into account this buildings level,
	/// the units type and level, and whether this building is a flag, because flags get a couple of special
	/// rules.
	bool canUnitWorkHere(Unit* unit);

	/// This function updates the ressources pointer. The variable ressources can either point to local ressources
	/// or team resources, depending on the BuildingType.
	void updateRessourcesPointer();

	/// checkstyle found this block of 26 lines being repeated 4 times.
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

	static std::string getBuildingName(int type);

public:
	// ─── Public data ────────────────────────────────────────────────

	int verbose;

	// type
	Sint32 typeNum; // number in BuildingTypes
	///This is the typenum from IntBuildingType
	int shortTypeNum;
	BuildingType *type;

	// construction state
	BuildingState buildingState;
	ConstructionResultState constructionResultState;

	// units
	Sint32 maxUnitWorking;  // (Uint16)
	Sint32 maxUnitWorkingPreferred;
	///This is a constantly updated number that indicates the buildings desired number of units,
	///say for example that the building is full, it needs no units, so this is 0
	Sint32 desiredMaxUnitWorking;
	///This is the list of units actively working on the building.
	std::list<Unit *> unitsWorking;
	Sint32 maxUnitInside;
	///This counts the number of units that failed the requirements for the building, but where free
	std::list<Unit *> unitsInside;
	///This stores the priority of the building, 0 is normal, -1 is low, +1 is high
	Sint32 priority;
	Sint32 priorityLocal;

	// identity
	Uint16 gid; // for reservation see GIDtoID() and GIDtoTeam().
	Team *owner;

	// position
	Sint32 posX, posY; // (Uint16)

	// Counts down 240 frames from when a unit was attacked
	Uint8 underAttackTimer;


	// Flag usefull :
	Sint32 unitStayRange; // (Uint8)
	bool clearingRessources[BASIC_COUNT]; // true if the ressource has to be cleared.
	bool clearingRessourcesLocal[BASIC_COUNT];
	Sint32 minLevelToFlag;
	Sint32 minLevelToFlagLocal;

	// Building specific :
	/// Amount stocked, or used for building building. Local ressources stores the ressources this particular building contains
	/// in the event that the building type designates using global ressources instead of local ressources, the ressources pointer
	/// will be changed to point to the global ressources Team::teamRessources instead of localRessources.
	Sint32* ressources;
	Sint32 wishedResources[MAX_NB_RESSOURCES];

	// quality parameters
	Sint32 hp; // (Uint16)

	// swarm building parameters
	Sint32 productionTimeout;
	Sint32 ratio[NB_UNIT_TYPE];
	Sint32 ratioLocal[NB_UNIT_TYPE];

	// exchange building parameters
	Uint32 receiveRessourceMask;
	Uint32 sendRessourceMask;
	Uint32 receiveRessourceMaskLocal;
	Uint32 sendRessourceMaskLocal;

	// turrets building parameters
	Sint32 bullets;

	// A true bit meant that the corresponding team can see this building, under FOW or not.
	Uint32 seenByMask;

	bool dirtyLocalGradient[2];
	Uint8 localGradient[2][LOCAL_GRID_AREA];
	Uint8 *globalGradient[2];
	bool locked[2]; //True if the building is not reachable.
	Uint32 lastGlobalGradientUpdateStepCounter[2];

	Uint8 *localRessources[2];
	int localRessourcesCleanTime[2]; // The time since the localRessources[x] has not been updated.
	int anyRessourceToClear[2]; // Which localRessources[x] gradient has any ressource. {0: unknow, 1:true, 2:false}

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

private:
	// ─── Private data ───────────────────────────────────────────────

	// pending player orders (consumed by step())
	std::list<Order *> orderQueue;

	// units: scratch counters for subscription / priority diff
	Sint32 maxUnitWorkingFuture;
	Sint32 maxUnitWorkingPrevious;
	///The subscribeToBringRessourcesStep and subscribeForFlagingStep operate every 32 ticks
	Sint32 subscriptionWorkingTimer;
	///This stores the old priority, so that if the priority changes, this building will be updated in Teams
	Sint32 oldPriority;

	///This is the list of units harvesting from the building (if it is a market for instance)
	std::list<Unit *> unitsHarvesting;

	// optimisation and consistency
	InListState inCanFeedUnit;
	Uint8 canNotConvertUnitTimer; //counts down 150 frames after the building was last unable to feed a unit
	InListState inCanHealUnit;
	InListState inUpgrade[NB_ABILITY];
	/// This variable indicates whether this building is already in the team call list
	/// to receive units. A 1 indicates its already in the call list, and 0 indicates
	/// that it is not.
	Uint8 callListState;

	// Building specific (private):
	Sint32 localRessource[MAX_NB_RESSOURCES];

	// swarm building parameters (private):
	Sint32 totalRatio;
	Sint32 percentUsed[NB_UNIT_TYPE];

	// turrets building parameters (private):
	Uint32 shootingStep;
	Sint32 shootingCooldown;
};
