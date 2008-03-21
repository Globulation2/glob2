/*
  Copyright (C) 2006 Bradley Arsenault

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

#ifndef AINicowar_h
#define AINicowar_h

#include "AIEcho.h"
#include "ConfigFiles.h"

///This class represents the configuragle strategy that Nicowar will take. It uses the same algorithms,
///but this can allow it to fine tune the variables in those algorithms
class NicowarStrategy : LoadableFromConfigFile
{
public:
	///Constructs a new NicowarStrategy
	NicowarStrategy();

	///Loads the strategy from the configuration block
	void loadFromConfigFile(const ConfigBlock *configBlock);
	
	///Returns the name of the strategy
	std::string getStrategyName();
	
	///Sets the name of the strategy
	void setStrategyName(const std::string& name);
	
	///Maximum number of units for the growth phase
	int growth_phase_unit_max;
	///Minimum number of units for the skilled work phase
	int skilled_work_phase_unit_min;
	///Minimum number of schools to activate the upgrading phase 1
	int upgrading_phase_1_school_min;
	///Minimum number of units to activate the upgrading phase 1
	int upgrading_phase_1_unit_min;
	///Minimum number of trained workers to activate the upgrading phase 1
	int upgrading_phase_1_trained_worker_min;
	///Minimum number of level 2/3 schools to activate the upgrading phase 2
	int upgrading_phase_2_school_min;
	///Minimum number of units to activate the upgrading phase 2
	int upgrading_phase_2_unit_min;
	///Minimum number of trained workers to activate the upgrading phase 2
	int upgrading_phase_2_trained_worker_min;
	///The minimum warrior level to be considered "trained" for the war preperation and war phases, starting at 0
	int minimum_warrior_level_for_trained;
	///Minimum number of units to activate the war preperation phase
	int war_preperation_phase_unit_min;
	///Maximum number of barracks to activate the war preperation phase
	int war_preperation_phase_barracks_max;
	///Maximum number of trained warriors to activate the war preperation phase
	int war_preperation_phase_trained_warrior_max;
	///Minimum number of trained warriors to activate the war phase
	int war_phase_trained_warrior_min;
	///Minimum number of units to activate the fruit phase
	int fruit_phase_unit_min;
	///Minimum percentage of starving units that can't find an Inn to eat at to trigger the starvation recovery phase
	int starvation_recovery_phase_starving_no_inn_min_percent;
	///How many un-fed units to trigger placing a starving recovery Inn when in the starving recovery phase
	int starving_recovery_phase_unfed_per_new_inn;
	///Minimum, percentage of workers free to trigger the no workers phase
	int no_workers_phase_free_worker_minimum_percent;
	///How many units a level 1 Inn can feed, used to determine when more inns are needed
	int level_1_inn_units_can_feed;
	///How many units a level 2 Inn can feed, used to determine when more inns are needed
	int level_2_inn_units_can_feed;
	///How many units a level 3 Inn can feed, used to determine when more inns are needed
	int level_3_inn_units_can_feed;
	///How many units it takes to consitute a new swarm when in the growth phase
	int growth_phase_units_per_swarm;
	///How many units it takes to consitute a new swarm when not in the growth phase
	int non_growth_phase_units_per_swarm;
	///The maximum number of swarms that can be made during the growth phase
	int growth_phase_maximum_swarms;
	///The number of racetracks to be built during the skilled work phase
	int skilled_work_phase_number_of_racetracks;
	///The number of swimmingpools to be built during the skilled work phase
	int skilled_work_phase_number_of_swimmingpools;
	///The number of schools to be built during the skilled work phase
	int skilled_work_phase_number_of_schools;
	///The number of barracks to be built during the war preperation phase
	int war_preparation_phase_number_of_barracks;
	///The base number of hospitals to build, these are only built when there is first demand
	int base_number_of_hospitals;
	///The number of warriors required to trigger another hospital
	int war_preperation_phase_warriors_per_hospital;
	///The base number of construction sites that can go at once
	int base_number_of_construction_sites;
	///The number of extra construction sites when in starving recovery mode (to facilitate extra inns to be constructed0
	int starving_recovery_phase_number_of_extra_construction_sites;
	///The ammount of wheat that triggers a level 1 inn to increase the units working
	int level_1_inn_low_wheat_trigger_ammount;
	///The ammount of wheat that triggers a level 2 inn to increase the units working
	int level_2_inn_low_wheat_trigger_ammount;
	///The ammount of wheat that triggers a level 3 inn to increase the units working
	int level_3_inn_low_wheat_trigger_ammount;
	///The number of units to assign to an level 1 inn when its wheat is above the trigger
	int level_1_inn_units_assigned_normal_wheat;
	///The number of units to assign to an level 2 inn when its wheat is above the trigger
	int level_2_inn_units_assigned_normal_wheat;
	///The number of units to assign to an level 3 inn when its wheat is above the trigger
	int level_3_inn_units_assigned_normal_wheat;
	///The number of units to assign to an level 1 inn when its wheat is below the trigger
	int level_1_inn_units_assigned_low_wheat;
	///The number of units to assign to an level 2 inn when its wheat is below the trigger
	int level_2_inn_units_assigned_low_wheat;
	///The number of units to assign to an level 3 inn when its wheat is below the trigger
	int level_3_inn_units_assigned_low_wheat;
	///The base number of units to assign to a swarm, which is doubled/halved based on conditions
	int base_swarm_units_assigned;
	///The ammount of ressources that caused a swarm to double its units assigned if it goes below this ammount
	int base_swarm_low_wheat_trigger_ammount;
	///The percentage of units that are hungry/starving that will cause swarms to halve the number of units assigned, (more wheat to inns)
	int base_swarm_hungry_reduce_trigger_percent;
	///The ratio of workers assigned to swarms during the growth phase
	int growth_phase_swarm_worker_ratio;
	///The ratio of workers assigned to swarms when not in the growth phase
	int non_growth_phase_swarm_worker_ratio;
	///The base number of explorers needed
	int base_number_of_explorers;
	///The extra number of explorers needed during the fruit phase
	int fruit_phase_extra_number_of_explorers;
	///The base ratio of explorers to use when explorers are needed
	int base_swarm_explorer_ratio;
	///The warrior ratio during the war preperation phase
	int war_preperation_swarm_warrior_ratio;
	///The random chance that, when selecting the type of level 1 building to upgrade, it will choose an inn
	int upgrading_phase_1_inn_chance;
	///The random chance that, when selecting the type of level 1 building to upgrade, it will choose an hospital
	int upgrading_phase_1_hospital_chance;
	///The random chance that, when selecting the type of level 1 building to upgrade, it will choose an racetrack
	int upgrading_phase_1_racetrack_chance;
	///The random chance that, when selecting the type of level 1 building to upgrade, it will choose an swimmingpool
	int upgrading_phase_1_swimmingpool_chance;
	///The random chance that, when selecting the type of level 1 building to upgrade, it will choose an barracks
	int upgrading_phase_1_barracks_chance;
	///The random chance that, when selecting the type of level 1 building to upgrade, it will choose an school
	int upgrading_phase_1_school_chance;
	///The random chance that, when selecting the type of level 1 building to upgrade, it will choose an tower
	int upgrading_phase_1_tower_chance;
	///The random chance that, when selecting the type of level 2 building to upgrade, it will choose an inn
	int upgrading_phase_2_inn_chance;
	///The random chance that, when selecting the type of level 2 building to upgrade, it will choose an hospital
	int upgrading_phase_2_hospital_chance;
	///The random chance that, when selecting the type of level 2 building to upgrade, it will choose an racetrack
	int upgrading_phase_2_racetrack_chance;
	///The random chance that, when selecting the type of level 2 building to upgrade, it will choose an swimmingpool
	int upgrading_phase_2_swimmingpool_chance;
	///The random chance that, when selecting the type of level 2 building to upgrade, it will choose an barracks
	int upgrading_phase_2_barracks_chance;
	///The random chance that, when selecting the type of level 2 building to upgrade, it will choose an school
	int upgrading_phase_2_school_chance;
	///The random chance that, when selecting the type of level 2 building to upgrade, it will choose an tower
	int upgrading_phase_2_tower_chance;
	///The number of units to assign to an upgrade for upgrading phase level 1
	int upgrading_phase_1_units_assigned;
	///The number of units to assign to an upgrade for upgrading phase level 2
	int upgrading_phase_2_units_assigned;
	///The number of level 1 or higher units needed to count for upgrading one more building
	int upgrading_phase_1_num_units;
	///The number of level 1 or higher units needed to count for upgrading one more building
	int upgrading_phase_2_num_units;
	///The number of units to assign to a war flag attacking an enemy building
	int war_phase_war_flag_units_assigned;
	///The number of flags to attack with at any one time
	int war_phase_num_attack_flags;
private:
	///The name of the strategy.
	std::string name;
};

///This class is meant to load a Nicowar Strategy
class NicowarStrategyLoader : ConfigVector<NicowarStrategy>
{
public:
	///Loads all strategies from the strategy files
	NicowarStrategyLoader();
	
	///This chooses a strategy at random
	NicowarStrategy chooseRandomStrategy();
	
	///This chooses a strategy with a particular name
	NicowarStrategy getParticularStrategy(const std::string& name);
};

///Nicowar is a new powerhouse AI for Globulation 2
class NewNicowar : public AIEcho::EchoAI
{
public:
	NewNicowar();
	bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
	void save(GAGCore::OutputStream *stream);
	void tick(AIEcho::Echo& echo);
	void handle_message(AIEcho::Echo& echo, const std::string& message);
private:
	///This function loads up all available strategies, and selects one at random.
	///As such, Nicowar may be going war-rush style, or it may try a longer game.
	void selectStrategy();

	///This is the basic, variable strategy that Nicowar will be taking at all times
	NicowarStrategy strategy;

	///These are all of the various buildings that can be constructed. Note that,
	///while their may be more than one for a particular type of building, the
	///different orders have different properties for deciding where to place the
	///building
	enum BuildingPlacement
	{
		RegularInn,
		StarvingRecoveryInn,
		RegularSwarm,
		RegularRacetrack,
		RegularSwimmingpool,
		RegularSchool,
		RegularBarracks,
		RegularHospital,
		PlacementSize,
	};

	///This function is called at the very begginning of the game,
	///to initialize the existing buildings with the right amount of units
	void initialize(AIEcho::Echo& echo);

	///This function is called periodically to choose the strategies
	///(called phases) that will be used at that time
	void check_phases(AIEcho::Echo& echo);
	///During the growth phase, the object of the game is the fast growth of economy.
	///Swarms become a big priority, so does gaining territory and exploration.
	bool growth_phase;
	///During the skilled work phase, Nicowar tries to better the skills of its workers
	///by building 1 Racetrack, 1 Swimmingpool and 2 Schools
	bool skilled_work_phase;
	///During the upgrading_phase_1 , Nicowar tries to upgrade its level 1 buildings in small numbers.
	bool upgrading_phase_1;
	///During the upgrading_phase_2 , Nicowar tries to upgrade its level 2 buildings in small numbers.
	bool upgrading_phase_2;
	///During the war preperation phase, warriors are made and barracks are prioritized
	bool war_preperation;
	///During the war phase, the enemies are attacked
	bool war;
	///During the fruit phase, explorers are stationed on fruit trees and Nicowar starts converting
	///enemy units
	bool fruit_phase;
	///During the starving recovery phase, swarms are turned off and extra inns may be created
	bool starving_recovery;
	///During the no worker phase, no workers are created, which occurs if there are too many free
	bool no_workers_phase;
	///During this phase, the colonies workers are "able to swim"
	bool can_swim;


	///This function decides how many buildings need to be constructed (and with what properties
	///their location must be picked with) and adds them to a queue. Most of the decisions are
	///made using statistics and depend on the phases that are activated
	void queue_buildings(AIEcho::Echo& echo);
	///This function decides how many Inns and of what placements need construction and adds
	///them to the queue
	void queue_inns(AIEcho::Echo& echo);
	///This function decides how many swarms are needed and queues them up
	void queue_swarms(AIEcho::Echo& echo);
	///This function decides how many racetracks are needed and queues them up.
	void queue_racetracks(AIEcho::Echo& echo);
	///This function decides how many swimmingpools are needed and queues them up.
	void queue_swimmingpools(AIEcho::Echo& echo);
	///This function decides how many schools are needed and queues them up.
	void queue_schools(AIEcho::Echo& echo);
	///This function decides how many barracks are needed and queues them up.
	void queue_barracks(AIEcho::Echo& echo);
	///This function decides how many hospitals are needed and queues them up.
	void queue_hospitals(AIEcho::Echo& echo);
	///This counts how many StarvingRecoveryInn's there are under construction
	int starving_recovery_inns;

	///This function starts construction on buildings that are queued for construction. Its carefull
	///not to construct too much or too little at once
	void order_buildings(AIEcho::Echo& echo);
	///This function starts construction of a RegularInn, and returns the ID code
	int order_regular_inn(AIEcho::Echo& echo);
	///This function starts construction of a RegularSwarm, and returns the ID code
	int order_regular_swarm(AIEcho::Echo& echo);
	///This function starts construction of a RegularSwarm, and returns the ID code
	int order_regular_racetrack(AIEcho::Echo& echo);
	///This function starts construction of a RegularSwarm, and returns the ID code
	int order_regular_swimmingpool(AIEcho::Echo& echo);
	///This function starts construction of a RegularSwarm, and returns the ID code
	int order_regular_school(AIEcho::Echo& echo);
	///This function starts construction of a RegularBarracks, and returns the ID code
	int order_regular_barracks(AIEcho::Echo& echo);
	///This function starts construction of a RegularHospital, and returns the ID code
	int order_regular_hospital(AIEcho::Echo& echo);
	///This integer stores the total number of buildings that are currently being constructed
	int buildings_under_construction;
	///This integer stores the number of buildings being constructed based on their placement id,
	///it counts buildings that are queued to be constructed but have not been started yet
	int buildings_under_construction_per_type[PlacementSize];
	///This is the queue for buildings that have yet to be proccessed for construction
	std::list<BuildingPlacement> placement_queue;
	///This is the queue for buildings that are going to be constructed
	std::list<BuildingPlacement> construction_queue;


	///This function updates all of the buildings that are not under construction.
	void manage_buildings(AIEcho::Echo& echo);
	///This function updates the units assigned to a particular Inn.
	///Using the messaging system, it is called after the completion
	///of a new Inn and periodically thereafter
	void manage_inn(AIEcho::Echo& echo, int id);
	///This function updated the units assigned and the creation
	///ratios of a particular Swarm. Its done afeter the completion
	///of a new swarm and periodically thereafter
	void manage_swarm(AIEcho::Echo& echo, int id);


	///This function chooses the type of level 1 building to be upgraded randomly
	///(but weighted in favor of certain building depending on the phase)
	int choose_building_upgrade_type_level1(AIEcho::Echo& echo);
	///This function chooses the type of level 2 building to be upgraded randomly
	///(but weighted in favor of certain building depending on the phase)
	int choose_building_upgrade_type_level2(AIEcho::Echo& echo);
	///This function is a generic version of the above functions
	int choose_building_upgrade_type(AIEcho::Echo& echo, int level, int inn_ratio, int hospital_ratio, int racetrack_ratio, int swimmingpool_ratio, int barracks_ratio, int school_ratio, int tower_ratio);
	///This function chooses a building of the given type and level to be upgraded.
	int choose_building_for_upgrade(AIEcho::Echo& echo, int type, int level);
	///This function starts upgrading buildings if the upgrading_phase is active.
	void upgrade_buildings(AIEcho::Echo& echo);


	///This function chooses an enemy building to be destroyed. It returns -1 if there are no accessible buildings
	int choose_building_to_attack(AIEcho::Echo& echo);
	///This function starts an attack on another enemy building
	void attack_building(AIEcho::Echo& echo);
	///This function controls the attacking of enemies, such as how many flags are active at one time
	void control_attacks(AIEcho::Echo& echo);
	///This function chooses the enemy team to target
	void choose_enemy_target(AIEcho::Echo& echo);
	///This function digs out an enemy building that is surrounded by ressources.
	///It will also cause Nicowar to dig itself out in certain situtation
	///Returns true if there are buildings that it can dig out, false otherwise
	bool dig_out_enemy(AIEcho::Echo& echo);
	///This will disable all current enemy attack flags
	void disable_attacks(AIEcho::Echo& echo);
	///This will re-enable all current enemy attack flags
	void enable_attacks(AIEcho::Echo& echo);

	///This function calculates the positions of defense flags
	void compute_defense_flag_positioning(AIEcho::Echo& echo);
	///This function adds the specific value to the counts arround the given pos, used in computeDefenseFlagPositioning
	void modify_points(Uint8* counts, int w, int h, int x, int y, int dist, int value, std::list<int>& locations);

	///This integer stores the currently targetted enemy
	int target;
	///This integer stores whether a building is being dug out at the current moment
	bool is_digging_out;
	///This vector stores the ID's for all current war flags
	std::vector<int> attack_flags;
	///This vector stores the ID's for all current defense flags
	std::vector<int> defense_flags;

	///This function updates the restricted areas for farming
	void update_farming(AIEcho::Echo& echo);

	///This function puts exploration flags on fruit trees once the fruit phase
	///has been activated.
	void update_fruit_flags(AIEcho::Echo& echo);
	///This function updates the alliances with opponents once the fruit phase
	///has been activated.
	void update_fruit_alliances(AIEcho::Echo& echo);
	///This variable tells whether their are exploration flags on the fruit trees yet or not
	bool exploration_on_fruit;


	int timer;
};


#endif
