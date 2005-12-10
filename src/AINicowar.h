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
#include "Utilities.h"

using namespace std;

class Map;
class Order;
class Player;
class Team;

///This implements an advanced AI, it is designed to do everything the best of players would do. But since it is an AI, it can
///manage much more than a human can at one time, thus making it a difficult opponent. It is unstable right now, but its
///techniques are defined and ready to use. Having personally tested the grow fast technique that it employs, i know first hand
///just how devestating an attack of level 3 warriros can be to a guard half the size of level 1 warriors!
namespace Nicowar
{

	class Module;
	class DefenseModule;
	class AttackModule;
	class NewConstructionModule;
	class UpgradeRepairModule;
	class UnitModule;
	class OtherModule;

	///This is the base module implementation, it does most of the interfacing with the game engine.
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

			void setDefenseModule(DefenseModule* module);
			void setAttackModule(AttackModule* module);
			void setNewConstructionModule(NewConstructionModule* module);
			void setUpgradeRepairModule(UpgradeRepairModule* module);
			void setUnitModule(UnitModule* module);
			void addOtherModule(OtherModule* module);
			DefenseModule* getDefenseModule();
			AttackModule* getAttackModule();
			NewConstructionModule* getNewConstructionModule();
			UpgradeRepairModule* getUpgradeRepairModule();
			UnitModule* getUnitModule();
			OtherModule* getOtherModule(string name);

			unsigned int getCenterX() const
			{
				return center_x;
			}

			unsigned int getCenterY() const
			{
				return center_y;
			}

			std::queue<Order*> orders;
		private:
			///Initiates the player
			void init(Player *player);
			unsigned int timer;
			unsigned int iteration;
			unsigned int center_x;
			unsigned int center_y;

			///Sets the center x and center y based on the currently viewable squares.
			void setCenter();


			DefenseModule* defense_module;
			AttackModule* attack_module;
			NewConstructionModule* new_construction_module;
			UpgradeRepairModule* upgrade_repair_module;
			UnitModule* unit_module;
			std::map<string, OtherModule*> other_modules;
			std::vector<Module*> modules;
			unsigned int module_timer;
			std::vector<Module*>::iterator active_module;
	};

	///This set of methods, enumerations and structs are what abstract the map polling system, which is used by several
	///modules. The map polling system devides the map into a set of sections, which can also be called regions or zones.
	///The names are used interchangebly. It then polls each grid area for a set of information, sucvh as hidden squares
	///or enemy units. The various methods do different things with that information.
	class GridPollingSystem
	{
		public:
			GridPollingSystem(AINicowar& ai);

			///This represents the various things that can be polled for on a grid section
			enum pollType
			{
				///Hidden squares: Squares that can't be seen by the player, they are black in the game window.
				HIDDEN_SQUARES,
				///The number of squares that aren't hidden
				VISIBLE_SQUARES,
				///Returns the distance between the center square and the village center
				CENTER_DISTANCE,
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
				unsigned int x;
				unsigned int y;
				unsigned int width;
				unsigned int height;
				unsigned int score;
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
			unsigned int pollArea(unsigned int x, unsigned int y, unsigned int width, unsigned int height, pollModifier mod, pollType poll_type);

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

			///This contains all of the information for a single poll of getBestZones. Its obvious that there
			///are many possible factors that can go into a poll.
			struct poll
			{
				pollModifier mod_1;
				pollType type_1;
				pollModifier mod_2;
				pollType type_2;
				pollModifier mod_3;
				pollType type_3;
				pollModifier mod_minimum;
				pollType minimum_type;
				unsigned int minimum_score;
				pollModifier mod_maximum;
				pollType maximum_type;
				unsigned int maximum_score;
				poll() :    mod_1(MAXIMUM), type_1(NONE), mod_2(MAXIMUM), type_2(NONE), mod_3(MAXIMUM), type_3(NONE),
					mod_minimum(MAXIMUM), minimum_type(NONE), minimum_score(0), mod_maximum(MAXIMUM),
					maximum_type(NONE), maximum_score(0)
					{}
			};

			///Returns the zones in order of scores of each poll type: a, b, and c,
			///where a is prioritized over b and b is prioritized over c. extention_width
			///and extention_height cause getBestZones to poll a zone larger than
			///what it would normally, so this allows for zones not to be strictly
			///scored in only there area, but surrounding areas can be considered
			///as well. The extentions are applied to both the top and the bottom
			///of the zone, so the total width or height of the zone increases by two
			///times the extention.
			std::vector<zone> getBestZones(poll p, unsigned int width, unsigned int height, int horizontal_overlap, int vertical_overlap, unsigned int extention_width, unsigned int extention_height);

		private:
			Map* map;
			Team* team;
			Game* game;
			unsigned int center_x;
			unsigned int center_y;
	};

	///Individual modules handle everything, after being connected to their respecting ai.
	class Module
	{
		public:
			///Destructs the module, deconnecting it from the base.
			virtual ~Module() {};
			///Asks the Module to perform something in its timeslice
			virtual void perform(unsigned int time_slice_n)=0;
			///Gets the name of this module.
			virtual string getName() const=0;
			///This should load the modules contents from the stream
			virtual bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)=0;
			///This should save the modules contents from the stream
			virtual void save(GAGCore::OutputStream *stream) const=0;
			///Should return the number of ticks this module desires in each iteration.
			///The module should not handle the time inbetween the ticks, so it should
			///do something for every tick it requests.
			virtual unsigned int numberOfTicks() const=0;
	};

	///Combines common components between various defense modules.
	class DefenseModule : public Module
	{
		public:
	};

	///A base class for all modules that are to handle attacking.
	class AttackModule : public Module
	{
		public:
	};

	///Any module that is to start and manage construction should derive from this one.
	class NewConstructionModule : public Module
	{
		public:
	};

	///Any module that is to manage updrades and repairs should derive from this one.
	class UpgradeRepairModule : public Module
	{
		public:
	};

	///Any module that is going to handle general unit passing and swarms should derive from this one.
	class UnitModule : public Module
	{
		public:
			///Allows a module to set the amount of units it requires for its purposes.
			///The number of desired units should be extra units that aren't essential for the running of the module.
			///Required units should be how many units the module requires to work at a minimal level.
			///Emergency units are any units that a module needs *know*. Can be used by the defense module to raise
			///an army quickly.
			virtual void changeUnits(string module_name, unsigned int unit_type, unsigned int desired_units, unsigned int required_units, unsigned int emergency_units)=0;
	};

	///This designates other modules that don't fit into the above catagories.
	class OtherModule : public Module
	{
		public:

	};

	///This module controls primary defense of your base. When a building is under attack, it identifies that buildings region,
	///then puts up an attack flag in that region if it doesn't already exist. It assigns as many enemy units in that region to
	///the flag times 1 and 1/2. It will destroy the flag when there are no enemy units left in the region. If the total threat
	///is large (as in more units than we have to defend with), it will tell the swarm controller to make warriors on the double.
	class SimpleBuildingDefense : public DefenseModule
	{
		public:

			SimpleBuildingDefense(AINicowar& ai);

			~SimpleBuildingDefense() {};
			void perform(unsigned int time_slice_n);
			string getName() const;
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream) const;
			unsigned int numberOfTicks() const
			{
				return 3;
			}

			struct defenseRecord
			{
				Building* flag;
				unsigned int flagx;
				unsigned int flagy;
				unsigned int zonex;
				unsigned int zoney;
				unsigned int width;
				unsigned int height;
				unsigned int assigned;
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

			AINicowar& ai;
	};

	///Generals defense is an alternative, fairly difficult defense mechanism.
	///It looks through its enemies buildings, looking for war flags that are
	///near its buildings. If it finds one, it will check to see if it has already
	///setup defense against it. If not, it will set up a flag to match the enemies
	///flag in the same position in size, number of units, and training level.
	///It can fail when the opponent simply has a stronger force, but this is more
	///than capable of combatting several weaker forces.
	class GeneralsDefense : public DefenseModule
	{
		public:
			GeneralsDefense(AINicowar& ai);
			~GeneralsDefense() {};
			void perform(unsigned int time_slice_n);
			string getName() const;
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream) const;
			unsigned int numberOfTicks() const
			{
				return 2;
			}

			///Stores the information nessecary to remeber the combat against one enemy flag.
			struct defenseRecord
			{
				unsigned int flag;
				unsigned int enemy_flag;
			};

			vector<defenseRecord> defending_flags;

			///Searches for enemy flags that it needs to defend against, and if
			///it finds one, it will make a new flag.
			void findEnemyFlags();

			///Looks through the defense records and updates the flags as nessecary
			///to be able to combat the opponents forces.
			void updateDefenseFlags();

			AINicowar& ai;
	};

	///Moderates ground attacks on the enemy. After it has chosen what enemy to attack, it attacks buildings based on priority,
	///destroying all of one building type before moving onto the next.
	class PrioritizedBuildingAttack : public AttackModule
	{
		public:
			PrioritizedBuildingAttack(AINicowar& ai);
			~PrioritizedBuildingAttack() {};
			void perform(unsigned int time_slice_n);
			string getName() const;
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream) const;
			unsigned int numberOfTicks() const
			{
				return 3;
			}

			///Stores all of the information accociated with a particular attack
			struct attackRecord
			{
				unsigned int target;
				unsigned int target_x;
				unsigned int target_y;
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

			///Stores everything about what the ai is attacking
			std::vector<attackRecord> attacks;

			///Chooses an enemy to attack. Will change to a different enemy if the current enemy has been eradicated.
			void targetEnemy();
			Team* enemy;

			///If we have enough warriors of the best available skill level, launch an attack!
			void attack();

			///Find flags that attack() created, and update flags as neccecary to keep up with the number of units defending each building
			void updateAttackFlags();

			///Holds a refernece to the ai so taht the module can work properly.
			AINicowar& ai;

	};

	///This construction manager constructs buildings based on the total numbers of buildings it wants.
	///It builds based on what percentage of buildings each type has comapred to how many it wants.
	class DistributedNewConstructionManager : public NewConstructionModule
	{
		public:
			DistributedNewConstructionManager(AINicowar& ai);
			~DistributedNewConstructionManager() {};
			void perform(unsigned int time_slice_n);
			string getName() const;
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream) const;
			unsigned int numberOfTicks() const
			{
				return 3;
			}

			///Stores a single point on the map
			struct point
			{
				unsigned int x;
				unsigned int y;
			};

			///Stores information about the size and offset the maximum size a particular type of building has.
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

			///Stores the percentage of buildings for a particular type there is.
			struct typePercent
			{
				unsigned int building_type;
				unsigned int percent;
				bool operator<(const typePercent& tp) const
				{
					return percent<tp.percent;
				}
				bool operator>(const typePercent& tp) const
				{
					return percent>tp.percent;
				}
			};

			///Stores the various records of what is being built
			std::vector<newConstructionRecord> new_buildings;

			upgradeData findMaxSize(unsigned int building_type, unsigned int cur_level);

			point findBestPlace(unsigned int building_type);

			///Constructs the various queued up buildings.
			void constructBuildings();

			///Finds the buildings that constructBuildings has started construction of.
			void updateBuildings();

			//Calculates how many of each type of building the ai should have.
			void calculateBuildings();

			std::map<unsigned int, unsigned int> num_buildings_wanted;

			///Holds a refernece to the ai so taht the module can work properly.
			AINicowar& ai;
	};

	///This module upgrades and repairs buildings at random, up to a designated maximum construction.
	///It only uses free workers.
	class RandomUpgradeRepairModule : public UpgradeRepairModule
	{
		public:
			RandomUpgradeRepairModule(AINicowar& ai);
			~RandomUpgradeRepairModule() {};
			void perform(unsigned int time_slice_n);
			string getName() const;
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream) const;
			unsigned int numberOfTicks() const
			{
				return 4;
			}

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

			///Holds a refernece to the ai so taht the module can work properly.
			AINicowar& ai;
	};

	///Controls swarms quite simplisticly, creating rations based on desired numbers of units (with priorites),
	///and assigning units to swarms evenly, based on the number of untis it has to create.
	class BasicDistributedSwarmManager : public UnitModule
	{
		public:
			BasicDistributedSwarmManager(AINicowar& ai);
			~BasicDistributedSwarmManager() {};
			void perform(unsigned int time_slice_n);
			string getName() const;
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream) const;
			unsigned int numberOfTicks() const
			{
				return 1;
			}

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

			///Holds a refernece to the ai so taht the module can work properly.
			AINicowar& ai;
	};

	///The following deal with AINicowars management of explorers. It tries to explore areas covered with fog of war
	///and, if it has enough exploreres with magic ground damage, it will launch deadly assaults on the enemy using
	///explorers.
	class ExplorationManager : public OtherModule
	{
		public:
			ExplorationManager(AINicowar& ai);
			~ExplorationManager() {};
			void perform(unsigned int time_slice_n);
			string getName() const;
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream) const;
			unsigned int numberOfTicks() const
			{
				return 4;
			}

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
			void updateExplorationFlags(void);

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

			///Checks if the given spot is free of flags.
			bool isFreeOfFlags(unsigned int x, unsigned int y);

			unsigned int explorers_wanted;
			///True if explorers that are intended to be used in an attack are being created.
			bool developing_attack_explorers;
			///True if the conditions are right for an explorer attack.
			bool explorer_attacking;

			///Holds a refernece to the ai so taht the module can work properly.
			AINicowar& ai;
	};

	///Performs usage recording coupled with supply recording
	class InnManager : public OtherModule
	{
		public:
			InnManager(AINicowar& ai);
			~InnManager() {};
			void perform(unsigned int time_slice_n);
			string getName() const;
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream) const;
			unsigned int numberOfTicks() const
			{
				return 2;
			}

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

			///Holds a refernece to the ai so taht the module can work properly.
			AINicowar& ai;
	};

	///Does the reletivly simple task of assigning units to towers.
	class TowerController : public OtherModule
	{
		public:
			TowerController(AINicowar& ai);
			~TowerController() {};
			void perform(unsigned int time_slice_n);
			string getName() const;
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream) const;
			unsigned int numberOfTicks() const
			{
				return 1;
			}

			///Searches for any towers that do not have the right amount of units assigned, and assigns them.
			void controlTowers();

			///Holds a refernece to the ai so taht the module can work properly.
			AINicowar& ai;
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

	//The following constants deal with the function iteration. All of these must be
	//lower than TIMER_ITERATION.
	const unsigned int TIMER_INTERVAL=4;
	const unsigned int STARTUP_TIME=50;

	//These constants are for the AI's management of exploration and explorer attacks.
	//Warning, the following four constants must be powers of 2, because map sizes are in powers of two,
	//or the explorers will leave small strips at the right side of maps unexplored.
	const unsigned int EXPLORER_REGION_WIDTH=16;
	const unsigned int EXPLORER_REGION_HEIGHT=16;
	const int EXPLORER_REGION_HORIZONTAL_OVERLAP=8;
	const int EXPLORER_REGION_VERTICAL_OVERLAP=8;
	const int EXPLORER_REGION_HORIZONTAL_EXTENTION=0;
	const int EXPLORER_REGION_VERTICAL_EXTENTION=0;
	//Its reccomended that this number is an even number.
	const unsigned int EXPLORERS_PER_REGION=2;
	const unsigned int EXPLORATION_FLAG_RADIUS=12;
	const unsigned int EXPLORER_MAX_REGIONS_AT_ONCE=3;

	const unsigned int EXPLORER_ATTACK_AREA_WIDTH=8;
	const unsigned int EXPLORER_ATTACK_AREA_HEIGHT=8;
	const int EXPLORER_ATTACK_AREA_HORIZONTAL_OVERLAP=4;
	const int EXPLORER_ATTACK_AREA_VERTICAL_OVERLAP=4;
	const int EXPLORER_ATTACK_AREA_HORIZONTAL_EXTENTION=0;
	const int EXPLORER_ATTACK_AREA_VERTICAL_EXTENTION=0;
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
	const unsigned int BASE_DEFENSE_WARRIORS=10;

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
	const unsigned int MINIMUM_BARRACKS_LEVEL=0;
	const unsigned int MAX_ATTACKS_AT_ONCE=4;
	const unsigned int BASE_ATTACK_WARRIORS=static_cast<unsigned int>(MAX_ATTACKS_AT_ONCE*ATTACK_WARRIOR_MINIMUM*1.5);

	//The following are for the construction manager

	const unsigned int BUILD_AREA_WIDTH=8;
	const unsigned int BUILD_AREA_HEIGHT=8;
	const unsigned int BUILD_AREA_HORIZONTAL_OVERLAP=4;
	const unsigned int BUILD_AREA_VERTICAL_OVERLAP=4;
	const unsigned int BUILD_AREA_EXTENTION_WIDTH=8;
	const unsigned int BUILD_AREA_EXTENTION_HEIGHT=8;
	const unsigned int BUILDING_PADDING=2;
	///With the following enabled, the new construction manager will try to place buildings as close together as possible that still satisfy the padding.
	const bool CRAMP_BUILDINGS=true;
	const unsigned int NOPOS=1023;
	const unsigned int MINIMUM_NEARBY_BUILDINGS_TO_CONSTRUCT=0;
	const unsigned int CONSTRUCTION_FACTORS[IntBuildingType::NB_BUILDING][3][2]=
	{
	{{GridPollingSystem::MAXIMUM, GridPollingSystem::POLL_CORN}, {GridPollingSystem::MAXIMUM, GridPollingSystem::FRIENDLY_BUILDINGS}, {GridPollingSystem::MAXIMUM, GridPollingSystem::NONE}},
	{{GridPollingSystem::MAXIMUM, GridPollingSystem::POLL_CORN}, {GridPollingSystem::MAXIMUM, GridPollingSystem::FRIENDLY_BUILDINGS}, {GridPollingSystem::MAXIMUM, GridPollingSystem::NONE}},
	{{GridPollingSystem::MAXIMUM, GridPollingSystem::POLL_TREES}, {GridPollingSystem::MAXIMUM, GridPollingSystem::FRIENDLY_BUILDINGS}, {GridPollingSystem::MAXIMUM, GridPollingSystem::NONE}},
	{{GridPollingSystem::MAXIMUM, GridPollingSystem::FRIENDLY_BUILDINGS}, {GridPollingSystem::MAXIMUM, GridPollingSystem::NONE}, {GridPollingSystem::MAXIMUM, GridPollingSystem::NONE}},
	{{GridPollingSystem::MAXIMUM, GridPollingSystem::FRIENDLY_BUILDINGS}, {GridPollingSystem::MAXIMUM, GridPollingSystem::NONE}, {GridPollingSystem::MAXIMUM, GridPollingSystem::NONE}},
	{{GridPollingSystem::MAXIMUM, GridPollingSystem::POLL_STONE}, {GridPollingSystem::MAXIMUM, GridPollingSystem::POLL_TREES}, {GridPollingSystem::MAXIMUM, GridPollingSystem::FRIENDLY_BUILDINGS}},
	{{GridPollingSystem::MAXIMUM, GridPollingSystem::FRIENDLY_BUILDINGS}, {GridPollingSystem::MAXIMUM, GridPollingSystem::NONE}, {GridPollingSystem::MAXIMUM, GridPollingSystem::NONE}},
	{{GridPollingSystem::MINIMUM, GridPollingSystem::FRIENDLY_BUILDINGS}, {GridPollingSystem::MAXIMUM, GridPollingSystem::NONE}, {GridPollingSystem::MAXIMUM, GridPollingSystem::NONE}},
	{{GridPollingSystem::MAXIMUM, GridPollingSystem::NONE}, {GridPollingSystem::MAXIMUM, GridPollingSystem::NONE}, {GridPollingSystem::MAXIMUM, GridPollingSystem::NONE}},
	{{GridPollingSystem::MAXIMUM, GridPollingSystem::NONE}, {GridPollingSystem::MAXIMUM, GridPollingSystem::NONE}, {GridPollingSystem::MAXIMUM, GridPollingSystem::NONE}},
	{{GridPollingSystem::MAXIMUM, GridPollingSystem::NONE}, {GridPollingSystem::MAXIMUM, GridPollingSystem::NONE}, {GridPollingSystem::MAXIMUM, GridPollingSystem::NONE}},
	{{GridPollingSystem::MAXIMUM, GridPollingSystem::NONE}, {GridPollingSystem::MAXIMUM, GridPollingSystem::NONE}, {GridPollingSystem::MAXIMUM, GridPollingSystem::NONE}},
	{{GridPollingSystem::MAXIMUM, GridPollingSystem::NONE}, {GridPollingSystem::MAXIMUM, GridPollingSystem::NONE}, {GridPollingSystem::MAXIMUM, GridPollingSystem::NONE}}
	};

	const unsigned int MAX_NEW_CONSTRUCTION_AT_ONCE=6;
	const unsigned int MAX_NEW_CONSTRUCTION_PER_BUILDING[IntBuildingType::NB_BUILDING] =
		{1, 4, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0};
	const unsigned int MINIMUM_TO_CONSTRUCT_NEW=4;
	const unsigned int MAXIMUM_TO_CONSTRUCT_NEW=8;
	///How many units it requires to constitute construction another building, per type
	const unsigned int UNITS_FOR_BUILDING[IntBuildingType::NB_BUILDING] =
		{20, 6, 10, 20, 20, 20, 20, 0, 0, 0, 0, 0, 0};

	///This constant turns on debugging output
	const bool AINicowar_DEBUG = true;
	///@}

	//These are just some handy functions

	///Adapts syncRand to work as a RandomNumberFunctor for the std.
	inline unsigned int syncRandAdapter(unsigned int x)
	{
		return syncRand()%x;
	}

	///Shuffles the given list.
	template<typename T> void list_shuffle(std::list<T>& l)
	{
		std::vector<T> v(l.begin(), l.end());
		std::random_shuffle(v.begin(), v.end(), syncRandAdapter);
		typename std::list<T>::iterator i1 = l.begin();
		typename std::vector<T>::iterator i2 = v.begin();
		while (i1!=l.end())
		{
			(*i1)=(*i2);
			++i1;
			++i2;
		}
	}

	inline std::ostream& operator<<(std::ostream& o, const GridPollingSystem::zone& z)
	{
		o<<"zone(x="<<z.x<<", y="<<z.y<<", width="<<z.width<<", height="<<z.height<<")";
		return o;
	}

	inline unsigned int intdistance(unsigned int a, unsigned int b)
	{
		if(a>b)
			return a-b;
		return b-a;
	}

	///Returns the number of free units with the given ability and the given level in it.
	///It discounts hungry/hurt units.
	unsigned int getFreeUnits(Team* team, int ability, int level);

	///Returns the building* of the gid, or NULL
	inline Building* getBuildingFromGid(Game* game, int gid)
	{
		if(gid==NOGBID)
			return NULL;
		return game->teams[Building::GIDtoTeam(gid)]->myBuildings[Building::GIDtoID(gid)];
	}
	///Returns a unit* of the gid, or NULL
	inline Unit* getUnitFromGid(Game* game, int gid)
	{
		if(gid==NOGUID)
			return NULL;
		return game->teams[Unit::GIDtoTeam(gid)]->myUnits[Unit::GIDtoID(gid)];
	}

	///Returns true if the given building hasn't been destroyed
	bool buildingStillExists(Game* team, Building* b);
	///Returns true if the given building hasn't been destroyed
	bool buildingStillExists(Game* game, unsigned int gid);

}
#endif
