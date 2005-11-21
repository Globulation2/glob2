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
#ifndef __AI_HELPER_H
#define __AI_HELPER_H

#include "BuildingType.h"
#include "Building.h"
#include "Game.h"
#include "Unit.h"
#include "AIImplementation.h"
#include <queue>
#include <list>
#include <algorithm>
#include "IntBuildingType.h"
#include <map>

using namespace std;

class Map;
class Order;
class Player;
class Team;

///These constants are what fine tune AIHelper. There is allot of them.
///@{
//The following deal with the upgrade and repair management system.
const unsigned int MINIMUM_TO_UPGRADE=4;
const unsigned int MAXIMUM_TO_UPGRADE=8;
const unsigned int MINIMUM_TO_REPAIR=2;
const unsigned int MAXIMUM_TO_REPAIR=8;
const unsigned int MAX_CONSTRUCTION_AT_ONCE=4;
const int MAX_BUILDING_SPECIFIC_CONSTRUCTION_LIMITS[IntBuildingType::NB_BUILDING]=
{0, 4, 1, 1, 1, 1, 1, 2, 0, 0, 0, 1, 1};

//The following constants deal with the function iteration. All of these except the first must be
//lower than TIMER_ITERATION.
const int TIMER_ITERATION=75;
const int removeOldConstruction_TIME=0;
const int updatePendingConstruction_TIME=5;
const int startNewConstruction_TIME=10;
const int reassignConstruction_TIME=15;
const int exploreWorld_TIME=20;
const int findCreatedFlags_TIME=25;
const int moderateSwarmsForExplorers_TIME=30;
const int explorerAttack_TIME=35;
const int moderateSwarms_TIME=40;
const int recordInns_TIME=45;
const int modifyInns_TIME=50;
const int controlTowers_TIME=55;
const int updateFlags_TIME=60;
const int findDefense_TIME=65;
const int findCreatedDefenseFlags_TIME=70;

//These constants are for the AI's management of exploration and explorer attacks.
//Warning, the following four constants must be powers of 2, because map sizes are in powers of two,
//or the explorers will leave small strips at the right side of maps unexplored.
const unsigned int EXPLORER_REGION_WIDTH=16;
const unsigned int EXPLORER_REGION_HEIGHT=16;
const int EXPLORER_REGION_HORIZONTAL_OVERLAP=0;
const int EXPLORER_REGION_VERTICAL_OVERLAP=0;
//Its reccomended that this number is an even number.
const unsigned int EXPLORERS_PER_REGION=2;
const unsigned int EXPLORATION_FLAG_RADIUS=12;
const unsigned int EXPLORER_MAX_REGIONS_AT_ONCE=3;

const unsigned int EXPLORER_ATTACK_AREA_WIDTH=8;
const unsigned int EXPLORER_ATTACK_AREA_HEIGHT=8;
const int EXPLORER_ATTACK_AREA_HORIZONTAL_OVERLAP=4;
const int EXPLORER_ATTACK_AREA_VERTICAL_OVERLAP=4;
const unsigned int EXPLORERS_PER_ATTACK=2;
const unsigned int EXPLORATION_FLAG_ATTACK_RADIUS=5;
const unsigned int EXPLORER_ATTACKS_AT_ONCE=4;

//These constants are for the AI's swarm controller.
const unsigned int DESIRED_UNIT_SCORE=1;
const unsigned int REQUIRED_UNIT_SCORE=2;
const unsigned int EMERGENCY_UNIT_SCORE=6;
//This means that for every n units that need to be created, it will add in one new worker to the swarms.
const unsigned int CREATION_UNIT_REQUIREMENT=15;

//These constants are for the defense system.
const unsigned int DEFENSE_ZONE_BUILDING_PADDING=2;

const unsigned int DEFENSE_ZONE_WIDTH=8;
const unsigned int DEFENSE_ZONE_HEIGHT=8;
const unsigned int DEFENSE_ZONE_HORIZONTAL_OVERLAP=0;
const unsigned int DEFENSE_ZONE_VERTICAL_OVERLAP=0;

//These constants are for the AI's inn manager.
//Says how many records it should take for each inn before restarting back at the begginning.
const unsigned int INN_RECORD_MAX=20;
const unsigned int INN_MAX[3]={2, 5, 8};
const unsigned int INN_MINIMUM[3]={1, 1, 2};

//These constants are for AIHelpers tower controller
const unsigned int NUM_PER_TOWER=2;
///@}

///This constant turns on debugging output
const bool AIHelper_DEBUG = true;

///This implements an advanced AI, it is designed to do everything the best of players would do. But since it is an AI, it can
///manage much more than a human can at one time, thus making it a difficult opponent. However, the AI is not going to be
///stand alone for quite some time, thus its name AIHelper, as it is an ai that can apply advanced techniques to other AI's or
///human players.
class AIHelper : public AIImplementation
{
	public:
		AIHelper(Player *player);
		AIHelper(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
		~AIHelper();

		Player *player;
		Team *team;
		Game *game;
		Map *map;

		bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
		void save(GAGCore::OutputStream *stream);

		Order *getOrder(void);

	private:
		void init(Player *player);

		///Returns the number of free workers with the given ability and the given level in it.
		///It discounts hungry/starving units.
		int getFreeUnits(int ability, int level);
		int timer;
		int iteration;
		std::queue<Order*> orders;
		///Checks if the given spot is free of flags
		bool isFreeOfFlags(unsigned int x, unsigned int y);
		///Returns the building* of the gid, or NULL
		Building* getBuildingFromGid(int gid)
		{
			//Hack Fix needs to be changed. Map::getBuilding is returning invalid gid's.
			if(gid>32768)
				gid=0;
			return game->teams[Building::GIDtoTeam(gid)]->myBuildings[Building::GIDtoID(gid)];
		}
		///Returns a unit* of the gid, or NULL
		Unit* getUnitFromGid(int gid)
		{
			//Hack Fix needs to be changed. Map::getBuilding is returning invalid gid's.
			if(gid>32768)
				gid=0;
			return game->teams[Unit::GIDtoTeam(gid)]->myUnits[Unit::GIDtoID(gid)];
		}

		///Returns true if the given building hasn't been destroyed
		bool buildingStillExists(Building* b);
		///Returns true if the given building hasn't been destroyed
		bool buildingStillExists(unsigned int gid);

		enum pollType
		{
			HIDDEN_SQUARES,
			ENEMY_BUILDINGS,
			ENEMY_UNITS
		};

		struct zone
		{
			unsigned int x;
			unsigned int y;
			unsigned int width;
			unsigned int height;
		};

		struct pollRecord
		{
			int x;
			int y;
			int width;
			int height;
			int score;
			pollType poll_type;
			pollRecord(int ax, int ay, int awidth, int aheight, int ascore, pollType apoll_type) : x(ax), y(ay), width(awidth), height(aheight), score(ascore), poll_type(apoll_type) {}
			bool operator>(const pollRecord& cmp) const { return score>cmp.score; }
			bool operator<(const pollRecord& cmp) const { return score<cmp.score; }
			bool operator<=(const pollRecord& cmp) const { return score<=cmp.score; }
			bool operator>=(const pollRecord& cmp) const { return score>=cmp.score; }
		};

		///Polls a specific region for the number of objects given by type, which can be one of
		///the above enum.
		int pollArea(unsigned int x, unsigned int y, unsigned int width, unsigned int height, pollType poll_type);

		///Polls the entire map using the given information, returning the n top spots and their
		///information.
		std::vector<pollRecord> pollMap(unsigned int area_width, unsigned int area_height, int horizontal_overlap, int vertical_overlap, unsigned int requested_spots, pollType poll_type);

		///Gets the zone that position x and y are in.
		zone getZone(unsigned int x, unsigned int y, unsigned int area_width, unsigned int area_height, int horizontal_overlap, int vertical_overlap);

		///@name AIHelper Upgrade and Repair Manegement System
		///Upgrades and repairs buildings at random, within certain limits. Only uses spare workers (some of which it may have asked for from the spawn manager.)
		///@{

		///A construction record, keeps track of information accociated with buildings that the ai is having construction done on
		///(either repair or upgrade.)
		struct constructionRecord
		{
			///The building that this record is for
			Building* building;
			///The number of units assigned to the building (or requested if its still pending)
			unsigned int assigned;
			///The number of units that where working the building before the construction
			unsigned int original;
			///True if the construction is repair, false if it is an upgrade.
			bool is_repair;
		};

		///Removes construction records that are no longer being constructed (either from cancel or finish)
		void removeOldConstruction(void);

		///A building may have been issued a command to be upgraded/repaired, but it is only
		///constructed on when all the people are out of it. So this goes through the pending
		///building list for any buildings that are no longer pending (don't have any
		///people in them.), and then assigns the number of requested units for the upgrade/
		///repair to the building.
		void updatePendingConstruction(void);

		///Looks through all of the buildings to see if any need to be upgraded or repaired.
		///If so, request construction, and push information including how many worker globs
		///it saw that where available onto the pending list. When the building is empty
		///and is ready to be constructed on, updatePendingUpgrades assign the number of
		///requested units to the building. Does nothing if it lacks units or available
		///buildings to upgrade.
		void startNewConstruction(void);

		///Returns the number of free units available to construct a building to the given level.
		///It does not take into account that other buildings under construction may not have
		///recieved all of their desired units.
		int getAvailableUnitsForConstruction(int level);

		///This one is simple. It gets the numbers of available units to perform upgrades
		///first by counting the numbers of free units. Then it adds the numbers of all
		///the units already working on an upgrade. Then it reassigns the numbers of units
		///assigned to the various upgrades all over again, so this makes up for any units
		///that have become available since the start of the upgrades, or perhaps loss of
		///units.
		void reassignConstruction(void);

		///A common piece of code. unit_counts is an array of unit counts for each level
		///up to NB_UNIT_LEVELS. Assuming that slot 0 in the area represents counts of units
		///with level 1. It will remove each counted unit with a minimum level of
		///minimum_level up to amount. It can be used, for example, when unit_counts is
		///the number of free units, to knock off all the units that are going to be
		///allocated to a construction, if there aren't enough units with the minimum level
		///to build the building, then it will take from the units of higher levels.
		inline void reduce(int* unit_counts, int minimum_level, int amount)
		{
			for (int j = 0; j < NB_UNIT_LEVELS; j++)
			{
				if (j < minimum_level)
				{
					continue;
				}

				if (unit_counts[j] < amount)
				{
					amount-=unit_counts[j];
					unit_counts[j]=0;
					continue;
				}
				unit_counts[j]-=amount;
				break;
			}
		}
		///This is the list of buildings that are undergoing upgrades, with attached information. It is order independant.
		std::list<constructionRecord> active_construction;

		///A list of buildings that are awaiting, with attached information. It is order independant.
		std::list<constructionRecord> pending_construction;
		///@}

		///@name AIHelper Air Search and Assault System
		///The following deal with AIHelpers management of explorers. It tries to explore areas covered with fog of war
		///and, if it has enough exploreres with magic ground damage, it will launch deadly assaults on the enemy using
		///explorers.
		///@{
		///Stores a simple record of what areas are being explored and/or attacked.
		struct explorationRecord
		{
			Building* flag;
			unsigned int flag_x;
			unsigned int flag_y;
			unsigned int zone_x;
			unsigned int zone_y;
			unsigned int width;
			unsigned int height;
			unsigned int assigned;
			unsigned int radius;
			bool isAssaultFlag;
		};
		///Creates, destroys, or moves explorer flags to explore all of the hidden world.
		///Important!! Each call to exploreWorld only manages changes (creates, destroys,
		///or moves) one flag. So if many regions are desired to be explored at once, it
		///may take several calls to exploreWorld to get them explored.
		void exploreWorld(void);

		///When exploreWorld creates a new flag, it isn't able to get a link to the flag
		///right away to be put into the active_exploration record list. So, instead, it
		///adds in a NULL pointer, and this function will search out for exploration
		///flags that have been create, that are not in the active_exploration record
		///list yet. And it puts them there, as well as assigning them the right radius.
		void findCreatedFlags(void);

		///Controls the swarms in order to get the desired numbers of explorers. It puts
		///the explorers creation at top priority, and if multiple explorers need to be
		///created, will distribute them between its swarms.
		void moderateSwarmsForExplorers(void);

		///Prepares and launches an explorer attack (if possible). Explorer attacks are
		///where a group of level 4 explorers are assigned to an explorer flag over an
		///area, who will spin around the permiter of the flag attack ground units. Can
		///be devestating if not defended against properly.
		void explorerAttack(void);

		///This list keeps a record of what regions are being explored, so when they're
		///done being explored, it can move on to explore other regions.
		std::list<explorationRecord> active_exploration;

		///Gives the number of desired explorers, which moderateSpawnExplorers will
		///control swarms in order to meet.
		unsigned int desired_explorers;
		///True if explorers that are intended to be used in an attack are being created.
		bool developing_attack_explorers;
		///True if the conditions are right for an explorer attack.
		bool explorer_attacking;
		///@}

		///@name Ground Attack System
		///Moderates ground attacks on the enemy. First chooses one enemy to attack
		///@{
		struct attackRecord
		{
			Building* target;
			Building* flag;
			unsigned int flagx;
			unsigned int flagy;
			unsigned int assigned_units;
			unsigned int assigned_level;

		};
		std::vector<attackRecord> attacks;

		///Chooses an enemy to attack. Will change to a different enemy if the current enemy has been eradicated.
		void targetEnemy();
		Team* enemy;

		///If we have enough warriors of the best available skill level, launch an attack!
		void attack();
		///@}

		///@name Spawn Controller
		///Controls spawns to maintain the desired number of units, based on 3 priority levels.
		///@{

		///Holds one record of one modules desires for one type of unit.
		struct unitRecord
		{
			unsigned int desired_units[NB_UNIT_TYPE];
			unsigned int required_units[NB_UNIT_TYPE];
			unsigned int emergency_units[NB_UNIT_TYPE];
		};
		///Stores a modules demands for units based on its module name.
		std::map<string, unitRecord> module_demands;

		///Allows a module to set the amount of units it requires for its purposes.
		///The number of desired units should be extra units that aren't essential for the running of the module.
		///Required units should be how many units the module requires to work at a minimal level.
		///Emergency units are any units that a module needs *know*. Can be used by the defense module to raise
		///an army quickly.
		void changeUnits(string module_name, unsigned int unit_type, unsigned int desired_units, unsigned int required_units, unsigned int emergency_units);

		///Constructs the ratios of desired workers, turns off production if there are enough units,
		///otherwise uses the ratios to moderate all of the spawns to get enough workers. It will
		///increase the number of people working on the spawn depending on how many units are wanted.
		void moderateSwarms();
		///@}

		///@name Inn Manager
		///Controls the amounts of units assigned to each inn using advanced statistical anylysis.
		///@{
		struct singleInnRecord
		{
			singleInnRecord(){food_amount=0; units_eating=0;}
			unsigned int food_amount;
			unsigned int units_eating;
		};
		struct innRecord
		{
			innRecord() : pos(0), records(INN_RECORD_MAX) {}
			unsigned int pos;
			vector<singleInnRecord> records;
		};

		///Maps the gid of buildings to their respective innRecord.
		std::map<int, innRecord> inns;

		///Adds one more recording to the innRecord for each of the inns. Uses round robin recording in order
		///to get untainted, recent averages.
		void recordInns();

		///Uses the simple, yet advanced, statistical anylysis algorithm to increase workers on particular inns.
		void modifyInns();
		///@}

		///@name Defense Engine
		///This module controls primary defense of your base. When a building is under attack, it identifies that buildings region,
		///then puts up an attack flag in that region if it doesn't already exist. It assigns as many enemy units in that region to
		///the flag times 1 and 1/2. It will destroy the flag when there are no enemy units left in the region. If the total threat
		///is large (as in more units than we have to defend with), it will tell the swarm controller to make warriors on the double.
		///@{
		struct defenseRecord
		{
			Building* flag;
			int flagx;
			int flagy;
			int zonex;
			int zoney;
			int width;
			int height;
			int assigned;
		};

		std::vector<defenseRecord> defending_zones;

		std::map<unsigned int, unsigned int> building_health;

		///Will check to see if there are any buildings under attack, if so, it will identify that buildings zone,
		///check to see if there aren't already warriors defending in that zone, and if not, add a flag with enough
		///assigned units to counter everything the enemy has *1.5. If the forces are overwhelming, tells the swarm
		///manager to create warriors on the double.
		void findDefense();

		///Destroys defense flags that are defending zones that are no longer under enemy attack. It also reassigns
		///units to flags in order to keep up to date on what zones need how many defending warriors. Finally, it
		///will also call on the swarm to create units in a big rush if it has not enough.
		void updateFlags();

		///When defense flags are created by findDefense, they aren't created instantly. Generally it takes 1 tick
		///later before the flag can be found and added to the records.
		void findCreatedDefenseFlags();

		///@}

		///@name Tower Controller
		///Assigns units to towers.
		///@{

		///Searches for any towers that do not have the right amount of units assigned, and assigns them.
		void controlTowers();
		///@}
};

///Shuffles the given list.
template<typename T> void list_shuffle(std::list<T>& l)
{
	std::vector<T> v(l.begin(), l.end());
	std::random_shuffle(v.begin(), v.end());
	typename std::list<T>::iterator i1 = l.begin();
	typename std::vector<T>::iterator i2 = v.begin();
	while (i1!=l.end())
	{
		(*i1)=(*i2);
		++i1;
		++i2;
	}
}
#endif
