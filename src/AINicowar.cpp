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

#include "AINicowar.h"
#include "GlobalContainer.h"
#include "FormatableString.h"
#include "boost/lexical_cast.hpp"
#include "Utilities.h"

using namespace AIEcho;
using namespace AIEcho::Gradients;
using namespace AIEcho::Construction;
using namespace AIEcho::Management;
using namespace AIEcho::Conditions;
using namespace AIEcho::SearchTools;
using namespace boost::logic;

NewNicowar::NewNicowar()
{
	timer=0;
	buildings_under_construction=0;
	growth_phase=false;
	skilled_work_phase=0;
	upgrading_phase_1=false;
	upgrading_phase_2=false;
	war_preperation=false;
	war=false;
	for(int n=0; n<PlacementSize; ++n)
		buildings_under_construction_per_type[n]=0;
	attack_flags=0;
}


bool NewNicowar::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("NewNicowar");
	timer=stream->readUint32("timer");
	stream->readLeaveSection();
	return true;
}


void NewNicowar::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("NewNicowar");
	stream->writeUint32(timer, "timer");
	stream->writeLeaveSection();
}


void NewNicowar::tick(Echo& echo)
{
	timer++;
	if(timer==1)
	{
		check_phases(echo);
		initialize(echo);
	}
	if(timer%50 == 0)
	{
		queue_buildings(echo);
	}
	if(timer%100 == 0)
	{
		check_phases(echo);
	}
	if(timer%100 == 33)
	{
		manage_buildings(echo);
	}
	if(timer%100 == 66)
	{
		upgrade_buildings(echo);
		control_attacks(echo);
	}
	order_buildings(echo);
}


void NewNicowar::handle_message(Echo& echo, const std::string& message)
{
	if(message.substr(0,19) == "building completed ")
	{
		int placement_num=boost::lexical_cast<int>(message.substr(19, message.size()-1));
		buildings_under_construction-=1;
		buildings_under_construction_per_type[placement_num]-=1;
	}
	if(message.substr(0,13) == "update swarm ")
	{
		int id=boost::lexical_cast<int>(message.substr(13, message.size()-1));
		manage_swarm(echo, id);
	}
	if(message.substr(0,11) == "update inn ")
	{
		int id=boost::lexical_cast<int>(message.substr(11, message.size()-1));
		manage_inn(echo, id);
	}
	if(message == "attack finished")
	{
		attack_flags-=1;
	}
}


void NewNicowar::initialize(Echo& echo)
{
	manage_buildings(echo);
}


void NewNicowar::check_phases(Echo& echo)
{
	TeamStat* stat=echo.player->team->stats.getLatestStat();

	///Qualifications for the growth phase:
	///1) Less than 60 units
	if(stat->totalUnit<60)
	{
		growth_phase=true;
	}
	else
	{
		growth_phase=false;
	}

	///Qualifications for the skilled work phase:
	///1) More than 20 units
	if(stat->totalUnit>=20)
	{
		skilled_work_phase=true;
	}
	else
	{
		skilled_work_phase=false;
	}

	///Qualifications for the upgrading phase 1:
	///1) Atleast 1 school
	///2) Atleast 30 units
	BuildingSearch schools(echo);
	schools.add_condition(new SpecificBuildingType(IntBuildingType::SCIENCE_BUILDING));
	schools.add_condition(new NotUnderConstruction);
	const int school_counts=schools.count_buildings();

	if(stat->totalUnit>=30 && school_counts>0)
	{
		upgrading_phase_1=true;
	}
	else
	{
		upgrading_phase_1=false;
	}

	///Qualifications for the upgrading phase 2:
	///1) Atleast 1 level 2 or level 3 school
	///2) Atleast 50 units
	BuildingSearch schools_2(echo);
	schools_2.add_condition(new SpecificBuildingType(IntBuildingType::SCIENCE_BUILDING));
	schools_2.add_condition(new NotUnderConstruction);
	schools_2.add_condition(new BuildingLevel(2));
	BuildingSearch schools_3(echo);
	schools_3.add_condition(new SpecificBuildingType(IntBuildingType::SCIENCE_BUILDING));
	schools_3.add_condition(new NotUnderConstruction);
	schools_3.add_condition(new BuildingLevel(3));
	const int school_counts_2=schools_2.count_buildings() + schools_3.count_buildings();

	if(stat->totalUnit>=50 && school_counts_2>0)
	{
		upgrading_phase_2=true;
	}
	else
	{
		upgrading_phase_2=false;
	}

	///Qualifications for the war preperation phase:
	///1) Atleast 50 units
	///2) Less than 2 barracks OR
	///3) Less than 50 warriors
	BuildingSearch barracks(echo);
	barracks.add_condition(new SpecificBuildingType(IntBuildingType::ATTACK_BUILDING));
	barracks.add_condition(new NotUnderConstruction);
	int barracks_count=barracks.count_buildings();

	if(stat->totalUnit>=50 && (stat->numberUnitPerType[WARRIOR] < 50 || barracks_count<2))
	{
		war_preperation=true;
	}
	else
	{
		war_preperation=false;
	}

	///Qualifications for the war phase:
	///Atleast 50 warriors and 2 barracks
	if(stat->numberUnitPerType[WARRIOR] >= 50 && barracks_count>=2)
	{
		war=true;
	}
	else
	{
		war=false;
	}
}


void NewNicowar::queue_buildings(Echo& echo)
{
	queue_inns(echo);
	queue_swarms(echo);
	queue_racetracks(echo);
	queue_swimmingpools(echo);
	queue_schools(echo);
	queue_barracks(echo);
	queue_hospitals(echo);
}


void NewNicowar::queue_inns(Echo& echo)
{
	//Get some statistics
	TeamStat* stat=echo.player->team->stats.getLatestStat();
	int total_workers=stat->numberUnitPerType[WORKER];
	int total_explorers=stat->numberUnitPerType[EXPLORER];
	int total_warriors=stat->numberUnitPerType[WARRIOR];

	//Count the number of inns there are at each level
	BuildingSearch bs_level1(echo);
	bs_level1.add_condition(new SpecificBuildingType(IntBuildingType::FOOD_BUILDING));
	bs_level1.add_condition(new BuildingLevel(1));
	bs_level1.add_condition(new NotUnderConstruction);
	const int number1=bs_level1.count_buildings() + buildings_under_construction_per_type[RegularInn];

	BuildingSearch bs_level2(echo);
	bs_level2.add_condition(new SpecificBuildingType(IntBuildingType::FOOD_BUILDING));
	bs_level2.add_condition(new BuildingLevel(2));
	const int number2=bs_level2.count_buildings();

	BuildingSearch bs_level3(echo);
	bs_level3.add_condition(new SpecificBuildingType(IntBuildingType::FOOD_BUILDING));
	bs_level3.add_condition(new BuildingLevel(3));
	const int number3=bs_level3.count_buildings();

	///A level 1 Inn can handle 8 units, a level 2 can handle 12 and a level 3 can handle 16
	if((total_workers+total_explorers+total_warriors)>=(number1*8 + number2*12 + number3*16))
	{
		placement_queue.push(RegularInn);
	}
}


void NewNicowar::queue_swarms(Echo& echo)
{
	BuildingSearch bs(echo);
	bs.add_condition(new SpecificBuildingType(IntBuildingType::SWARM_BUILDING));
	bs.add_condition(new NotUnderConstruction);
	const int swarm_count = bs.count_buildings() + buildings_under_construction_per_type[RegularSwarm];
	const int total_unit = echo.player->team->stats.getLatestStat()->totalUnit;
	int demand=0;
	if(growth_phase)
	{
		demand = 1 + (std::min(30, total_unit)/10);
	}
	else
	{
		demand = (total_unit/50);
	}

	if(demand > swarm_count)
	{
		placement_queue.push(RegularSwarm);
	}
}


void NewNicowar::queue_racetracks(Echo& echo)
{
	BuildingSearch bs_finished(echo);
	bs_finished.add_condition(new SpecificBuildingType(IntBuildingType::WALKSPEED_BUILDING));
	bs_finished.add_condition(new NotUnderConstruction);

	BuildingSearch bs_upgrading(echo);
	bs_upgrading.add_condition(new SpecificBuildingType(IntBuildingType::WALKSPEED_BUILDING));
	bs_upgrading.add_condition(new BeingUpgraded);

	const int racetrack_count=bs_finished.count_buildings() + bs_upgrading.count_buildings() + buildings_under_construction_per_type[RegularRacetrack];
	const int total_unit = echo.player->team->stats.getLatestStat()->totalUnit;
	int demand=0;
	if(skilled_work_phase)
	{
		demand=1;
	}

	if(demand > racetrack_count)
	{
		placement_queue.push(RegularRacetrack);
	}
}


void NewNicowar::queue_swimmingpools(Echo& echo)
{
	BuildingSearch bs_finished(echo);
	bs_finished.add_condition(new SpecificBuildingType(IntBuildingType::SWIMSPEED_BUILDING));
	bs_finished.add_condition(new NotUnderConstruction);

	BuildingSearch bs_upgrading(echo);
	bs_upgrading.add_condition(new SpecificBuildingType(IntBuildingType::SWIMSPEED_BUILDING));
	bs_upgrading.add_condition(new BeingUpgraded);

	const int swimmingpool_count=bs_finished.count_buildings() + bs_upgrading.count_buildings() + buildings_under_construction_per_type[RegularSwimmingpool];
	const int total_unit = echo.player->team->stats.getLatestStat()->totalUnit;
	int demand=0;
	if(skilled_work_phase)
	{
		demand=1;
	}

	if(demand > swimmingpool_count)
	{
		placement_queue.push(RegularSwimmingpool);
	}
}


void NewNicowar::queue_schools(Echo& echo)
{
	BuildingSearch bs_finished(echo);
	bs_finished.add_condition(new SpecificBuildingType(IntBuildingType::SCIENCE_BUILDING));
	bs_finished.add_condition(new NotUnderConstruction);

	BuildingSearch bs_upgrading(echo);
	bs_upgrading.add_condition(new SpecificBuildingType(IntBuildingType::SCIENCE_BUILDING));
	bs_upgrading.add_condition(new BeingUpgraded);

	const int school_count=bs_finished.count_buildings() + bs_upgrading.count_buildings() + buildings_under_construction_per_type[RegularSchool];
	const int total_unit = echo.player->team->stats.getLatestStat()->totalUnit;
	int demand=0;
	if(skilled_work_phase)
	{
		demand=2;
	}

	if(demand > school_count)
	{
		placement_queue.push(RegularSchool);
	}
}


void NewNicowar::queue_barracks(Echo& echo)
{
	BuildingSearch bs_finished(echo);
	bs_finished.add_condition(new SpecificBuildingType(IntBuildingType::ATTACK_BUILDING));
	bs_finished.add_condition(new NotUnderConstruction);

	BuildingSearch bs_upgrading(echo);
	bs_upgrading.add_condition(new SpecificBuildingType(IntBuildingType::ATTACK_BUILDING));
	bs_upgrading.add_condition(new BeingUpgraded);

	const int barracks_count=bs_finished.count_buildings() + bs_upgrading.count_buildings() + buildings_under_construction_per_type[RegularBarracks];

	int demand=0;
	if(war_preperation)
	{
		demand=2;
	}

	if(demand > barracks_count)
	{
		placement_queue.push(RegularBarracks);
	}
}


void NewNicowar::queue_hospitals(Echo& echo)
{
	BuildingSearch bs_finished(echo);
	bs_finished.add_condition(new SpecificBuildingType(IntBuildingType::HEAL_BUILDING));
	bs_finished.add_condition(new NotUnderConstruction);

	BuildingSearch bs_upgrading(echo);
	bs_upgrading.add_condition(new SpecificBuildingType(IntBuildingType::HEAL_BUILDING));
	bs_upgrading.add_condition(new BeingUpgraded);

	const int hospital_count=bs_finished.count_buildings() + bs_upgrading.count_buildings() + buildings_under_construction_per_type[RegularHospital];
	const int total_warrior = echo.player->team->stats.getLatestStat()->numberUnitPerType[WARRIOR];

	int demand=1;
	if(war_preperation)
	{
		demand+=total_warrior/20;
	}

	if(demand > hospital_count)
	{
		placement_queue.push(RegularHospital);
	}
}



void NewNicowar::order_buildings(Echo& echo)
{
	while(!placement_queue.empty())
	{
		BuildingPlacement b=placement_queue.front();
		placement_queue.pop();
		construction_queue.push(b);
		buildings_under_construction_per_type[int(b)]+=1;
	}

	while(!construction_queue.empty() && buildings_under_construction < 2)
	{
		int id=-1;
		BuildingPlacement b=construction_queue.front();
		construction_queue.pop();
		if(b==RegularInn)
		{
			id=order_regular_inn(echo);
		}
		if(b==RegularSwarm)
		{
			id=order_regular_swarm(echo);
		}
		if(b==RegularRacetrack)
		{
			id=order_regular_racetrack(echo);
		}
		if(b==RegularSwimmingpool)
		{
			id=order_regular_swimmingpool(echo);
		}
		if(b==RegularSchool)
		{
			id=order_regular_school(echo);
		}
		if(b==RegularBarracks)
		{
			id=order_regular_barracks(echo);
		}
		if(b==RegularHospital)
		{
			id=order_regular_hospital(echo);
		}

		///This code keeps track of the number of buildings that are under construction at any one point
		buildings_under_construction+=1;
		ManagementOrder* mo_completion_message=new SendMessage("building completed "+boost::lexical_cast<std::string>(int(b)));
		mo_completion_message->add_condition(new EitherCondition(
		                             new ParticularBuilding(new NotUnderConstruction, id),
		                             new BuildingDestroyed(id)));
		echo.add_management_order(mo_completion_message);
	}
}


int NewNicowar::order_regular_inn(Echo& echo)
{
	//The main order for the inn
	BuildingOrder* bo = new BuildingOrder(IntBuildingType::FOOD_BUILDING, 2);

	//Constraints arround the location of wheat
	AIEcho::Gradients::GradientInfo gi_wheat;
	gi_wheat.add_source(new AIEcho::Gradients::Entities::Ressource(CORN));
	//You want to be close to wheat
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_wheat, 4));
	//You can't be farther than 10 units from wheat
	bo->add_constraint(new AIEcho::Construction::MaximumDistance(gi_wheat, 10));

	//Constraints arround nearby settlement
	AIEcho::Gradients::GradientInfo gi_building;
	gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
	gi_building.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	//You want to be close to other buildings, but wheat is more important
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 2));

	AIEcho::Gradients::GradientInfo gi_building_construction;
	gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
	gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	//You don't want to be too close
	bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_building_construction, 3));

	if(echo.is_fruit_on_map())
	{
		//Constraints arround the location of fruit
		AIEcho::Gradients::GradientInfo gi_fruit;
		gi_fruit.add_source(new AIEcho::Gradients::Entities::Ressource(CHERRY));
		gi_fruit.add_source(new AIEcho::Gradients::Entities::Ressource(ORANGE));
		gi_fruit.add_source(new AIEcho::Gradients::Entities::Ressource(PRUNE));
		//You want to be reasnobly close to fruit, closer if possible
		bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_fruit, 1));
	}

	//Add the building order to the list of orders
	unsigned int id=echo.add_building_order(bo);

	//Change the number of workers assigned when the building is finished
	ManagementOrder* mo_completion=new SendMessage(FormatableString("update inn %0").arg(id));
	mo_completion->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
	echo.add_management_order(mo_completion);

	return id;
}


int NewNicowar::order_regular_swarm(Echo& echo)
{
	//The main order for the swarm
	BuildingOrder* bo = new BuildingOrder(IntBuildingType::SWARM_BUILDING, 4);

	//Constraints arround the location of wheat
	AIEcho::Gradients::GradientInfo gi_wheat;
	gi_wheat.add_source(new AIEcho::Gradients::Entities::Ressource(CORN));
	//You want to be close to wheat
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_wheat, 4));

	//Constraints arround nearby settlement
	AIEcho::Gradients::GradientInfo gi_building;
	gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
	gi_building.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	//You want to be close to other buildings, but wheat is more important
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 1));

	AIEcho::Gradients::GradientInfo gi_building_construction;
	gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
	gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	//You don't want to be too close
	bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_building_construction, 3));

	//Add the building order to the list of orders
	unsigned int id=echo.add_building_order(bo);

	//Change the number of workers assigned when the building is finished
	ManagementOrder* mo_completion=new SendMessage(FormatableString("update swarm %0").arg(id));
	mo_completion->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
	echo.add_management_order(mo_completion);

	return id;
}


int NewNicowar::order_regular_racetrack(Echo& echo)
{
	//The main order for the racetrack
	BuildingOrder* bo = new BuildingOrder(IntBuildingType::WALKSPEED_BUILDING, 6);

	//Constraints arround the location of wood
	AIEcho::Gradients::GradientInfo gi_wood;
	gi_wood.add_source(new AIEcho::Gradients::Entities::Ressource(WOOD));
	//You want to be close to wood
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_wood, 4));

	//Constraints arround the location of stone
	AIEcho::Gradients::GradientInfo gi_stone;
	gi_stone.add_source(new AIEcho::Gradients::Entities::Ressource(STONE));
	//You want to be close to stone
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_stone, 1));
	//But not to close, so you have room to upgrade
	bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_stone, 2));

	//Constraints arround nearby settlement
	AIEcho::Gradients::GradientInfo gi_building;
	gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
	gi_building.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	//You want to be close to other buildings, but wheat is more important
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 2));

	AIEcho::Gradients::GradientInfo gi_building_construction;
	gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
	gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	//You don't want to be too close
	bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_building_construction, 4));

	//Add the building order to the list of orders
	int id = echo.add_building_order(bo);

	return id;
}


int NewNicowar::order_regular_swimmingpool(Echo& echo)
{
	//The main order for the swimmingpool
	BuildingOrder* bo = new BuildingOrder(IntBuildingType::SWIMSPEED_BUILDING, 6);

	//Constraints arround the location of wood
	AIEcho::Gradients::GradientInfo gi_wood;
	gi_wood.add_source(new AIEcho::Gradients::Entities::Ressource(WOOD));
	//You want to be close to wood
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_wood, 4));

	//Constraints arround the location of wheat
	AIEcho::Gradients::GradientInfo gi_wheat;
	gi_wheat.add_source(new AIEcho::Gradients::Entities::Ressource(CORN));
	//You want to be close to wheat
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_wheat, 1));

	//Constraints arround the location of stone
	AIEcho::Gradients::GradientInfo gi_stone;
	gi_stone.add_source(new AIEcho::Gradients::Entities::Ressource(STONE));
	//You don't want to be too close, so you have room to upgrade
	bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_stone, 2));

	//Constraints arround nearby settlement
	AIEcho::Gradients::GradientInfo gi_building;
	gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
	gi_building.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	//You want to be close to other buildings, but wheat is more important
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 2));

	AIEcho::Gradients::GradientInfo gi_building_construction;
	gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
	gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	//You don't want to be too close
	bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_building_construction, 4));

	//Add the building order to the list of orders
	int id = echo.add_building_order(bo);

	return id;
}


int NewNicowar::order_regular_school(Echo& echo)
{
	//The main order for the school
	BuildingOrder* bo = new BuildingOrder(IntBuildingType::SCIENCE_BUILDING, 5);

	//Constraints arround nearby settlement
	AIEcho::Gradients::GradientInfo gi_building;
	gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
	gi_building.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	//You want to be close to other buildings, but wheat is more important
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 2));

	AIEcho::Gradients::GradientInfo gi_building_construction;
	gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
	gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	//You don't want to be too close
	bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_building_construction, 4));

	//Constraints arround the enemy
	AIEcho::Gradients::GradientInfo gi_enemy;
	for(enemy_team_iterator i(echo); i!=enemy_team_iterator(); ++i)
	{
		gi_enemy.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(*i, false));
	}
	gi_enemy.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	bo->add_constraint(new AIEcho::Construction::MaximizedDistance(gi_enemy, 3));

	//Add the building order to the list of orders
	int id = echo.add_building_order(bo);

	return id;
}


int NewNicowar::order_regular_barracks(Echo& echo)
{
	//The main order for the barracks
	BuildingOrder* bo = new BuildingOrder(IntBuildingType::ATTACK_BUILDING, 6);

	//Constraints arround the location of stone
	AIEcho::Gradients::GradientInfo gi_stone;
	gi_stone.add_source(new AIEcho::Gradients::Entities::Ressource(STONE));
	//You want to be close to stone
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_stone, 5));

	//Constraints arround the location of wood
	AIEcho::Gradients::GradientInfo gi_wood;
	gi_wood.add_source(new AIEcho::Gradients::Entities::Ressource(WOOD));
	//You want to be close to wood
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_wood, 2));

	//Constraints arround nearby settlement
	AIEcho::Gradients::GradientInfo gi_building;
	gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
	gi_building.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	//You want to be close to other buildings
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 1));

	AIEcho::Gradients::GradientInfo gi_building_construction;
	gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
	gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	//You don't want to be too close
	bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_building_construction, 2));

	//Add the building order to the list of orders
	int id = echo.add_building_order(bo);

	return id;
}


int NewNicowar::order_regular_hospital(Echo& echo)
{
	//The main order for the hospital
	BuildingOrder* bo = new BuildingOrder(IntBuildingType::HEAL_BUILDING, 2);

	//Constraints arround the location of wood
	AIEcho::Gradients::GradientInfo gi_wood;
	gi_wood.add_source(new AIEcho::Gradients::Entities::Ressource(WOOD));
	//You want to be close to wood
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_wood, 2));

	//Constraints arround nearby settlement
	AIEcho::Gradients::GradientInfo gi_building;
	gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
	gi_building.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	//You want to be close to other buildings
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 3));

	AIEcho::Gradients::GradientInfo gi_building_construction;
	gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
	gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	//You don't want to be too close
	bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_building_construction, 2));

	//Add the building order to the list of orders
	int id = echo.add_building_order(bo);

	return id;

}


void NewNicowar::manage_buildings(Echo& echo)
{
	BuildingSearch bs(echo);
	bs.add_condition(new NotUnderConstruction);
	for(building_search_iterator i = bs.begin(); i!=bs.end(); ++i)
	{	
		if(echo.get_building_register().get_type(*i)==IntBuildingType::SWARM_BUILDING)
		{
			manage_swarm(echo, *i);
		}
		if(echo.get_building_register().get_type(*i)==IntBuildingType::FOOD_BUILDING)
		{
			manage_inn(echo, *i);
		}
	}
}


void NewNicowar::manage_inn(Echo& echo, int id)
{
	int level=echo.get_building_register().get_level(id);
	int assigned=echo.get_building_register().get_assigned(id);

	///The number of units assigned to an Inn depends entirely on its level
	if(level==1 && assigned!=1)
	{
		ManagementOrder* mo_assign=new AssignWorkers(1, id);
		echo.add_management_order(mo_assign);
	}
	if(level==2 && assigned!=3)
	{
		ManagementOrder* mo_assign=new AssignWorkers(3, id);
		echo.add_management_order(mo_assign);
	}
	if(level==3 && assigned!=5)
	{
		ManagementOrder* mo_assign=new AssignWorkers(5, id);
		echo.add_management_order(mo_assign);
	}
}


void NewNicowar::manage_swarm(Echo& echo, int id)
{
	//Get some statistics
	TeamStat* stat=echo.player->team->stats.getLatestStat();
	int total_explorers=stat->numberUnitPerType[EXPLORER];

	int assigned=echo.get_building_register().get_assigned(id);
	int to_assign=0;

	int worker_ratio=0;
	int explorer_ratio=0;
	int warrior_ratio=0;

	///Assign double workers to swarms during the growth phase and the war_preperation phase
	if(growth_phase || war_preperation)
	{
		to_assign=6;
	}
	else
	{
		to_assign=3;
	}

	///During the growth phase, there is an 4-1 ratio between workers and explorers until
	///there are atleast 3 explorers
	if(growth_phase)
	{
		worker_ratio=4;
		if(total_explorers<3)
			explorer_ratio=1;
		else
			explorer_ratio=0;
	}
	else
	{
		worker_ratio=1;
		explorer_ratio=0;
	}

	///Warriors are constructed during the war preperation phase
	if(war_preperation)
	{
		warrior_ratio=2;
	}
	else
	{
		warrior_ratio=0;
	}

	if(assigned != to_assign)
	{
		ManagementOrder* mo_assign=new AssignWorkers(to_assign, id);
		echo.add_management_order(mo_assign);
	}

	//Change the ratio of the swarm when its finished
	ManagementOrder* mo_ratios=new ChangeSwarm(worker_ratio, explorer_ratio, warrior_ratio, id);
	echo.add_management_order(mo_ratios);
}



int NewNicowar::choose_building_upgrade_type_level1(Echo& echo)
{
	BuildingSearch schools(echo);
	schools.add_condition(new SpecificBuildingType(IntBuildingType::SCIENCE_BUILDING));
	schools.add_condition(new BeingUpgradedTo(2));
	const int school_counts=schools.count_buildings();

	///Schools are only upgraded one at a time
	if(school_counts>0)
		return choose_building_upgrade_type(echo, 1, 10, 5, 8, 8, 15, 0, 0);
	else
		return choose_building_upgrade_type(echo, 1, 10, 5, 8, 8, 15, 5, 0);
}



int NewNicowar::choose_building_upgrade_type_level2(Echo& echo)
{
	BuildingSearch schools_upgrading(echo);
	schools_upgrading.add_condition(new SpecificBuildingType(IntBuildingType::SCIENCE_BUILDING));
	schools_upgrading.add_condition(new BeingUpgradedTo(3));
	const int school_counts_upgrading=schools_upgrading.count_buildings();

	BuildingSearch schools_lvl2(echo);
	schools_lvl2.add_condition(new SpecificBuildingType(IntBuildingType::SCIENCE_BUILDING));
	schools_lvl2.add_condition(new BuildingLevel(2));
	schools_lvl2.add_condition(new NotUnderConstruction);
	const int school_counts_level2=schools_lvl2.count_buildings();

	BuildingSearch schools_lvl3(echo);
	schools_lvl3.add_condition(new SpecificBuildingType(IntBuildingType::SCIENCE_BUILDING));
	schools_lvl3.add_condition(new BuildingLevel(3));
	schools_lvl3.add_condition(new NotUnderConstruction);
	const int school_counts_level3=schools_lvl3.count_buildings();


	if(school_counts_upgrading>0 || (school_counts_level2 + school_counts_level3)<2)
		return choose_building_upgrade_type(echo, 2, 6, 5, 8, 8, 20, 0, 0);
	else
		return choose_building_upgrade_type(echo, 2, 6, 5, 8, 8, 20, 5, 0);
}


int NewNicowar::choose_building_upgrade_type(Echo& echo, int level, int inn_ratio, int hospital_ratio, int racetrack_ratio, int swimmingpool_ratio, int barracks_ratio, int school_ratio, int tower_ratio)
{
	///First count the types of buildings that are available to us for upgrading
	///you wouldn't want to choose a Barracks to be upgraded if there are none
	int building_count[IntBuildingType::NB_BUILDING];
	std::fill(building_count, building_count+IntBuildingType::NB_BUILDING, 0);

	BuildingSearch bs(echo);
	bs.add_condition(new NotUnderConstruction);
	bs.add_condition(new BuildingLevel(level));
	bs.add_condition(new Upgradable);
	for(building_search_iterator i = bs.begin(); i!=bs.end(); ++i)
	{	
		building_count[echo.get_building_register().get_type(*i)]+=1;
	}

	///Next, add in the n slices for each of the buildings with respect to their ratio
	std::vector<int> buildings;
	if(building_count[IntBuildingType::FOOD_BUILDING] > 0)
	{
		for(int n=0; n<inn_ratio; ++n)
			buildings.push_back(IntBuildingType::FOOD_BUILDING);
	}
	if(building_count[IntBuildingType::HEAL_BUILDING] > 0)
	{
		for(int n=0; n<hospital_ratio; ++n)
			buildings.push_back(IntBuildingType::HEAL_BUILDING);
	}
	if(building_count[IntBuildingType::WALKSPEED_BUILDING] > 0)
	{
		for(int n=0; n<racetrack_ratio; ++n)
			buildings.push_back(IntBuildingType::WALKSPEED_BUILDING);
	}
	if(building_count[IntBuildingType::SWIMSPEED_BUILDING] > 0)
	{
		const int SwimmingpoolWeight=8;
		for(int n=0; n<SwimmingpoolWeight; ++n)
			buildings.push_back(IntBuildingType::SWIMSPEED_BUILDING);
	}
	if(building_count[IntBuildingType::ATTACK_BUILDING] > 0)
	{
		for(int n=0; n<barracks_ratio; ++n)
			buildings.push_back(IntBuildingType::ATTACK_BUILDING);
	}
	if(building_count[IntBuildingType::SCIENCE_BUILDING] > 0)
	{
		for(int n=0; n<school_ratio; ++n)
			buildings.push_back(IntBuildingType::SCIENCE_BUILDING);
	}
	if(building_count[IntBuildingType::DEFENSE_BUILDING] > 0)
	{
		for(int n=0; n<tower_ratio; ++n)
			buildings.push_back(IntBuildingType::DEFENSE_BUILDING);
	}

	//Now choose a building, or return -1 for none available
	int random = syncRand() % buildings.size();
	if(buildings.size()==0)
		return -1;
	return buildings[random];
}


int NewNicowar::choose_building_for_upgrade(Echo& echo, int type, int level)
{
	BuildingSearch bs(echo);
	bs.add_condition(new SpecificBuildingType(type));
	bs.add_condition(new NotUnderConstruction);
	bs.add_condition(new BuildingLevel(level));
	bs.add_condition(new Upgradable);
	std::vector<int> buildings;
	std::copy(bs.begin(), bs.end(), std::back_insert_iterator<std::vector<int> >(buildings));
	int random=syncRand() % buildings.size();
	int id=buildings[random];

	return id;
}


void NewNicowar::upgrade_buildings(Echo& echo)
{
	int num_to_upgrade_level1=0;
	int num_to_upgrade_level2=0;
	if(upgrading_phase_1)
	{
		num_to_upgrade_level1=2;
	}
	else
	{
		num_to_upgrade_level1=0;
	}

	if(upgrading_phase_2)
	{
		num_to_upgrade_level2=2;
	}
	else
	{
		num_to_upgrade_level2=0;
	}

	BuildingSearch bs_lvl1(echo);
	bs_lvl1.add_condition(new BeingUpgradedTo(2));
	int num_upgrading_level1=bs_lvl1.count_buildings();

	BuildingSearch bs_lvl2(echo);
	bs_lvl2.add_condition(new BeingUpgradedTo(3));
	int num_upgrading_level2=bs_lvl2.count_buildings();

	///Level one upgrades
	if(num_upgrading_level1 < num_to_upgrade_level1)
	{
		int building_type=choose_building_upgrade_type_level1(echo);
		if(building_type!=-1)
		{
			std::string type=IntBuildingType::typeFromShortNumber(building_type);

			int id=choose_building_for_upgrade(echo, building_type, 1);	

			ManagementOrder* uro = new UpgradeRepair(id);
			echo.add_management_order(uro);
			
			ManagementOrder* mo_assign=new AssignWorkers(8, id);
			mo_assign->add_condition(new ParticularBuilding(new UnderConstruction, id));
			echo.add_management_order(mo_assign);

			//Cause the building to be updated after its completion. Not all buildings need
			//to be updated, in which case the order will simply be ignored
			ManagementOrder* mo_completion=new SendMessage(FormatableString("update %0 %1").arg(type).arg(id));
			mo_completion->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
			echo.add_management_order(mo_completion);
		}
	}

	///Level two upgrades
	if(num_upgrading_level2 < num_to_upgrade_level2)
	{
		int building_type=choose_building_upgrade_type_level2(echo);
		if(building_type!=-1)
		{
			std::string type=IntBuildingType::typeFromShortNumber(building_type);

			int id=choose_building_for_upgrade(echo, building_type, 2);			
			ManagementOrder* uro = new UpgradeRepair(id);
			echo.add_management_order(uro);
			
			ManagementOrder* mo_assign=new AssignWorkers(6, id);
			mo_assign->add_condition(new ParticularBuilding(new UnderConstruction, id));
			echo.add_management_order(mo_assign);

			//Cause the building to be updated after its completion. Not all buildings need
			//to be updated, in which case the order will simply be ignored
			ManagementOrder* mo_completion=new SendMessage(FormatableString("update %0 %1").arg(type).arg(id));
			mo_completion->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
			echo.add_management_order(mo_completion);
		}
	}
}


int NewNicowar::choose_building_to_attack(Echo& echo)
{
	std::vector<int> buildings_to_attack;
	buildings_to_attack.reserve(100);
	for(enemy_team_iterator i(echo); i!=enemy_team_iterator(); ++i)
	{
		for(enemy_building_iterator ebi(echo, *i, -1, -1, indeterminate); ebi!=enemy_building_iterator(); ++ebi)
		{
			buildings_to_attack.push_back(*ebi);
		}
	}
	int num=syncRand() % buildings_to_attack.size();
	return buildings_to_attack[num];
}


void NewNicowar::attack_building(Echo& echo)
{
	int building=choose_building_to_attack(echo);
	BuildingOrder* bo = new BuildingOrder(IntBuildingType::WAR_FLAG, 15);
	bo->add_constraint(new CenterOfBuilding(building));
	unsigned int id=echo.add_building_order(bo);

	ManagementOrder* mo_destroyed_1=new DestroyBuilding(id);
	mo_destroyed_1->add_condition(new EnemyBuildingDestroyed(echo, building));
	echo.add_management_order(mo_destroyed_1);

	ManagementOrder* mo_destroyed_2=new SendMessage("attack finished");
	mo_destroyed_2->add_condition(new EnemyBuildingDestroyed(echo, building));
	echo.add_management_order(mo_destroyed_2);

	attack_flags+=1;
}


void NewNicowar::control_attacks(Echo& echo)
{
	int number_attacks=0;
	if(war)
	{
		number_attacks=2;
	}


	if(attack_flags < number_attacks)
	{
		attack_building(echo);
	}
}

