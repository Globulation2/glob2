/*
 * Globulation 2 building
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#ifndef __BUILDING_H
#define __BUILDING_H

#include "GAG.h"
#include "Unit.h"
#include <list>
#include <vector>

#define SHOOTING_COOLDOWN_MAX 65536

class Team;
class BuildingType;
class BuildingsTypes;

class Building
{
public:

	enum BuildingState
	{
		DEAD=0,
		ALIVE,
		WAITING_FOR_DESTRUCTION,
		WAITING_FOR_UPGRADE,
		WAITING_FOR_UPGRADE_ROOM,
	};

	// type
	Uint32 typeNum; // number in BuildingTypes
	BuildingType *type;

	// construction state
	BuildingState buildingState;

	// units
	Sint32 maxUnitWorkingLocal;
	Sint32 maxUnitWorking;  // (Uint16)
	Sint32 maxUnitWorkingPreferred;
	std::list<Unit *> unitsWorking;
	Uint32 maxUnitInside;
	std::list<Unit *> unitsInside;
	
	// identity
	Sint32 UID; // Sint16, for reservation see below
	Team *owner; // if < 0, not allocated

	// position
	Sint32 posX, posY; // (Uint16)

	// UID Buildings and flags = -1 .. -16384 = - 1 - team * 512 - ID

	// 256 flags
	// Flag usefull :
	Uint32 unitStayRange; // (Uint8)
	Uint32 unitStayRangeLocal;

	// 512 buldings
	// Building specific :
	Sint32 ressources[NB_RESSOURCES]; // Ammount stocked, or used for building building.

	// quality parameters
	Sint32 hp; // (Uint16)

	// prefered parameters
	Sint32 productionTimeout;
	Sint32 totalRatio;
	Sint32 ratio[UnitType::NB_UNIT_TYPE];
	Sint32 ratioLocal[UnitType::NB_UNIT_TYPE];
	Sint32 percentUsed[UnitType::NB_UNIT_TYPE];
	
	Uint32 shootingStep;
	Sint32 shootingCooldown;

	// optimisation parameters
	Sint32 closestRessourceX[NB_RESSOURCES], closestRessourceY[NB_RESSOURCES];

public:
	Building(SDL_RWops *stream, BuildingsTypes *types, Team *owner);
	Building(int x, int y, int uid, int typeNum, Team *team, BuildingsTypes *types);
	virtual ~Building(void) { }
	
	void load(SDL_RWops *stream, BuildingsTypes *types, Team *owner);
	void save(SDL_RWops *stream);
	void loadCrossRef(SDL_RWops *stream, BuildingsTypes *types, Team *owner);
	void saveCrossRef(SDL_RWops *stream);

	bool isRessourceFull(void);
	int neededRessource(void);
	void cancelUpgrade(void);
	void update(void);
	bool tryToUpgradeRoom(void);
	bool isHardSpace(void);
	void step(void);
	void swarmStep(void);
	void turretStep(void);
	void kill(void);
	
	int getMidX(void);
	int getMidY(void);
	bool findExit(int *posX, int *posY, int *dx, int *dy, bool canFly);
	
	static Sint32 UIDtoID(Sint32 uid);
	static Sint32 UIDtoTeam(Sint32 uid);
	static Sint32 UIDfrom(Sint32 id, Sint32 team);
	
	Sint32 checkSum();
};

class Bullet
{
public:
	Bullet(SDL_RWops *stream);
	Bullet(Sint32 px, Sint32 py, Sint32 speedX, Sint32 speedY, Sint32 ticksLeft, Sint32 shootDamage, Sint32 targetX, Sint32 targetY);
	void load(SDL_RWops *stream);
	void save(SDL_RWops *stream);
public:
	Sint32 px, py; // pixel precision point of x,y
	Sint32 speedX, speedY; //pixel precision speed.
	Sint32 ticksLeft;
	Sint32 shootDamage;
	Sint32 targetX, targetY;
public:
	void step(void);
};

#endif
 
