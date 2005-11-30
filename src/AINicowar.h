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


///This implements an advanced AI, it is designed to do everything the best of players would do. But since it is an AI, it can
///manage much more than a human can at one time, thus making it a difficult opponent. However, the AI is not going to be
///stand alone for quite some time, thus its name AINicowar, as it is an ai that can apply advanced techniques to other AI's or
///human players.
class AINicowar : public AIImplementation
{
	public:
		AINicowar(Player *player);
		AINicowar(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
		~AINicowar();

		Player *player;
		Team *team;
		Game *game;
		Map *map;

		bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
		void save(GAGCore::OutputStream *stream);

		Order *getOrder(void);
	public:
		void init(Player *player);

		///Returns the number of free units with the given ability and the given level in it.
		///It discounts hungry/hurt units.
		int getFreeUnits(int ability, int level);
		int timer;
		int iteration;
		std::queue<Order*> orders;
		///Checks if the given spot is free of flags
		bool isFreeOfFlags(unsigned int x, unsigned int y);
		///Returns the building* of the gid, or NULL
		Building* getBuildingFromGid(int gid)
		{
			if(gid==NOGBID)
				return NULL;
			return game->teams[Building::GIDtoTeam(gid)]->myBuildings[Building::GIDtoID(gid)];
		}
		///Returns a unit* of the gid, or NULL
		Unit* getUnitFromGid(int gid)
		{
			if(gid==NOGUID)
				return NULL;
			return game->teams[Unit::GIDtoTeam(gid)]->myUnits[Unit::GIDtoID(gid)];
		}

		///Returns true if the given building hasn't been destroyed
		bool buildingStillExists(Building* b);
		///Returns true if the given building hasn't been destroyed
		bool buildingStillExists(unsigned int gid);



		///@name Abstract Region Polling System
		///These set of methods, enumerations and structs are what abstract the map polling system, which is used by several
		///modules. The map polling system devides the map into a set of sections, which can also be called regions or zones.
		///The names are used interchangebly. It then polls each grid area for a set of information, sucvh as hidden squares
		///or enemy units. The various methods do different things with that information.
		///@{

		///This represents the various things that can be polled for on a grid section
		enum pollType
		{
			///Hidden squares: Squares that can't be seen by the player, they are black in the game window.
			HIDDEN_SQUARES,
			///The number of squares that aren't hidden
			VISIBLE_SQUARES,
			///Any of our opponents buildings.
			ENEMY_BUILDINGS,
			///This teams buildings
			FRIENDLY_BUILDINGS,
			///Any opposing unit, including explorers, warriors and workers.
			ENEMY_UNITS,
			///A single section of corn.
			POLL_CORN,
			///A single section of trees
			POLL_TREES,
			///Polls for a block of stone
			POLL_STONE,
			///Returns 0 everytime.
			NONE,
		};

		///The poll modifiers tell what the scores mean, and how to change them.
		enum pollModifier
		{
			///This will give a point for each square that has the desired thing.
			MAXIMUM,
			///This gives a point to each square that doesn't have the desired thing.
			MINIMUM
		};

		///Represents a zone, with its x and y cordinates and its width and height.
		struct zone
		{
			unsigned int x;
			unsigned int y;
			unsigned int width;
			unsigned int height;

		};
		///Represents a single pollRecord. It has the information for the zone, as well as the score and type of information that as polled for.
		struct pollRecord
		{
			int x;
			int y;
			int width;
			int height;
			int score;
			pollType poll_type;
			pollRecord(int ax, int ay, int awidth, int aheight, int ascore, pollType apoll_type) : x(ax), y(ay), width(awidth), height(aheight), score(ascore), poll_type(apoll_type) {}
			pollRecord() {}
			bool operator>(const pollRecord& cmp) const { return score>cmp.score; }
			bool operator<(const pollRecord& cmp) const { return score<cmp.score; }
			bool operator<=(const pollRecord& cmp) const { return score<=cmp.score; }
			bool operator>=(const pollRecord& cmp) const { return score>=cmp.score; }
		};

		///Polls a specific region for the number of objects given by type, which can be one of
		///the above enum.
		int pollArea(unsigned int x, unsigned int y, unsigned int width, unsigned int height, pollModifier mod, pollType poll_type);

		///Polls the entire map using the given information, returning the n top spots and their
		///information.
		std::vector<pollRecord> pollMap(unsigned int area_width, unsigned int area_height, int horizontal_overlap, int vertical_overlap, unsigned int requested_spots, pollModifier mod, pollType poll_type);

		///Returns the placement of a certain psoition in a pollRecord vector. This handles
		///multiple pollRecords being tied for a certain place. The list must be sorted.
		int getPositionScore(const std::vector<pollRecord>& polls, const std::vector<pollRecord>::const_iterator& iter);

		///Gets the zone that position x and y are in.
		zone getZone(unsigned int x, unsigned int y, unsigned int area_width, unsigned int area_height, int horizontal_overlap, int vertical_overlap);

		///This three tier record is meant for its three scores to be balanced, meaning
		///they must be out of the same maximum, so the scores are best made to be
		///indexes on previoussly sorted, equal length lists.
		struct threeTierRecord
		{
			unsigned int x;
			unsigned int y;
			unsigned int width;
			unsigned int height;
			unsigned int score_a;
			unsigned int score_b;
			unsigned int score_c;
			bool operator<(const threeTierRecord& cmp) const
			{
				if(score_a+score_b+score_c < cmp.score_a+cmp.score_b+cmp.score_c)
					return true;

				else if(score_a+score_b+score_c == cmp.score_a+cmp.score_b+cmp.score_c)
				{

					if(score_a<cmp.score_a)
						return true;
					if(score_a==cmp.score_a && score_b<cmp.score_b)
						return true;
					if(score_a==cmp.score_a && score_b==cmp.score_b && score_c<cmp.score_c)
						return true;
				}
				return false;
			}
		};

		///Returns the zones in order of scores of each poll type: a, b, and c,
		///where a is prioritized over b and b is prioritized over c. extention_width
		///and extention_height cause getBestZones to poll a zone larger than
		///what it would normally, so this allows for zones not to be strictly
		///scored in only there area, but surrounding areas can be considered
		///as well. The extentions are applied to both the top and the bottom
		///of the zone, so the total width or height of the zone increases by two
		///times the extention.
		std::vector<zone> getBestZones(pollModifier amod, pollType a, pollModifier bmod, pollType b, pollModifier cmod, pollType c, unsigned int width, unsigned int height, int horizontal_overlap, int vertical_overlap, unsigned int extention_width, unsigned int extention_height, unsigned int minimum_friendly_buildings);
		///@}

		///@name AINicowar Upgrade and Repair Manegement System
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

		///@name AINicowar Air Search and Assault System
		///The following deal with AINicowars management of explorers. It tries to explore areas covered with fog of war
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
			unsigned int target;
			Building* flag;
			unsigned int flagx;
			unsigned int flagy;
			unsigned int zonex;
			unsigned int zoney;
			unsigned int width;
			unsigned int height;
			unsigned int unitx;
			unsigned int unity;
			unsigned int unit_width;
			unsigned int unit_height;
			unsigned int assigned_units;
			unsigned int assigned_level;

		};
		std::vector<attackRecord> attacks;

		///Chooses an enemy to attack. Will change to a different enemy if the current enemy has been eradicated.
		void targetEnemy();
		Team* enemy;

		///If we have enough warriors of the best available skill level, launch an attack!
		void attack();

		//Find flags that attack() created, and update flags as neccecary to keep up with the number of units defending each building
		void updateAttackFlags();
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
			innRecord();
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
		///Assigns units to towers. It will eventually do full management of towers, adding more units to towers that
		///need them more.
		///@{
		///Searches for any towers that do not have the right amount of units assigned, and assigns them.
		void controlTowers();
		///@}

		///@name Construction Manager.
		///Likely to be one of the most important modules in the AI. This one handles construction of buildings.
		///@{

		struct point
		{
			unsigned int x;
			unsigned int y;
		};

		struct upgradeData
		{
			int horizontal_offset;
			int vertical_offset;
			unsigned int width;
			unsigned int height;
		};

		///Stores information for the creation of a new building.
		struct newConstructionRecord
		{
			unsigned int building;
			unsigned int x;
			unsigned int y;
			unsigned int assigned;
			unsigned int building_type;
		};

		///Stores the various records of what is being built
		std::vector<newConstructionRecord> new_buildings;

		upgradeData findMaxSize(unsigned int building_type, unsigned int cur_level);

		point findBestPlace(unsigned int building_type);

		///Constructs the various queued up buildings.
		void constructBuildings();

		///Finds the buildings that constructBuildings has started construction of.
		void updateBuildings();

		std::map<unsigned int, unsigned int> num_buildings_wanted;
		///@}

};


///These constants are what fine tune AINicowar. There is allot of them.
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
const int TIMER_ITERATION=100;
const int removeOldConstruction_TIME=0;
const int updatePendingConstruction_TIME=5;
const int startNewConstruction_TIME=10;
const int reassignConstruction_TIME=15;
const int exploreWorld_TIME=20;
const int findCreatedFlags_TIME=25;
const int moderateSwarmsForExplorers_TIME=30;
const int explorerAttack_TIME=35;
const int targetEnemy_TIME=40;
const int attack_TIME=45;
const int updateAttackFlags_TIME=50;
const int moderateSwarms_TIME=55;
const int recordInns_TIME=60;
const int modifyInns_TIME=65;
const int controlTowers_TIME=70;
const int updateFlags_TIME=75;
const int findDefense_TIME=80;
const int findCreatedDefenseFlags_TIME=85;
const int constructBuildings_TIME=90;
const int updateBuildings_TIME=95;

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
const unsigned int EMERGENCY_UNIT_SCORE=5;
//This means that for every n points it will add in one new worker to the swarms.
const unsigned int CREATION_UNIT_REQUIREMENT=8;
const unsigned int MAXIMUM_UNITS_FOR_SWARM=6;

//These constants are for the AI's inn manager.
//Says how many records it should take for each inn before restarting back at the begginning.
const unsigned int INN_RECORD_MAX=20;
const unsigned int INN_MAX[3]={2, 5, 8};
const unsigned int INN_MINIMUM[3]={1, 1, 2};

//These constants are for AINicowars tower controller
const unsigned int NUM_PER_TOWER=2;

//These constants are for the defense system.
const unsigned int DEFENSE_ZONE_BUILDING_PADDING=2;
const unsigned int DEFENSE_ZONE_WIDTH=8;
const unsigned int DEFENSE_ZONE_HEIGHT=8;
const int DEFENSE_ZONE_HORIZONTAL_OVERLAP=0;
const int DEFENSE_ZONE_VERTICAL_OVERLAP=0;
const unsigned int BASE_DEFENSE_WARRIORS=20;

//These constants are for the attack system.
const IntBuildingType::Number ATTACK_PRIORITY[IntBuildingType::NB_BUILDING-3] =
{
	IntBuildingType::HEAL_BUILDING,
	IntBuildingType::FOOD_BUILDING,
	IntBuildingType::ATTACK_BUILDING,
	IntBuildingType::WALKSPEED_BUILDING,
	IntBuildingType::SWIMSPEED_BUILDING,
	IntBuildingType::SCIENCE_BUILDING,
	IntBuildingType::SWARM_BUILDING,
	IntBuildingType::DEFENSE_BUILDING,
	IntBuildingType::MARKET_BUILDING,
	IntBuildingType::STONE_WALL
};
const unsigned int ATTACK_ZONE_BUILDING_PADDING=1;
const unsigned int ATTACK_ZONE_EXAMINATION_PADDING=10;
const unsigned int ATTACK_WARRIOR_MINIMUM=8;
const unsigned int MAX_ATTACKS_AT_ONCE=5;
const unsigned int BASE_ATTACK_WARRIORS=static_cast<unsigned int>(MAX_ATTACKS_AT_ONCE*ATTACK_WARRIOR_MINIMUM*1.5);

//The following are for the construction manager

const unsigned int BUILD_AREA_WIDTH=8;
const unsigned int BUILD_AREA_HEIGHT=8;
const unsigned int BUILD_AREA_HORIZONTAL_OVERLAP=2;
const unsigned int BUILD_AREA_VERTICAL_OVERLAP=2;
const unsigned int BUILD_AREA_EXTENTION_WIDTH=16;
const unsigned int BUILD_AREA_EXTENTION_HEIGHT=16;
const unsigned int BUILDING_PADDING=2;
///With the following enabled, the new construction manager will try to place buildings as close together as possible that still satisfy the padding.
const bool CRAMP_BUILDINGS=true;
const unsigned int NOPOS=1023;
const unsigned int MINIMUM_NEARBY_BUILDINGS_TO_CONSTRUCT=1;
const unsigned int CONSTRUCTION_FACTORS[IntBuildingType::NB_BUILDING][3][2]=
{
{{AINicowar::MAXIMUM, AINicowar::POLL_CORN}, {AINicowar::MAXIMUM, AINicowar::NONE}, {AINicowar::MAXIMUM, AINicowar::NONE}},
{{AINicowar::MAXIMUM, AINicowar::POLL_CORN}, {AINicowar::MAXIMUM, AINicowar::NONE}, {AINicowar::MAXIMUM, AINicowar::NONE}},
{{AINicowar::MAXIMUM, AINicowar::POLL_TREES}, {AINicowar::MAXIMUM, AINicowar::NONE}, {AINicowar::MAXIMUM, AINicowar::NONE}},
{{AINicowar::MAXIMUM, AINicowar::FRIENDLY_BUILDINGS}, {AINicowar::MAXIMUM, AINicowar::NONE}, {AINicowar::MAXIMUM, AINicowar::NONE}},
{{AINicowar::MAXIMUM, AINicowar::FRIENDLY_BUILDINGS}, {AINicowar::MAXIMUM, AINicowar::NONE}, {AINicowar::MAXIMUM, AINicowar::NONE}},
{{AINicowar::MAXIMUM, AINicowar::POLL_STONE}, {AINicowar::MAXIMUM, AINicowar::POLL_TREES}, {AINicowar::MAXIMUM, AINicowar::NONE}},
{{AINicowar::MAXIMUM, AINicowar::FRIENDLY_BUILDINGS}, {AINicowar::MAXIMUM, AINicowar::NONE}, {AINicowar::MAXIMUM, AINicowar::NONE}},
{{AINicowar::MINIMUM, AINicowar::FRIENDLY_BUILDINGS}, {AINicowar::MAXIMUM, AINicowar::NONE}, {AINicowar::MAXIMUM, AINicowar::NONE}},
{{AINicowar::MAXIMUM, AINicowar::NONE}, {AINicowar::MAXIMUM, AINicowar::NONE}, {AINicowar::MAXIMUM, AINicowar::NONE}},
{{AINicowar::MAXIMUM, AINicowar::NONE}, {AINicowar::MAXIMUM, AINicowar::NONE}, {AINicowar::MAXIMUM, AINicowar::NONE}},
{{AINicowar::MAXIMUM, AINicowar::NONE}, {AINicowar::MAXIMUM, AINicowar::NONE}, {AINicowar::MAXIMUM, AINicowar::NONE}},
{{AINicowar::MAXIMUM, AINicowar::NONE}, {AINicowar::MAXIMUM, AINicowar::NONE}, {AINicowar::MAXIMUM, AINicowar::NONE}},
{{AINicowar::MAXIMUM, AINicowar::NONE}, {AINicowar::MAXIMUM, AINicowar::NONE}, {AINicowar::MAXIMUM, AINicowar::NONE}}
};
const unsigned int MAX_NEW_CONSTRUCTION_AT_ONCE=6;
const unsigned int MAX_NEW_CONSTRUCTION_PER_BUILDING[IntBuildingType::NB_BUILDING] =
{1, 1, 3, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
const unsigned int MINIMUM_TO_CONSTRUCT_NEW=2;
const unsigned int MAXIMUM_TO_CONSTRUCT_NEW=8;

///This constant turns on debugging output
const bool AINicowar_DEBUG = true;
///@}


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



inline std::ostream& operator<<(std::ostream& o, const AINicowar::zone& z)
{
	o<<"zone(x="<<z.x<<", y="<<z.y<<", width="<<z.width<<", height="<<z.height<<")";
	return o;
}
#endif
