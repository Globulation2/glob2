/*
* Globulation 2 building type
* (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
*/

#ifndef __BULDING_TYPE_H
#define __BULDING_TYPE_H

#include <vector>
#include "EntityType.h"
#include "Building.h"
#include "Unit.h"

class BuildingType: public EntityType
{
public:
	

	enum BuildingTypeNumber
	{
		SWARM_BUILDING=0,
		FOOD_BUILDING=1,
		HEALTH_BUILDING=2,

		WALKSPEED_BUILDING=3,
		FLYSPEED_BUILDING=4,
		ATTACK_BUILDING=5,
		SCIENCE_BUILDING=6,

		DEFENSE_BUILDING=7,
		
		EXPLORATION_FLAG=8,
		WAR_FLAG=9,

		NB_BUILDING
	};

//	Uint32 __STARTDATA[0];
#define __STARTDATA_B ((Uint32*)&startImage)

	// visualisation
	Sint32 startImage;
	Sint32 hueImage; // bool. The way we show the building's team (false=we draw a flag, true=we hue all the sprite)
	Sint32 flagImage;
	//Sint32 nbDifferentImages;
	//Sint32 timeBetweenImages;

	// could be Uint8, if non 0 tell the number of maximum units locked by bulding for:
	// by order of priority (top = max)
	Sint32 upgrade[NB_ABILITY]; // What kind on units can be upgraded here
	Sint32 upgradeTime[NB_ABILITY]; // Time to upgrade an unit, given the upgrade type needed.
	Sint32 job[NB_ABILITY]; // If an unit is required for a job.
	Sint32 attract[NB_ABILITY]; // If an unit is required for a presence.
	Sint32 canFeedUnit;
	Sint32 timeToFeedUnit;
	Sint32 canHealUnit;
	Sint32 timeToHealUnit;
	Sint32 insideSpeed;

	Sint32 width, height; // Uint8, size in square
	Sint32 decLeft, decTop;
	Sint32 isVirtual; // bool, doesn't occupy ground occupation map, used for war-flag and exploration-flag.
	Sint32 isCloacked; // bool, graphicaly invisible for enemi.
	//Sint32 *walkOverMap; // should be allocated and deleted in a cleany way
	//Sint32 walkableOver; // bool, can walk over
	Sint32 shootingRange; // Uint8, if 0 can't shoot
	Sint32 shootDamage; // Uint8
	Sint32 shootSpeed; // Uint8
	Sint32 shootRythme; // Uint8

	Sint32 unitProductionTime; // Uint8, nb tick to produce one unit
	Sint32 ressourceForOneUnit;
	
	Sint32 maxRessource[NB_RESSOURCES];
	Sint32 maxUnitInside;
	Sint32 maxUnitWorking;

	Sint32 hpInit; // (Uint16)
	Sint32 hpMax;
	Sint32 hpInc;
	Sint32 armor; // (Uint8)
	Sint32 level; // (Uint8)
	Sint32 type; // BuildingTypeNumber, Should not be used by the main engine, but only to choose the next level building.
	Sint32 isBuildingSite; // (bool, 0=false, 1=true)

	// Flag usefull
	Sint32 defaultUnitStayRange;
	
	// Number to access next upgrade in BuildingsTypes.
	// It is computed in the second phase of the BuildingsTypes constructor after all building are read from disk.
	Sint32 nextLevelTypeNum;
	Sint32 typeNum;
	Sint32 lastLevelTypeNum;

public:
	BuildingType() { init(); }
	BuildingType(SDL_RWops *stream) { load(stream); }
	virtual ~BuildingType() { }
	virtual const char **getVars(int *size, Uint32 **data)
	{
		static const char *vars[] =
		{
			"startImage",
			"hueImage",
			"flagImage",

			"upgradeStopWalk",
			"upgradeStopSwim",
			"upgradeStopFly",
			"upgradeWalk",
			"upgradeSwim",
			"upgradeFly",
			"upgradeBuild",
			"upgradeHarvest",
			"upgradeAttackSpeed",
			"upgradeAttackStrength",
			"upgradeArmor",
			"upgradeHP",

			"upgradeTimeStopWalk",
			"upgradeTimeStopSwim",
			"upgradeTimeStopFly",
			"upgradeTimeWalk",
			"upgradeTimeSwim",
			"upgradeTimeFly",
			"upgradeTimeBuild",
			"upgradeTimeHarvest",
			"upgradeTimeAttackSpeed",
			"upgradeTimeAttackStrength",
			"upgradeTimeArmor",
			"upgradeTimeHP",
			
			"jobStopWalk",
			"jobStopSwim",
			"jobStopFly",
			"jobWalk",
			"jobSwim",
			"jobFly",
			"jobBuild",
			"jobHarvest",
			"jobAttackSpeed",
			"jobAttackStrength",
			"jobArmor",
			"jobHP",
			
			"attractStopWalk",
			"attractStopSwim",
			"attractStopFly",
			"attractWalk",
			"attractSwim",
			"attractFly",
			"attractBuild",
			"attractHarvest",
			"attractAttackSpeed",
			"attractAttackStrength",
			"attractArmor",
			"attractHP",

			"canFeedUnit",
			"timeToFeedUnit",
			"canHealUnit",
			"timeToHealUnit",
			"insideSpeed",

			"width",
			"height",
			"decLeft",
			"decTop",
			"isVirtual",
			"isCloacked",
			"shootingRange",
			"shootDamage",
			"shootSpeed",
			"shootRythme",

			"unitProductionTime",
			"ressourceForOneUnit",

			"maxWood",
			"maxFood",
			"maxStone",
			"maxAlgue",
			"maxUnitInside",
			"maxUnitWorking",

			"hpInit",
			"hpMax",
			"hpInc",
			"armor",
			"level",
			"type",
			"isBuildingSite",

			"defaultUnitStayRange"
		};
		if (size)
			*size=(sizeof(vars)/sizeof(char *));
		if (data)
			*data=__STARTDATA_B;
		return vars;
	}
};

class BuildingsTypes
{
public:
	BuildingsTypes();
	void load(const char *filename);
	~BuildingsTypes();

	int getTypeNum(int type, int level, bool isBuildingSite);
	BuildingType *getBuildingType(unsigned int typeNum);
	
public:
	std::vector <BuildingType *> buildingsTypes;
};

#endif
 
