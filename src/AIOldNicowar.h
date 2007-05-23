/*
 Copyright 2005-2006 Bradley Arsenault

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
#ifndef __AI_OLD_NICOWAR_H
#define __AI_OLD_NICOWAR_H

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
#include <set>
#include "Order.h"
#include <boost/shared_ptr.hpp>

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
	///This constant turns on status output. status is output to the file "NicowarStatus.txt" in the current
	///working directory. It has plenty of information that explains nicowars choices, which is good for
	///fine tuning Nicowar as well as debugging it.
	const bool NicowarStatusUpdate = false;

	class AINicowar;
	class Module;
	class DefenseModule;
	class AttackModule;
	class NewConstructionModule;
	class UpgradeRepairModule;
	class UnitModule;
	class OtherModule;

	///This class serves as an adaptable full map gradient.
	class Gradient
	{
		public:
			///This enum holds anything that can be a source in the gradient
			enum Sources
			{
				VillageCenter=1<<0,
				Wheat=1<<1,
				Wood=1<<2,
				Stone=1<<3,
				TeamBuildings=1<<4,
				Water=1<<5,
			};

			///This enum holds anything that can be an obstacle in the gradient
			enum Obstacles
			{
				None=0,
				Resource=1<<0,
				Building=1<<1,
			};


			///Constructs an invalid gradient
			Gradient() {}
			///Constructs a new gredient
			Gradient(AINicowar& team, unsigned sources, unsigned obstacles);
			///Modifyies an existing gradient with new parameters
			void reset(AINicowar& team, unsigned sources, unsigned obstacles);
			///Updates the gradient from the given starting point outwards.
			///Resources count as obstacles
			void update();
			///Gets the height of point x,y
			int getHeight(int x, int y) const;

			///Outputs the gradient to the console, warning, very large.
			void output();
		private:
			bool isSource(unsigned x, unsigned y);
			bool isObstacle(unsigned x, unsigned y);
			unsigned width;
			unsigned height;
			unsigned sources;
			unsigned obstacles;
			Team* team;
			Map* map;
			AINicowar* ai;
			std::vector<short int> gradient;
	};

	///This class serves as a global gradient manager for the team, making sure that no gradients
	///are produced twice, and bundles all of the gradients together for routine updating
	class GradientManager
	{
		public:
			GradientManager() {};
			GradientManager(AINicowar* team) : team(team) {}
			void setTeam(AINicowar* aTeam)
			{
				team=aTeam;
			}
			Gradient& getGradient(unsigned sources, unsigned obstacles);
			void updateGradients();
		private:
			struct gradientSignature
			{
				gradientSignature(unsigned sources, unsigned obstacles) : sources(sources), obstacles(obstacles) {}
				unsigned sources;
				unsigned obstacles;
				bool operator<(const gradientSignature& cmp) const
				{
					if(sources!=cmp.sources)
						return sources<cmp.sources;
					return obstacles<cmp.obstacles;
				}
			};

			std::map<gradientSignature, Gradient> gradients;
			std::queue<std::map<gradientSignature, Gradient>::iterator> update_queue;
			AINicowar* team;

	};

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

			boost::shared_ptr<Order> getOrder(void);

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
			OtherModule* getOtherModule(std::string name);

			unsigned int getCenterX() const
			{
				return center_x;
			}

			unsigned int getCenterY() const
			{
				return center_y;
			}

			std::queue<boost::shared_ptr<Order> > orders;

			///This will remove all messages accocciatted with the given catagorizations
			void clearDebugMessages(std::string module, std::string group, std::string variable)
			{
				if(NicowarStatusUpdate)
					debug_messages[module][group][variable].clear();
			}
			///This will add a debug message to the given variable. Note the variable
			///does not actual have to be a variable name at all, its just a third level
			///of grouping for messages.
			void addDebugMessage(std::string module, std::string group, std::string variable, std::string message)
			{
				if(NicowarStatusUpdate)
					debug_messages[module][group][variable].push_back(message);
			}


			GradientManager& getGradientManager()
			{
				return gradient_manager;
			}

			void flare(unsigned x, unsigned y)
			{
				orders.push(boost::shared_ptr<Order>(new MapMarkOrder(team->teamNumber, x, y)));
			}
			void pause()
			{
				orders.push(boost::shared_ptr<Order>(new PauseGameOrder(true)));
			}
		private:

			std::map<std::string, std::map<std::string, std::map<std::string, std::vector<std::string> > > > debug_messages;
			void outputDebugMessages();


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
			std::map<std::string, OtherModule*> other_modules;
			std::vector<Module*> modules;
			unsigned int module_timer;
			std::vector<Module*>::iterator active_module;
			GradientManager gradient_manager;
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
				///Enemy warriors
				ENEMY_WARRIORS,
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
				bool operator>(const zone& cmp) const { if(x==cmp.x) return y>cmp.y; return x>cmp.x; }
				bool operator<(const zone& cmp) const { if(x==cmp.x) return y<cmp.y; return x<cmp.x; }

			};

			///Represents a single pollRecord. It has the information for the zone, as well as the score and type of information that as polled for.
			struct pollRecord
			{
				unsigned int x;
				unsigned int y;
				unsigned int width;
				unsigned int height;
				bool failed_constraint;
				unsigned int score;
				pollType poll_type;
				pollRecord(int ax, int ay, int awidth, int aheight, int ascore, pollType apoll_type) : x(ax), y(ay), width(awidth), height(aheight), score(ascore), poll_type(apoll_type) {}
				pollRecord() {}
				bool operator>(const pollRecord& cmp) const { if(failed_constraint==false && cmp.failed_constraint==true) return true; return score>cmp.score; }
				bool operator<(const pollRecord& cmp) const {  if(failed_constraint==true && cmp.failed_constraint==false) return true; return score<cmp.score; }
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
				bool is_strict_minimum;
				pollModifier mod_minimum;
				pollType minimum_type;
				unsigned int minimum_score;
				bool is_strict_maximum;
				pollModifier mod_maximum;
				pollType maximum_type;
				unsigned int maximum_score;
				poll() :    mod_1(MAXIMUM), type_1(NONE), mod_2(MAXIMUM), type_2(NONE), mod_3(MAXIMUM), type_3(NONE),
					is_strict_minimum(true), mod_minimum(MAXIMUM), minimum_type(NONE), minimum_score(0),
				 	is_strict_maximum(true), mod_maximum(MAXIMUM), maximum_type(NONE), maximum_score(0)
					{}
			};


			///This is the data that is stored between two calls to get best zones for split calculations
			struct getBestZonesSplit
			{
				poll p;
				unsigned int width;
				unsigned int height;
				int horizontal_overlap;
				int vertical_overlap;
				unsigned int extention_width;
				unsigned int extention_height;
				std::vector<pollRecord>* a_list;
				std::vector<pollRecord>* b_list;
				std::vector<pollRecord>* c_list;
			};


			///Returns the zones in order of scores of each poll type: a, b, and c,
			///where a is prioritized over b and b is prioritized over c. extention_width
			///and extention_height cause getBestZones to poll a zone larger than
			///what it would normally, so this allows for zones not to be strictly
			///scored in only there area, but surrounding areas can be considered
			///as well. The extentions are applied to both the top and the bottom
			///of the zone, so the total width or height of the zone increases by two
			///times the extention. The calculation is quite heavy and so is split into
			///two parts.
			getBestZonesSplit* getBestZones(poll p, unsigned int width, unsigned int height, int horizontal_overlap, int vertical_overlap, unsigned int extention_width, unsigned int extention_height);

			std::vector<zone> getBestZones(getBestZonesSplit* split_calc);
		private:
			Map* map;
			Team* team;
			Game* game;
			unsigned int center_x;
			unsigned int center_y;
	};

	///This class serves as an advanced statistics generator for a teams units and buildings.
	class TeamStatsGenerator
	{
		public:
			TeamStatsGenerator(Team* team);
			///Gets the number of units that follow the criteria. type is the type of unit. medical_state is the medical state of
			///the unit. activity is what the unit is doing. ability is the ability the unit should have to qualify. level is the
			///level of skill that unit should have in the ability, and isMinimum states whether the unit has to have exactly level
			///to qualify, or just have minimum of level, in which it can be higher. If level is 0, then it means the unit should
			///not have that ability, or have no skill in it.
			unsigned int getUnits(unsigned int type, Unit::Medical medical_state, Unit::Activity activity, unsigned int ability, unsigned int level, bool isMinimum);

			///Like the above method, this one returns the total number of units a person has meeting the criteria, not just ones
			///that go for a particular health of activity level
			unsigned int getUnits(unsigned int type, unsigned int ability, unsigned int level, bool isMinimum);

			///Returns the highest level that the team has for a particular building type, or 0 for none.
			unsigned int getMaximumBuildingLevel(unsigned int building_type);

			///The team that this team stats generator is connected to
			Team* team;
	};


	///Individual modules handle everything, after being connected to their respecting ai.
	class Module
	{
		public:
			///Destructs the module, deconnecting it from the base.
			virtual ~Module() {};
			///Asks the Module to perform something in its timeslice. If this returns true,
			///The main module will give it another tick, for split calculations. The function
			///will be called again with the same time_splice_n.
			virtual bool perform(unsigned int time_slice_n)=0;
			///Gets the name of this module.
			virtual std::string getName() const=0;
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
	///It implements a default distributation method, sub classes can choose to keep it or override it.
	class UnitModule : public Module
	{
		public:

			///This function sets the maximum number of units the module wants for a specific unit type, level, and priority.
			///This number must be the number of units the module would use, instantly, if granted them. This is with one
			///exception, units with a low priority should simply be created, and do not need to be used imediettly. Its
			///expectes that a module keep this number up to date, or the whole system can go down, because one module is being
			///offered units and its never using them.
			virtual void changeUnits(std::string moduleName, unsigned int unitType, unsigned int numUnits, unsigned int ability, unsigned int level)=0;

			///This function returns the number of units available to the given module, based on the number of free units,
			///how many units this module has already recieved, and how many it wants.
			virtual unsigned int available(std::string module_name, unsigned int unit_type, unsigned int ability, unsigned int level, bool is_minimum)=0;

			///This function returns true if the module can have the requested number of units for a specific task, and adds
			///the building to the record books and manages the number of free units so that building can constantly keep full.
			///This function can also be used to make changes on an existing building. Once you pass a building into UnitModule,
			///the UnitModule will keep the building full untill you change the number of assigned units.
			virtual bool request(std::string module_name, unsigned int unit_type, unsigned int ability, unsigned int minimum_level, unsigned int number, int building)=0;

                        virtual void reserve(std::string module_name, unsigned int unit_type, unsigned int ability, unsigned int minimum_level, unsigned int number)=0;
                        virtual void unreserve(std::string module_name, unsigned int unit_type, unsigned int ability, unsigned int minimum_level, unsigned int number)=0;
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
			bool perform(unsigned int time_slice_n);
			std::string getName() const;
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream) const;
			unsigned int numberOfTicks() const
			{
				return 3;
			}

			struct defenseRecord
			{
				unsigned int flag;
				unsigned int flagx;
				unsigned int flagy;
				unsigned int zonex;
				unsigned int zoney;
				unsigned int width;
				unsigned int height;
				unsigned int assigned;
				unsigned int building;
			};

			std::vector<defenseRecord> defending_zones;

			std::map<unsigned int, unsigned int> building_health;

			///Will check to see if there are any buildings under attack, if so, it will identify that buildings zone,
			///check to see if there aren't already warriors defending in that zone, and if not, add a flag with enough
			///assigned units to counter everything the enemy has *1.5. If the forces are overwhelming, tells the swarm
			///manager to create warriors on the double.
			bool findDefense();

			///Destroys defense flags that are defending zones that are no longer under enemy attack. It also reassigns
			///units to flags in order to keep up to date on what zones need how many defending warriors. Finally, it
			///will also call on the swarm to create units in a big rush if it has not enough.
			bool updateFlags();

			///When defense flags are created by findDefense, they aren't created instantly. Generally it takes 1 tick
			///later before the flag can be found and added to the records.
			bool findCreatedDefenseFlags();

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
			bool perform(unsigned int time_slice_n);
			std::string getName() const;
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

			std::vector<defenseRecord> defending_flags;

			///Searches for enemy flags that it needs to defend against, and if
			///it finds one, it will make a new flag.
			bool findEnemyFlags();

			///Looks through the defense records and updates the flags as nessecary
			///to be able to combat the opponents forces.
			bool updateDefenseFlags();

			AINicowar& ai;
	};

	///Moderates ground attacks on the enemy. After it has chosen what enemy to attack, it attacks buildings based on priority,
	///destroying all of one building type before moving onto the next.
	class PrioritizedBuildingAttack : public AttackModule
	{
		public:
			PrioritizedBuildingAttack(AINicowar& ai);
			~PrioritizedBuildingAttack() {};
			bool perform(unsigned int time_slice_n);
			std::string getName() const;
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream) const;
			unsigned int numberOfTicks() const
			{
				return 4;
			}

			///Stores all of the information accociated with a particular attack
			struct attackRecord
			{
				unsigned int target;
				unsigned int target_x;
				unsigned int target_y;
				unsigned int flag;
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
			bool targetEnemy();
			Team* enemy;

			///If we have enough warriors of the best available skill level, launch an attack!
			bool attack();

			///Find flags that attack() created, and update flags as neccecary to keep up with the number of units defending each building
			bool updateAttackFlags();

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
			bool perform(unsigned int time_slice_n);
			std::string getName() const;
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream) const;
			unsigned int numberOfTicks() const
			{
				return 3;
			}

			///Stores a single point on the map
			struct point
			{
				point() {}
				point(unsigned x, unsigned y) : x(x), y(y) {}
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

			///These are used to keep track of what zones can not be built in. It is stored in a cache,
			///and erased after a certain number of turns. It helps to lower how much it has to recompute
			///when there is little to no space left on the map to build anything.
			struct noBuildRecord
			{
				///The number of turns this record has been existant for
				unsigned int turns;
				///The smallest width of a building has been found not to fit into this zone
				unsigned int min_width;
				///The smallest height of a building has been found not to fit into this zone
				unsigned int min_height;
			};

			///Stores information for the creation of a new building.
			struct newConstructionRecord
			{
				unsigned int building;
				unsigned int x;
				unsigned int y;
				unsigned int assigned;
				unsigned int building_type;
				int no_build_timeout;
			};

			///Stores the percentage of buildings for a particular type there is.
			struct typePercent
			{
				unsigned int building_type;
				unsigned int percent;
				bool operator<(const typePercent& tp) const;
				bool operator>(const typePercent& tp) const;
			};

			///Stores the information for a single factor in deciding where a building should be placed
			struct GradientPoll
			{
				GradientPoll() : is_null(true) {}
				GradientPoll(Gradient::Sources source, Gradient::Obstacles obstacle, int weight) : is_null(false), source(source), obstacle(obstacle), weight(weight), min_dist(-1), max_dist(-1) {}
				GradientPoll(Gradient::Sources source, Gradient::Obstacles obstacle, int minimum_distance, int maximum_distance) : is_null(false), source(source), obstacle(obstacle), weight(1), min_dist(minimum_distance), max_dist(maximum_distance) {}
 				bool is_null;
				Gradient::Sources source;
				Gradient::Obstacles obstacle;
				int weight;
				int min_dist;
				int max_dist;
			};

			///Stores the various records of what is being built
			std::vector<newConstructionRecord> new_buildings;

			///Fings the largest size the building of a given type can have by iterating
			///through that buildings various levels and examining the sizes of the building
			///at the level, looking for the largest size. It will also set the offsets
			///correctly, which are used if a building expands in multiple directions,
			///if the current level is provided.
			upgradeData findMaxSize(unsigned int building_type, unsigned int cur_level);

			///Find the best spot to put a prticular kind of building. This function does
			///most of the calculation work.
			point findBestPlace(unsigned int building_type);

			///Constructs the various queued up buildings.
			bool constructBuildings();

			///Finds the buildings that constructBuildings has started construction of.
			bool updateBuildings();

			//Calculates how many of each type of building the ai should have.
			bool calculateBuildings();

			void updateImap();
			void updateImap(unsigned x, unsigned y, unsigned building_type);

			std::map<unsigned int, unsigned int> num_buildings_wanted;

			///The cache that stores all of the no-build records.
			std::map<GridPollingSystem::zone, noBuildRecord> no_build_cache;

			///This function updates the no build cache, removing expired records.
			void updateNoBuildCache();

			///Holds a refernece to the ai so taht the module can work properly.
			AINicowar& ai;

			std::vector<unsigned> imap;
	};

	///This module upgrades and repairs buildings at random, up to a designated maximum construction.
	///It only uses free workers.
	class RandomUpgradeRepairModule : public UpgradeRepairModule
	{
		public:
			RandomUpgradeRepairModule(AINicowar& ai);
			~RandomUpgradeRepairModule() {};
			bool perform(unsigned int time_slice_n);
			std::string getName() const;
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
			bool removeOldConstruction(void);

			///A building may have been issued a command to be upgraded/repaired, but it is only
			///constructed on when all the people are out of it. So this goes through the pending
			///building list for any buildings that are no longer pending (don't have any
			///people in them.), and then assigns the number of requested units for the upgrade/
			///repair to the building.
			bool updatePendingConstruction(void);

			///Looks through all of the buildings to see if any need to be upgraded or repaired.
			///If so, request construction, and push information including how many worker globs
			///it saw that where available onto the pending list. When the building is empty
			///and is ready to be constructed on, updatePendingUpgrades assign the number of
			///requested units to the building. Does nothing if it lacks units or available
			///buildings to upgrade.
			bool startNewConstruction(void);

			///This one is simple. It gets the numbers of available units to perform upgrades
			///first by counting the numbers of free units. Then it adds the numbers of all
			///the units already working on an upgrade. Then it reassigns the numbers of units
			///assigned to the various upgrades all over again, so this makes up for any units
			///that have become available since the start of the upgrades, or perhaps loss of
			///units.
			bool reassignConstruction(void);

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

	///This contains a bassic implementation of the UnitModule interface. It does not do everything
	///however, it just dishes out units. It is not a complete module.
	class DistributedUnitManager : public UnitModule
	{
		public:
			DistributedUnitManager(AINicowar& ai);
			~DistributedUnitManager() {};
			std::string getName() const;
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream) const;

			void changeUnits(std::string moduleName, unsigned int unitType, unsigned int numUnits, unsigned int ability, unsigned int level);
			unsigned int available(std::string module_name, unsigned int unit_type, unsigned int ability, unsigned int level, bool is_minimum);
			bool request(std::string module_name, unsigned int unit_type, unsigned int ability, unsigned int minimum_level, unsigned int number, int building);
                        void reserve(std::string module_name, unsigned int unit_type, unsigned int ability, unsigned int minimum_level, unsigned int number);
                        void unreserve(std::string module_name, unsigned int unit_type, unsigned int ability, unsigned int minimum_level, unsigned int number);
			void writeDebug();
		protected:
			///This stores all of the information needed for maintaining a particular building at its maximum unit consumption
			struct usageRecord
			{
				std::string owner;
				unsigned int x;
				unsigned int y;
				unsigned int type;
				unsigned int level;
				unsigned int ability;
				unsigned int unit_type;
				unsigned int minimum_level;
				unsigned int number;
			};

			///This stores all of the buildings and how many units they are using. A buildings usage record is both stored
			///here and in the related modules moduleRecord for effiency.
			std::map<int, usageRecord> buildings;

			///Module record
			struct moduleRecord
			{
				moduleRecord()
				{
					for(int i=0; i<NB_UNIT_TYPE; ++i)
						for(int j=0; j<NB_ABILITY; ++j)
							for(int k=0; k<NB_UNIT_LEVELS; ++k)
							{
								requested[i][j][k]=0;
								usingUnits[i][j][k]=0;
								reservedUnits[i][j][k]=0;
							}
				}
				///This is the number of requested units for various categories.
				unsigned int requested[NB_UNIT_TYPE][NB_ABILITY][NB_UNIT_LEVELS];
				///This is the units that this module in particular is using for
				unsigned int usingUnits[NB_UNIT_TYPE][NB_ABILITY][NB_UNIT_LEVELS];
				///These are units that are reserved by the module, and will be transfered to a building soon
				unsigned int reservedUnits[NB_UNIT_TYPE][NB_ABILITY][NB_UNIT_LEVELS];
			};

			int getUsagePercent(const std::string& module, int unit_type, int ability, int level);

			std::string getMinModule(const std::string& bias, int unit_type, int ability, int level);

			int getNeededUnits(int unit_type, int ability, int level, bool is_minimum);

			///This map stores the moduleRecords in accordance to their related module
			std::map<std::string, moduleRecord> module_records;

			///Holds a refernece to the ai so taht the module can work properly.
			AINicowar& ai;
			std::map<int, std::string> unit_names;
			std::map<int, std::string> ability_names;
	};


	///A subclass of distrbuted unit manager that completes the module with swarm management.
	class BasicDistributedSwarmManager : public DistributedUnitManager
	{
		public:
			BasicDistributedSwarmManager(AINicowar& ai);
			~BasicDistributedSwarmManager() {};
			bool perform(unsigned int time_slice_n);
			std::string getName() const;
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream) const;
			unsigned int numberOfTicks() const
			{
				return 1;
			}

			///Constructs the ratios of desired workers, turns off production if there are enough units,
			///otherwise uses the ratios to moderate all of the spawns to get enough workers. It will
			///increase the number of people working on the spawn depending on how many units are wanted.
			bool moderateSwarms();

			///Holds a refernece to the ai so taht the module can work properly.
			AINicowar& ai;
	};

	///The following deal with AINicowars management of explorers. For most of the time, it just orders the creation of explorers. But eventually,
	///it will try to launch explorer attacks.
	class ExplorationManager : public OtherModule
	{
		public:
			ExplorationManager(AINicowar& ai);
			~ExplorationManager() {};
			bool perform(unsigned int time_slice_n);
			std::string getName() const;
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream) const;
			unsigned int numberOfTicks() const
			{
				return 1;
			}
			///Controls the swarms in order to get the desired numbers of explorers. It puts
			///the explorers creation at top priority, and if multiple explorers need to be
			///created, will distribute them between its swarms.
			bool moderateSwarmsForExplorers(void);


			///Thisd is the number of explorers wanted
			unsigned int explorers_wanted;
			///This variable is only changed when the actual orders are sent to the unit module to request explorers,
			///it keeps track of when numbers change so that the ai knows when to use UnitModule::reserve
			unsigned int original_explorers_wanted;
			///Holds a refernece to the ai so taht the module can work properly.
			AINicowar& ai;
	};

	///Performs usage recording coupled with supply recording
	class InnManager : public OtherModule
	{
		public:
			InnManager(AINicowar& ai);
			~InnManager() {};
			bool perform(unsigned int time_slice_n);
			std::string getName() const;
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream) const;
			unsigned int numberOfTicks() const
			{
				return 2;
			}

			struct singleInnRecord
			{
				explicit singleInnRecord(unsigned int food_amount=0) : food_amount(food_amount) {}
				unsigned int food_amount;
			};
			struct innRecord
			{
				innRecord();
				unsigned int pos;
				std::vector<singleInnRecord> records;
			};

			///Maps the gid of buildings to their respective innRecord.
			std::map<int, innRecord> inns;

			///Adds one more recording to the innRecord for each of the inns. Uses round robin recording in order
			///to get untainted, recent averages.
			bool recordInns();

			///Uses the simple, yet advanced, statistical anylysis algorithm to increase workers on particular inns.
			bool modifyInns();

			///Holds a refernece to the ai so taht the module can work properly.
			AINicowar& ai;
	};

	///Does the reletivly simple task of assigning units to towers.
	class TowerController : public OtherModule
	{
		public:
			TowerController(AINicowar& ai);
			~TowerController() {};
			bool perform(unsigned int time_slice_n);
			std::string getName() const;
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream) const;
			unsigned int numberOfTicks() const
			{
				return 1;
			}

			///Searches for any towers that do not have the right amount of units assigned, and assigns them.
			bool controlTowers();

			///Holds a refernece to the ai so taht the module can work properly.
			AINicowar& ai;
	};

	///This will put clearing-area padding around buildings.
	class BuildingClearer : public OtherModule
	{
		public:
			BuildingClearer(AINicowar& ai);
			~BuildingClearer() {};
			bool perform(unsigned int time_slice_n);
			std::string getName() const;
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream) const;
			unsigned int numberOfTicks() const
			{
				return 2;
			}

			struct clearingRecord
			{
				unsigned int x;
				unsigned int y;
				unsigned int height;
				unsigned int width;
				unsigned int level;
			};


			std::map<int, clearingRecord> cleared_buildings;

			///Removes the padding around buildings that have been destroyed or upgraded (and their size has changed)
			bool removeOldPadding();

			///Looks for buildings that haven't had padding added to them
			bool updateClearingAreas();

			///Holds a refernece to the ai so taht the module can work properly.
			AINicowar& ai;
	};

	///This module will check if it has a higher happiness level than any opponents, and if it does,
	///it will open up in view to those opponents, to steal units. It will also set up explorer
	///flags on nearby fruit trees.
	class HappinessHandler : public OtherModule
	{
		public:
			HappinessHandler(AINicowar& ai);
			~HappinessHandler();
			bool perform(unsigned int time_slice_n);
			std::string getName() const;
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream) const;
			unsigned int numberOfTicks() const
			{
				return 2;
			}

			///Stores a single point on the map
			struct point
			{
				point() {}
				point(int x, int y) : x(x), y(y) {}
				int x;
				int y;
				bool operator<(const point& cmp) const
				{
					if(x!=cmp.x)
						return x<cmp.x;
					return y<cmp.y;
				}
			};

			struct fruitTreeRecord
			{
				unsigned int fruit_tree_max_x;
				unsigned int fruit_tree_max_y;
				unsigned int fruit_tree_min_y;
				unsigned int fruit_tree_min_x;
				int fruit_tree_type;
			};

			std::vector<fruitTreeRecord> fruit_trees;
			bool is_fruit_trees_computed;
			void computeFruitTrees();

			struct fruitTreeExplorationRecord
			{
				unsigned flag;
				int pos_x;
				int pos_y;
				unsigned radius;
			};
			std::vector<fruitTreeExplorationRecord> exploring_fruit_trees;

			///This will change the alliances approprietly depending on the average happiness level.
			bool adjustAlliances();

			///Wil hunt out groups of fruit trees and put explorer flags on them
			bool searchFruitTrees();


			///Holds a refernece to the ai so taht the module can work properly.
			AINicowar& ai;
	};

	///This module will put restricted zones on resources in order to maintain the resources.
	///This is known as "farming", and is explained on the wiki.
	class Farmer : public OtherModule
	{
		public:
			Farmer(AINicowar& ai);
			~Farmer();
			bool perform(unsigned int time_slice_n);
			std::string getName() const;
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream) const;
			unsigned int numberOfTicks() const
			{
				return 1;
			}

			enum FarmingMethod
			{
				CheckerBoard,
				CrossSpacing,
				Row4,
				Column4,
			};

			///This structure holds a point, with including comparison operators so that it can have
			///O(log2(n)) lookup times in a container such as set or map
			struct point
			{
				unsigned int x;
				unsigned int y;

				point(unsigned int ax, unsigned int ay) : x(ax), y(ay) {}
				point() : x(0), y(0) {}

				bool operator>(const point& cmp) const
				{
					if(x==cmp.x)
						return y>cmp.y;
					return x>cmp.x;
				}

				bool operator<(const point& cmp) const
				{
					if(x==cmp.x)
						return y<cmp.y;
					return x<cmp.x;
				}
			};

			std::set<point> resources;

			///This function goess through every square on the map, if it is a resource, not hidden,
			///on an even numbered x or a even numbered y, not both and not neither, and it doesn't
			///already have a Forbidden area on it, *and* it doesn't have clearing area on it, then
			///it will place forbidden area on it. It will remove forbidden area if there is forbidden
			///area on that square, but no resource or if clearing area has been put over it
			bool updateFarm();

			///Holds a reference to the ai so taht the module can work properly.
			AINicowar& ai;
			Gradient water_gradient;
			bool is_water_gradient_computed;
	};

	///These constants are what fine tune AINicowar. There is allot of them.
	///@{

	///This boolean tells the ai to uncover the map right at the start, for debugging.
	const bool SEE_EVERYTHING=false;

	//The following deal with the upgrade and repair management system.
	const unsigned int MINIMUM_TO_UPGRADE=4;
	const unsigned int MAXIMUM_TO_UPGRADE=8;
	const unsigned int MINIMUM_TO_REPAIR=2;
	const unsigned int MAXIMUM_TO_REPAIR=8;
	const unsigned int BUILDINGS_FOR_UPGRADE=5;
	const int MAX_BUILDING_SPECIFIC_CONSTRUCTION_LIMITS[IntBuildingType::NB_BUILDING]=
		{0, 4, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 0};
	const unsigned int BUILDING_UPGRADE_WEIGHTS[IntBuildingType::NB_BUILDING]=
		{0, 6, 8, 10, 10, 20, 10, 8, 0, 0, 0, 0, 0};

	//The following constants deal with the function iteration. All of these must be
	//lower than TIMER_ITERATION.
	const unsigned int TIMER_INTERVAL=4;
	const unsigned int STARTUP_TIME=50;

	//These constants are for the AI's management of exploration and explorer attacks.
	const unsigned int TOTAL_EXPLORERS=8;


	//These constants are for the AI's swarm controller.
	//The number of units to assign to a swarm
	const unsigned int MAXIMUM_UNITS_FOR_SWARM=5;

	//These constants are for the AI's inn manager.
	//Says how many records it should take for each inn before restarting back at the begginning.
	const unsigned int INN_RECORD_MAX=10;
	///This is the amount of wheat that is needed to constitute assigning an extra unit to the inn.
	const unsigned int WHEAT_NEEDED_FOR_UNIT=3;
	const unsigned int INN_MAX[3]={3, 6, 8};
	const unsigned int INN_MINIMUM[3]={1, 2, 2};

	//These constants are for AINicowars tower controller
	const unsigned int NUM_PER_TOWER=2;

		//These constants are for the defense system.
	const unsigned int DEFENSE_ZONE_BUILDING_PADDING=3;
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
	const IntBuildingType::Number IGNORED_BUILDINGS[3] =
	{
		IntBuildingType::EXPLORATION_FLAG,
		IntBuildingType::WAR_FLAG,
		IntBuildingType::CLEARING_FLAG
	};

	const unsigned int ATTACK_ZONE_BUILDING_PADDING=1;
	const unsigned int ATTACK_ZONE_EXAMINATION_PADDING=10;
	const unsigned int ATTACK_WARRIOR_MINIMUM=8;
	///As opposed to the above variable, this is the minimum number of units it needs to start a new attack, rather than the minimum it will send.
	const unsigned int MINIMUM_TO_ATTACK=10;
	const unsigned int MINIMUM_BARRACKS_LEVEL=0;
	const unsigned int MAX_ATTACKS_AT_ONCE=4;
	const unsigned int WARRIOR_FACTOR=3;
	const unsigned int BASE_ATTACK_WARRIORS=static_cast<unsigned int>(MAX_ATTACKS_AT_ONCE*ATTACK_WARRIOR_MINIMUM*WARRIOR_FACTOR);
	///In order to counter the fact that once the ai started building warriors, it wouldn't continue building enough workers,
	///i've enabled "chunk construction", meaning the ai will build warriors in chunks rather than all at once. This number
	///provides a starting point, meaning it will always be developing atleast this number of new warriors, and chunks are
	///done on top of this.
	const unsigned int WARRIOR_DEVElOPMENT_CONSISTANT_SIZE=30;
	///This is the size of chunks for the ai.
	const unsigned int WARRIOR_DEVELOPMENT_CHUNK_SIZE=5;
	///The number of warriors per available space to train them, for example, a level 1 barracks would have 2 spaces, and thus
	///the game would produce 2*WARRIORS_PER_BARRACKS_TRAINING_SPACE. Higher level barracks offer more spaces.
	const unsigned int WARRIORS_PER_BARRACKS_TRAINING_SPACE=3;
	///This variable denotes whether the attack system should use the maximum barracks level it has available, or the average attack level of its units.
	const bool USE_MAX_BARRACKS_LEVEL=false;

	//The following are for the construction manager
	const unsigned int BUILDING_PADDING=1;
	const unsigned int NO_POSITION=1023;

	const unsigned CONSTRUCTOR_FACTORS_COUNT=3;

	const unsigned MAXIMUM_DISTANCE_TO_BUILDING=8;
	typedef DistributedNewConstructionManager::GradientPoll GradientPoll;
	const GradientPoll CONSTRUCTION_FACTORS[IntBuildingType::NB_BUILDING][CONSTRUCTOR_FACTORS_COUNT] = 
		{{	GradientPoll(Gradient::Wheat, Gradient::None, 4),
			GradientPoll(Gradient::TeamBuildings, Gradient::Resource, 2), 
			GradientPoll(Gradient::VillageCenter, Gradient::Resource, 1)}, //swarm

		 {	GradientPoll(Gradient::Wheat, Gradient::None, 4), 
			GradientPoll(Gradient::TeamBuildings, Gradient::Resource, 2), 
			GradientPoll(Gradient::VillageCenter, Gradient::Resource, 1)}, //inn

		 {	GradientPoll(Gradient::Wood, Gradient::None, 2), 
			GradientPoll(Gradient::TeamBuildings, Gradient::Resource, 2), 
			GradientPoll(Gradient::VillageCenter, Gradient::Resource, 1)}, //hospital

		 {	GradientPoll(Gradient::VillageCenter, Gradient::Resource, 1), 
			GradientPoll(Gradient::TeamBuildings, Gradient::Resource, 2), 
			GradientPoll()}, //racetrack

		 {	GradientPoll(Gradient::VillageCenter, Gradient::Resource, 1), 
			GradientPoll(Gradient::TeamBuildings, Gradient::Resource, 2), 
			GradientPoll()}, //swimming pool

		 {	GradientPoll(Gradient::Stone, Gradient::None, 4), 
			GradientPoll(Gradient::TeamBuildings, Gradient::Resource, 2), 
			GradientPoll(Gradient::VillageCenter, Gradient::Resource, 1)}, //barracks

		 {	GradientPoll(Gradient::VillageCenter, Gradient::Resource, 1), 
			GradientPoll(Gradient::TeamBuildings, Gradient::Resource, 2), 
			GradientPoll(Gradient::Water, Gradient::None, 10, -1)}, //school

		 {	GradientPoll(Gradient::TeamBuildings, Gradient::Resource, 3),
			GradientPoll(Gradient::Stone, Gradient::None, 1),
			GradientPoll()}, //Tower

		 {	GradientPoll(), GradientPoll(), GradientPoll()},
		 {	GradientPoll(), GradientPoll(), GradientPoll()},
		 {	GradientPoll(), GradientPoll(), GradientPoll()},
		 {	GradientPoll(), GradientPoll(), GradientPoll()}};


	///This represents for every n buildings the team has, allow one to be upgraded
	const unsigned int MAX_NEW_CONSTRUCTION_AT_ONCE=8;
	const unsigned int MAX_NEW_CONSTRUCTION_PER_BUILDING[IntBuildingType::NB_BUILDING] =
		{2, 4, 3, 1, 1, 2, 2, 2, 0, 0, 0, 0, 0};
	const unsigned int MINIMUM_TO_CONSTRUCT_NEW=4;
	const unsigned int MAXIMUM_TO_CONSTRUCT_NEW=8;
	///How many units it requires to constitute construction another building, per type
	const unsigned int UNITS_FOR_BUILDING[IntBuildingType::NB_BUILDING] =
		{30, 12, 16, 80, 80, 30, 50, 30, 0, 0, 0, 0, 0};
	///This is non-strict prioritizing, meaning that the priorities are used as multipliers on the percentages used
	///for comparison. In otherwords, the lowest priorites will *almost* always be constructed first, however,
	///in more extreme situations, higher priorites may be constructed first, even when its are missing lower
	///priority buildings.
	const unsigned int WEAK_NEW_CONSTRUCTION_PRIORITIES[IntBuildingType::NB_BUILDING] =
		{4, 2, 4, 6, 5, 4, 5, 5, 0, 0, 0, 0};
	///Buildings with a higher strict priority will *always* go first
	const unsigned int STRICT_NEW_CONSTRUCTION_PRIORITIES[IntBuildingType::NB_BUILDING] =
		{2, 2, 2, 1, 1, 1, 1, 2, 0, 0, 0, 0};
	///The number of turns before a cached no-build zone gets erased
	const unsigned int NO_BUILD_CACHE_TIMEOUT=1;
	///The number of turns before a building record that refers to a building that was destroyed before update gets removed
	const unsigned int BUILDING_RECORD_TIMEOUT=5;

	///This this enabled, buildings are constructed instantly, which is cheating,
	///although can aid in debugging in certain situations
	const bool CHEAT_INSTANT_BUILDING=false;
	//These constants are for GeneralsDefense
	const unsigned int DEFENSE_ZONE_SIZE_INCREASE=2;

	//These constants are for BuildingClearer
	const unsigned int CLEARING_AREA_BUILDING_PADDING=1;

	//These constants are for HappinessHandler
	const unsigned int EXPLORERS_PER_GROUP=2;
	const unsigned int REQUESTED_EXPLORERES=4;
	const unsigned int MINIMUM_FLAG_SIZE=3;

	//These are for farmer
	const Farmer::FarmingMethod FARMING_METHOD=Farmer::CrossSpacing;
	const unsigned int MAX_DISTANCE_FROM_WATER=8;

	///This constant turns on debugging output
	const bool AINicowar_DEBUG = false;
	///@}

	//These are just some handy functions

	///Adapts syncRand to work as a RandomNumberFunctor for the std.
	inline unsigned int syncRandAdapter(unsigned int x)
	{
		return syncRand()%x;
	}

	
	inline bool buildingAttackPredicate(Building* a, Building* b)
	{
		if(a->constructionResultState==Building::NO_CONSTRUCTION && b->constructionResultState!=Building::NO_CONSTRUCTION)
			return true;
		else if(a->constructionResultState!=Building::NO_CONSTRUCTION && b->constructionResultState==Building::NO_CONSTRUCTION)
			return false;
		return syncRand()%2;
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

	///Implements a selection sort algorithm, which is usefull because it enables predicate sorting, or weighted random sorting etc based
	///on the predicate. the iter type is any forward iterator, and predicate is a functor that takes in two iter::value_type's and returns
	///true if the first should precede the second.
	template <typename iter, typename predicate> void selection_sort(iter b,  iter e, predicate p)
	{
		for( ; b != e; ++b )
		{
			iter_swap( b, min_element(b, e, p) );
		}
	}

	inline bool weighted_random_upgrade_comparison(Building* a, Building* b)
	{
		if(BUILDING_UPGRADE_WEIGHTS[a->type->shortTypeNum]==0)
			return false;
		if(BUILDING_UPGRADE_WEIGHTS[b->type->shortTypeNum]==0)
			return true;
		unsigned int num_a=syncRand()%BUILDING_UPGRADE_WEIGHTS[a->type->shortTypeNum];
		unsigned int num_b=syncRand()%BUILDING_UPGRADE_WEIGHTS[b->type->shortTypeNum];
		if(num_a>num_b)
			return true;
		return false;
	}


	inline int round_up(unsigned int a, unsigned int b)
	{
		return (a-a%b)+b;
	}
}
#endif
