/*
  Copyright (C) 2006 Bradley Arsenault

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

#ifndef AINicowar_h
#define AINicowar_h

#include "AIEcho.h"

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
	std::queue<BuildingPlacement> placement_queue;
	///This is the queue for buildings that are going to be constructed
	std::queue<BuildingPlacement> construction_queue;


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
	void dig_out_enemy(AIEcho::Echo& echo);

	///This integer stores the number of flags that are active at one time
	int attack_flags;
	///This integer stores the currently targetted enemy
	int target;
	///This integer stores whether a building is being dug out at the current moment
	bool is_digging_out;

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
