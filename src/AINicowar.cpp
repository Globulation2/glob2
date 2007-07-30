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
#include "Game.h"
#include "Unit.h"

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
	fruit_phase=false;
	starving_recovery=false;
	no_workers_phase=false;
	starving_recovery_inns = 0;
	exploration_on_fruit=false;
	for(int n=0; n<PlacementSize; ++n)
		buildings_under_construction_per_type[n]=0;
	target=-1;
	attack_flags.clear();
	is_digging_out=false;
}


bool NewNicowar::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("NewNicowar");
	timer=stream->readUint32("timer");
	if(versionMinor >= 59)
	{
		growth_phase=stream->readUint8("growth_phase");
		skilled_work_phase=stream->readUint8("skilled_work_phase");
		upgrading_phase_1=stream->readUint8("upgrading_phase_1");
		upgrading_phase_2=stream->readUint8("upgrading_phase_2");
		war_preperation=stream->readUint8("war_preperation");
		war=stream->readUint8("war");
		fruit_phase=stream->readUint8("fruit_phase");
		starving_recovery=stream->readUint8("starving_recovery");
		no_workers_phase=stream->readUint8("no_workers_phase");
		starving_recovery_inns=stream->readUint8("starving_recovery_inns");
		buildings_under_construction=stream->readUint32("buildings_under_construction");
		for(int n=0; n<PlacementSize; ++n)
		{
			buildings_under_construction_per_type[n]=stream->readUint8(FormatableString("buildings_under_construction_per_type[%0]").arg(n).c_str());
		}
			
		stream->readEnterSection("placement_queue");
		size_t size = stream->readUint16("size");
		for(size_t n = 0; n<size; ++n)
		{
			stream->readEnterSection(n);
			BuildingPlacement bp = static_cast<BuildingPlacement>(stream->readUint8("placement"));
			placement_queue.push(bp);
			stream->readLeaveSection();
		}
		stream->readLeaveSection();

		stream->readEnterSection("construction_queue");
		size = stream->readUint16("size");
		for(size_t n = 0; n<size; ++n)
		{
			stream->readEnterSection(n);
			BuildingPlacement bp = static_cast<BuildingPlacement>(stream->readUint8("placement"));
			construction_queue.push(bp);
			stream->readLeaveSection();
		}
		stream->readLeaveSection();

		target = stream->readSint8("target");
		is_digging_out = stream->readUint8("is_digging_out");

		stream->readEnterSection("attack_flags");
		size = stream->readUint16("size");
		for(size_t n = 0; n<size; ++n)
		{
			stream->readEnterSection(n);
			int flag = stream->readUint32("flag");
			attack_flags.push_back(flag);
			stream->readLeaveSection();
		}
		stream->readLeaveSection();

		exploration_on_fruit=stream->readUint8("exploration_on_fruit");
		stream->readLeaveSection();
	}
	return true;
}


void NewNicowar::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("NewNicowar");
	stream->writeUint32(timer, "timer");
	
	stream->writeUint8(growth_phase, "growth_phase");
	stream->writeUint8(skilled_work_phase, "skilled_work_phase");
	stream->writeUint8(upgrading_phase_1, "upgrading_phase_1");
	stream->writeUint8(upgrading_phase_2, "upgrading_phase_2");
	stream->writeUint8(war_preperation, "war_preperation");
	stream->writeUint8(war, "war");
	stream->writeUint8(fruit_phase, "fruit_phase");
	stream->writeUint8(starving_recovery, "starving_recovery");
	stream->writeUint8(no_workers_phase, "no_workers_phase");
	stream->writeUint8(starving_recovery_inns, "starving_recovery_inns");
	stream->writeUint32(buildings_under_construction, "buildings_under_construction");
	for(int n=0; n<PlacementSize; ++n)
	{
		stream->writeUint8(buildings_under_construction_per_type[n], FormatableString("buildings_under_construction_per_type[%0]").arg(n).c_str());
	}
		
	stream->writeEnterSection("placement_queue");
	stream->writeUint16(placement_queue.size(), "size");
	size_t n = 0;
	while(!placement_queue.empty())
	{
		stream->writeEnterSection(n);
		BuildingPlacement bp = placement_queue.front();
		placement_queue.pop();
		stream->writeUint8(static_cast<Uint8>(bp), "placement");
		stream->writeLeaveSection();
		n+=1;
	}
	stream->writeLeaveSection();

	stream->writeEnterSection("construction_queue");
	stream->writeUint16(construction_queue.size(), "size");
	n = 0;
	while(!construction_queue.empty())
	{
		stream->writeEnterSection(n);
		BuildingPlacement bp = construction_queue.front();
		construction_queue.pop();
		stream->writeUint8(static_cast<Uint8>(bp), "placement");
		stream->writeLeaveSection();
		n+=1;
	}
	stream->writeLeaveSection();

	stream->writeUint8(target, "target");
	stream->writeUint8(is_digging_out, "is_digging_out");

	stream->writeEnterSection("attack_flags");
	stream->writeUint16(attack_flags.size(), "size");
	for(n = 0; n<attack_flags.size(); ++n)
	{
		stream->writeEnterSection(n);
		stream->writeUint32(attack_flags[n], "flag");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	stream->writeUint8(exploration_on_fruit, "exploration_on_fruit");
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
	if(timer%100 == 0)
	{
		queue_buildings(echo);
	}
	if(timer%100 == 20)
	{
		check_phases(echo);
	}
	if(timer%100 == 40)
	{
		manage_buildings(echo);
	}
	if(timer%100 == 60)
	{
		upgrade_buildings(echo);
	}
	if(timer%100 == 80)
	{
		control_attacks(echo);
	}
	if(timer%250 == 0)
	{
		update_farming(echo);
	}
	if(timer%250 ==125)
	{
		update_fruit_flags(echo);
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
	if(message.substr(0,21) == "update clearing zone ")
	{
		MapInfo mi(echo);
		int id=boost::lexical_cast<int>(message.substr(21, message.size()-1));
		Building* b = echo.get_building_register().get_building(id);		
		AddArea* mo_clearing=new AddArea(ClearingArea);
		RemoveArea* mo_remove_clearing=new RemoveArea(ClearingArea);
		mo_remove_clearing->add_condition(new BuildingDestroyed(id));
		for(int nx=-1; nx<b->type->width+1; ++nx)
		{
			for(int ny=-1; ny<b->type->height+1; ++ny)
			{
				if(!mi.is_forbidden_area(b->posX+nx, b->posY+ny))
				{
					mo_clearing->add_location(b->posX+nx, b->posY+ny);
					mo_remove_clearing->add_location(b->posX+nx, b->posY+ny);
				}
			}
		}
		echo.add_management_order(mo_clearing);
		echo.add_management_order(mo_remove_clearing);
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
	if(message.substr(0,16)  == "attack finished ")
	{
		int id=boost::lexical_cast<int>(message.substr(16, message.size()-1));
		attack_flags.erase(std::find(attack_flags.begin(), attack_flags.end(), id));
	}
	if(message == "finished digging out")
	{
		is_digging_out=false;
	}
	if(message == "finished starving recovery inn")
	{
		starving_recovery_inns-=1;
	}
}


void NewNicowar::initialize(Echo& echo)
{
	BuildingSearch bs(echo);
	for(building_search_iterator i = bs.begin(); i!=bs.end(); ++i)
	{	
		if(echo.get_building_register().get_type(*i)==IntBuildingType::SWARM_BUILDING)
		{
			ManagementOrder* mo_tracker=new AddRessourceTracker(25, CORN, *i);
			mo_tracker->add_condition(new ParticularBuilding(new NotUnderConstruction, *i));
			echo.add_management_order(mo_tracker);
		}
		if(echo.get_building_register().get_type(*i)==IntBuildingType::FOOD_BUILDING)
		{
			ManagementOrder* mo_tracker=new AddRessourceTracker(25, CORN, *i);
			mo_tracker->add_condition(new ParticularBuilding(new NotUnderConstruction, *i));
			echo.add_management_order(mo_tracker);
		}
	}
	
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
	///3) Atleast 5 of them are trained for upgrading to level 2
	BuildingSearch schools(echo);
	schools.add_condition(new SpecificBuildingType(IntBuildingType::SCIENCE_BUILDING));
	schools.add_condition(new NotUnderConstruction);
	const int school_counts=schools.count_buildings();
	const int trained_count=echo.get_team_stats().upgradeState[BUILD][1];

	if(stat->totalUnit>=30 && school_counts>0 && trained_count>5)
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
	///3) Atleast 5 of them are trained for upgrading to level 3
	BuildingSearch schools_2(echo);
	schools_2.add_condition(new SpecificBuildingType(IntBuildingType::SCIENCE_BUILDING));
	schools_2.add_condition(new NotUnderConstruction);
	schools_2.add_condition(new BuildingLevel(2));
	BuildingSearch schools_3(echo);
	schools_3.add_condition(new SpecificBuildingType(IntBuildingType::SCIENCE_BUILDING));
	schools_3.add_condition(new NotUnderConstruction);
	schools_3.add_condition(new BuildingLevel(3));
	const int school_counts_2=schools_2.count_buildings() + schools_3.count_buildings();
	const int trained_count_2=echo.get_team_stats().upgradeState[BUILD][2];

	if(stat->totalUnit>=50 && school_counts_2>0 && trained_count_2>5)
	{
		upgrading_phase_2=true;
	}
	else
	{
		upgrading_phase_2=false;
	}

	///Qualifications for the war preperation phase:
	///1) Atleast 30 units
	///2) Less than 2 barracks OR
	///3) Less than 30 trained warriors
	BuildingSearch barracks(echo);
	barracks.add_condition(new SpecificBuildingType(IntBuildingType::ATTACK_BUILDING));
	int barracks_count=barracks.count_buildings();

	int warrior_count=0;
	for(int i=1; i<=3; ++i)
	{
		warrior_count += stat->upgradeState[ATTACK_SPEED][i];
	}

	if(stat->totalUnit>=30 && (warrior_count < 30 || barracks_count<2))
	{
		war_preperation=true;
	}
	else
	{
		war_preperation=false;
	}

	///Qualifications for the war phase:
	///Atleast 20 trained warriors
	if(warrior_count >= 20)
	{
		war=true;
	}
	else
	{
		war=false;
	}

	///Qualifcations for the fruit phase:
	///Atleast 40 units, and fruits on the map
	if(echo.is_fruit_on_map() && stat->totalUnit >= 40)
	{
		fruit_phase=true;
	}
	else
	{
		fruit_phase=false;
	}
	
	///Qualifications for the starving recovery phase:
	///1) More than 8% units hungry but not able to eat
	///2) Atleast one unit
	if(stat->totalUnit > 1)
	{
		int total_starving_percent = stat->needFoodNoInns * 100 / stat->totalUnit;
		if(total_starving_percent >= 8)
		{
			starving_recovery=true;
		}
		else
		{
			starving_recovery=false;
		}
	}
	else
	{
		starving_recovery=false;
	}
	
	///Qualifications for the no worker phase:
	///1) More than 10% workers free
	///2) Atleast one worker
	if(stat->numberUnitPerType[WORKER] > 0)
	{
		const int workers_free = stat->isFree[WORKER] * 100 / stat->numberUnitPerType[WORKER];
		if(workers_free > 10)
		{
			no_workers_phase=true;
		}
		else
		{
			no_workers_phase=false;
		}
	}
	else
	{
		no_workers_phase=false;
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

	const int score = number1*8 + number2*12 + number3*16;
	///A level 1 Inn can handle 8 units, a level 2 can handle 12 and a level 3 can handle 16
	if((total_workers+total_explorers+total_warriors)>=(number1*8 + number2*12 + number3*16))
	{
		placement_queue.push(RegularInn);
	}
	
	//Place for starving recovery inns
	if(starving_recovery)
	{
		int total_starving = stat->needFoodNoInns;
		int required_inns = total_starving / 6;
		if(starving_recovery_inns < required_inns)
		{
			starving_recovery_inns += 1;
			placement_queue.push(StarvingRecoveryInn);
		}
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
	//const int total_unit = echo.player->team->stats.getLatestStat()->totalUnit;
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
	//const int total_unit = echo.player->team->stats.getLatestStat()->totalUnit;
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
	//const int total_unit = echo.player->team->stats.getLatestStat()->totalUnit;
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
		demand+=total_warrior/10;
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
	///Increase the maximum number of buildings under construction when starving recovery is active
	int maximum_under_construction = 2;
	if(starving_recovery)
		maximum_under_construction += 2;

	while(!construction_queue.empty() && buildings_under_construction < maximum_under_construction)
	{
		int id=-1;
		BuildingPlacement b=construction_queue.front();
		construction_queue.pop();
		if(b==RegularInn)
		{
			id=order_regular_inn(echo);
		}
		if(b==StarvingRecoveryInn)
		{
			id=order_regular_inn(echo);
			ManagementOrder* mo_completion_message=new SendMessage("finished starving recovery inn");
			mo_completion_message->add_condition(new EitherCondition(
			                             new ParticularBuilding(new NotUnderConstruction, id),
			                             new BuildingDestroyed(id)));
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
		
		ManagementOrder* mo_construction_completion_message=new SendMessage("update clearing zone "+boost::lexical_cast<std::string>(int(id)));
		mo_construction_completion_message->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
		echo.add_management_order(mo_construction_completion_message);
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
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_wheat, 8));
	//You can't be farther than 10 units from wheat
	bo->add_constraint(new AIEcho::Construction::MaximumDistance(gi_wheat, 10));

	//Constraints arround nearby settlement
	AIEcho::Gradients::GradientInfo gi_building;
	gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
	gi_building.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	//You want to be close to other buildings, but wheat is more important
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 4));

	AIEcho::Gradients::GradientInfo gi_building_construction;
	gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
	gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	//You don't want to be too close
	bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_building_construction, 3));

	///Add constraints for all enemy teams to keep distance
	AIEcho::Gradients::GradientInfo gi_enemy;
	for(enemy_team_iterator i(echo); i!=enemy_team_iterator(); ++i)
	{
		gi_enemy.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(*i, false));
	}
	bo->add_constraint(new AIEcho::Construction::MaximizedDistance(gi_enemy, 1));

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

	ManagementOrder* mo_tracker=new AddRessourceTracker(25, CORN, id);
	mo_tracker->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
	echo.add_management_order(mo_tracker);

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
	bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_building_construction, 2));

	//Add the building order to the list of orders
	unsigned int id=echo.add_building_order(bo);

	//Change the number of workers assigned when the building is finished
	ManagementOrder* mo_completion=new SendMessage(FormatableString("update swarm %0").arg(id));
	mo_completion->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
	echo.add_management_order(mo_completion);
	
	ManagementOrder* mo_tracker=new AddRessourceTracker(25, CORN, id);
	mo_tracker->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
	echo.add_management_order(mo_tracker);

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

	//Constraints arround water. Can't be too close to sand.
	AIEcho::Gradients::GradientInfo gi_sand;
	gi_sand.add_source(new AIEcho::Gradients::Entities::Sand); 
	bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_sand, 2));
	
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

	//Constraints arround water. Can't be too close to sand.
	AIEcho::Gradients::GradientInfo gi_sand;
	gi_sand.add_source(new AIEcho::Gradients::Entities::Sand); 
	bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_sand, 2));

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
	gi_building.add_obstacle(new AIEcho::Gradients::Entities::Water);
	//You want to be close to other buildings
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
//	gi_enemy.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
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
	gi_building.add_obstacle(new AIEcho::Gradients::Entities::Water);
	//You want to be close to other buildings
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 2));

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

	//Do nothing if the ressource_tracker order hasn't been processed yet
	if(! echo.get_ressource_tracker(id))
		return;
	int total_ressource_level = echo.get_ressource_tracker(id)->get_total_level();
	
	int to_assign = 0;
	if(level==1 && total_ressource_level>(5*25))
		to_assign=1;
	else if(level==1 && total_ressource_level<=(5*25))
		to_assign=2;

	if(level==2 && total_ressource_level>(8*25))
		to_assign=3;
	else if(level==2 && total_ressource_level<=(8*25))
		to_assign=5;
	
	if(level==3 && total_ressource_level>(17*25))
		to_assign=5;
	else if(level==3 && total_ressource_level<=(17*25))
		to_assign=7;
	
	///The number of units assigned to an Inn depends entirely on its level
	if(to_assign != assigned)
	{
		ManagementOrder* mo_assign=new AssignWorkers(to_assign, id);
		echo.add_management_order(mo_assign);
	}
}


void NewNicowar::manage_swarm(Echo& echo, int id)
{
	//Get some statistics
	TeamStat* stat=echo.player->team->stats.getLatestStat();
	int total_explorers=stat->numberUnitPerType[EXPLORER];
	if(stat->totalUnit == 0)
		return;
	int total_starving_percent = stat->needFoodCritical * 100 / stat->totalUnit;
	int total_hungry_percent = stat->needFood * 100 / stat->totalUnit;

	int assigned=echo.get_building_register().get_assigned(id);
	int to_assign=0;

	//Do nothing if the ressource_tracker order hasn't been processed yet
	if(! echo.get_ressource_tracker(id))
		return;
	int total_ressource_level = echo.get_ressource_tracker(id)->get_total_level();

	int worker_ratio=0;
	int explorer_ratio=0;
	int warrior_ratio=0;


	to_assign=3;
	///Assign double workers to swarms during the growth phase and the war_preperation phase
	if(growth_phase || war_preperation)
	{
		to_assign*=2;
	}

	///Double units when ressource level is low
	if(total_ressource_level <= (10 * 25))
		to_assign*=2;

	///Half units if world is hungry
	if((total_starving_percent + total_hungry_percent) > 12)
		to_assign/=2;
	
	///No units when the world is starving
	if(starving_recovery)
		to_assign=0;

	///During the growth phase, there is an 4-1 ratio between workers and explorers until
	///there are atleast 3 explorers
	if(growth_phase)
	{
		worker_ratio=4;

	}
	else
	{
		if(no_workers_phase)
			worker_ratio=0;
		else
			worker_ratio=1;
	}

	int needed_explorers=3;
	if(fruit_phase)
		needed_explorers+=9;

	if(total_explorers<needed_explorers)
		explorer_ratio=1;
	else
		explorer_ratio=0;

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

	if(buildings.size()==0)
		return -1;

	//Now choose a building, or return -1 for none available
	int random = syncRand() % buildings.size();

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

	AIEcho::Gradients::GradientInfo gi_building;
	gi_building.add_source(new Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
	gi_building.add_obstacle(new Entities::AnyRessource);
	Gradient& gradient=echo.get_gradient_manager().get_gradient(gi_building);

	for(enemy_building_iterator ebi(echo, target, -1, -1, indeterminate); ebi!=enemy_building_iterator(); ++ebi)
	{
		Building* b=echo.player->game->teams[target]->myBuildings[Building::GIDtoID(*ebi)];
		if(gradient.get_height(b->posX, b->posY) != -2)
			buildings_to_attack.push_back(*ebi);
	}

	if(buildings_to_attack.size() == 0)
		return -1;

	int num=syncRand() % buildings_to_attack.size();
	return buildings_to_attack[num];
}


void NewNicowar::attack_building(Echo& echo)
{
	int building=choose_building_to_attack(echo);
	if(building==-1)
	{
		if(!is_digging_out)
			dig_out_enemy(echo);
		return;
	}
	BuildingOrder* bo = new BuildingOrder(IntBuildingType::WAR_FLAG, 15);
	bo->add_constraint(new CenterOfBuilding(building));
	unsigned int id=echo.add_building_order(bo);

	ManagementOrder* mo_destroyed_1=new DestroyBuilding(id);
	mo_destroyed_1->add_condition(new EnemyBuildingDestroyed(echo, building));
	echo.add_management_order(mo_destroyed_1);

	ManagementOrder* mo_destroyed_2=new SendMessage("attack finished "+boost::lexical_cast<std::string>(id));
	mo_destroyed_2->add_condition(new BuildingDestroyed(id));
	echo.add_management_order(mo_destroyed_2);
	
	attack_flags.push_back(id);
}


void NewNicowar::control_attacks(Echo& echo)
{
	choose_enemy_target(echo);

	if(target!=-1)
	{
		int number_attacks=0;
		if(war)
		{
			number_attacks=2;
		}
		
	
		if(attack_flags.size() < number_attacks)
		{
			attack_building(echo);
		}
	}

	BuildingSearch bs_pool(echo);
	bs_pool.add_condition(new SpecificBuildingType(IntBuildingType::SWIMSPEED_BUILDING));
	int num_pool=bs_pool.count_buildings();
	
	for(int i=0; i<attack_flags.size(); ++i)
	{
		Building* b = echo.get_building_register().get_building(i);
		if((num_pool && b->locked[true]) || (num_pool==0 && b->locked[false]))
		{
			ManagementOrder* mo_destroy=new DestroyBuilding(i);
			echo.add_management_order(mo_destroy);
		}
	}
}



void NewNicowar::choose_enemy_target(Echo& echo)
{
	if(target==-1 || !echo.player->game->teams[target]->isAlive)
	{
		std::vector<int> available_targets;		
		for(enemy_team_iterator i(echo); i!=enemy_team_iterator(); ++i)
		{
			if(echo.player->game->teams[*i]->isAlive)
			{
				enemy_building_iterator ebi(echo, *i, -1, -1, indeterminate);
				/* Make sure we know of at least one
				   building before committing to a
				   particular enemy.  It used to be that we
				   did not (normally) need to test this,
				   because all starting buildings were
				   known.  But that was cheating and has
				   been fixed. */
				if (ebi != enemy_building_iterator())
				{
					available_targets.push_back(*i);
				}
			}
		}
		if(available_targets.size()==0)
			target=-1;
		else
			target=available_targets[syncRand() % available_targets.size()];
	}
}



void NewNicowar::dig_out_enemy(Echo& echo)
{
	///First choose an enemy building to dig out
	std::vector<int> buildings_to_attack;
	buildings_to_attack.reserve(100);

	AIEcho::Gradients::GradientInfo gi_building;
	gi_building.add_source(new Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
	gi_building.add_obstacle(new Entities::AnyRessource);
	Gradient& gradient=echo.get_gradient_manager().get_gradient(gi_building);

	for(enemy_building_iterator ebi(echo, target, -1, -1, indeterminate); ebi!=enemy_building_iterator(); ++ebi)
	{
		Building* b=echo.player->game->teams[target]->myBuildings[Building::GIDtoID(*ebi)];
		if(gradient.get_height(b->posX, b->posY) == -2)
			buildings_to_attack.push_back(*ebi);
	}

	if(buildings_to_attack.size() == 0)
		return;

	int num=syncRand() % buildings_to_attack.size();


	int building=buildings_to_attack[num];
	const int bx=echo.player->game->teams[target]->myBuildings[Building::GIDtoID(building)]->posX;
	const int by=echo.player->game->teams[target]->myBuildings[Building::GIDtoID(building)]->posY;

	AIEcho::Gradients::GradientInfo gi_pathfind;
	gi_pathfind.add_source(new Entities::Position(bx, by));
	gi_pathfind.add_obstacle(new Entities::Ressource(STONE));
	Gradient& gradient_pathfind=echo.get_gradient_manager().get_gradient(gi_pathfind);

	///Next, find the closest point manhattan distance wise, to the building that is accessible
	MapInfo mi(echo);
	int closest_x=0;
	int closest_y=0;
	int closest_distance=10000;
	for(int x=0; x<mi.get_width(); ++x)
	{
		for(int y=0; y<mi.get_height(); ++y)
		{
			if(gradient.get_height(x, y) >= 0)
			{
				int dist=gradient_pathfind.get_height(x, y);
				if(dist < closest_distance)
				{
					closest_x=x;
					closest_y=y;
					closest_distance=dist;
				}
			}
		}
	}

	///Next, follow a path arround stone between the closest point and the buildings position, 
	///placing Clearing flags as you go

	int xpos=closest_x;
	int ypos=closest_y;

	int flag_dist_count=3;

	int w=mi.get_width();
	int h=mi.get_height();

	while(xpos != bx || ypos!=by)
	{
		int nxpos = xpos;
		int nypos = ypos;
		int rx=(xpos+1+w) % w;
		int lx=(xpos-1+w) % w;
		int dy=(ypos+1+h) % h;
		int uy=(ypos-1+h) % h;
		int lowest_entity=gradient_pathfind.get_height(xpos, ypos)+2;

		//Test diagnols first, then the horizontals and verticals.
		if(gradient_pathfind.get_height(lx, uy) < lowest_entity && gradient_pathfind.get_height(lx, uy)>=0)
		{
			lowest_entity=gradient_pathfind.get_height(lx, uy);
			nxpos=lx;
			nypos=uy;
		}
		if(gradient_pathfind.get_height(rx, uy) < lowest_entity && gradient_pathfind.get_height(rx, uy)>=0)
		{
			lowest_entity=gradient_pathfind.get_height(rx, uy);
			nxpos=rx;
			nypos=uy;
		}
		if(gradient_pathfind.get_height(lx, dy) < lowest_entity && gradient_pathfind.get_height(lx, dy)>=0)
		{
			lowest_entity=gradient_pathfind.get_height(lx, dy);
			nxpos=lx;
			nypos=dy;
		}
		if(gradient_pathfind.get_height(rx, dy) < lowest_entity && gradient_pathfind.get_height(rx, dy)>=0)
		{
			lowest_entity=gradient_pathfind.get_height(rx, dy);
			nxpos=rx;
			nypos=dy;
		}

		if(gradient_pathfind.get_height(xpos, uy) < lowest_entity && gradient_pathfind.get_height(xpos, uy)>=0)
		{
			lowest_entity=gradient_pathfind.get_height(xpos, uy);
			nxpos=xpos;
			nypos=uy;
		}
		if(gradient_pathfind.get_height(lx, ypos) < lowest_entity && gradient_pathfind.get_height(lx, ypos)>=0)
		{
			lowest_entity=gradient_pathfind.get_height(lx, ypos);
			nxpos=lx;
			nypos=ypos;
		}
		if(gradient_pathfind.get_height(rx, ypos) < lowest_entity && gradient_pathfind.get_height(rx, ypos)>=0)
		{
			lowest_entity=gradient_pathfind.get_height(rx, ypos);
			nxpos=rx;
			nypos=ypos;
		}
		if(gradient_pathfind.get_height(xpos, dy) < lowest_entity && gradient_pathfind.get_height(xpos, dy)>=0)
		{
			lowest_entity=gradient_pathfind.get_height(xpos, dy);
			nxpos=xpos;
			nypos=dy;
		}


		flag_dist_count+=1;


		if(flag_dist_count>3)
		{
			flag_dist_count=0;
			//The main order for the clearing flag
			BuildingOrder* bo_flag = new BuildingOrder(IntBuildingType::CLEARING_FLAG, 10);
			//Place it on the current point
			bo_flag->add_constraint(new Construction::SinglePosition(xpos, ypos));
			//Add the building order to the list of orders
			unsigned int id_flag=echo.add_building_order(bo_flag);

			ManagementOrder* mo_destroyed=new DestroyBuilding(id_flag);
			mo_destroyed->add_condition(new EnemyBuildingDestroyed(echo, building));
			echo.add_management_order(mo_destroyed);

			ManagementOrder* mo_completion=new ChangeFlagSize(3, id_flag);
			echo.add_management_order(mo_completion);
		}
		xpos = nxpos;
		ypos = nypos;

	}

	ManagementOrder* mo_destroyed=new SendMessage("finished digging out");
	mo_destroyed->add_condition(new EnemyBuildingDestroyed(echo, building));
	echo.add_management_order(mo_destroyed);

	is_digging_out=true;
}



void NewNicowar::update_farming(Echo& echo)
{
	//Farming wheat and wood near water
	AddArea* mo_farming=new AddArea(ForbiddenArea);
	RemoveArea* mo_non_farming=new RemoveArea(ForbiddenArea);
	AIEcho::Gradients::GradientInfo gi_water;
	gi_water.add_source(new Entities::Water);
	Gradient& gradient=echo.get_gradient_manager().get_gradient(gi_water);
	MapInfo mi(echo);
	for(int x=0; x<mi.get_width(); ++x)
	{
		for(int y=0; y<mi.get_height(); ++y)
		{
			if((mi.is_ressource(x, y, WOOD) ||
				mi.is_ressource(x, y, CORN)) &&
				mi.is_discovered(x, y))
			{
				/*if(mi.backs_onto_sand(x, y))
				{
					if(!mi.is_forbidden_area(x, y))
						mo_farming->add_location(x, y);
				}
				else */if((x%2==1 && y%2==1) &&
						gradient.get_height(x, y)<(mi.is_ressource(x, y, WOOD) ? 6 : 10))
				{
					if(!mi.is_forbidden_area(x, y))
						mo_farming->add_location(x, y);
				}
				else if(mi.is_forbidden_area(x, y))
				{
					mo_non_farming->add_location(x, y);
				}
			}
			else if(mi.is_forbidden_area(x, y))
			{
				mo_non_farming->add_location(x, y);
			}
		}
	}
	echo.add_management_order(mo_farming);
	echo.add_management_order(mo_non_farming);
}


void NewNicowar::update_fruit_flags(AIEcho::Echo& echo)
{
	if(fruit_phase && !exploration_on_fruit)
	{
		//Constraints arround nearby settlement
		AIEcho::Gradients::GradientInfo gi_building;
		gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));


		//The main order for the exploration flag on cherry
		BuildingOrder* bo_cherry = new BuildingOrder(IntBuildingType::EXPLORATION_FLAG, 2);
		//You want the closest fruit to your settlement possible
		bo_cherry->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 1));
		//Constraint arround the location of fruit
		AIEcho::Gradients::GradientInfo gi_cherry;
		gi_cherry.add_source(new AIEcho::Gradients::Entities::Ressource(CHERRY));
		//You want to be ontop of the cherry trees
		bo_cherry->add_constraint(new AIEcho::Construction::MaximumDistance(gi_cherry, 0));
		//Add the building order to the list of orders
		unsigned int id_cherry=echo.add_building_order(bo_cherry);
		ManagementOrder* mo_completion_cherry=new ChangeFlagSize(4, id_cherry);
		echo.add_management_order(mo_completion_cherry);



		//The main order for the exploration flag in orange
		BuildingOrder* bo_orange = new BuildingOrder(IntBuildingType::EXPLORATION_FLAG, 2);
		//You want the closest fruit to your settlement possible
		bo_orange->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 1));
		//Constraints arround the location of fruit
		AIEcho::Gradients::GradientInfo gi_orange;
		gi_orange.add_source(new AIEcho::Gradients::Entities::Ressource(ORANGE));
		//You want to be ontop of the orange trees
		bo_orange->add_constraint(new AIEcho::Construction::MaximumDistance(gi_orange, 0));
		unsigned int id_orange=echo.add_building_order(bo_orange);
		ManagementOrder* mo_completion_orange=new ChangeFlagSize(4, id_orange);
		echo.add_management_order(mo_completion_orange);

		//The main order for the exploration flag on prunes
		BuildingOrder* bo_prune = new BuildingOrder(IntBuildingType::EXPLORATION_FLAG, 2);
		//You want the closest fruit to your settlement possible
		bo_prune->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 1));
		AIEcho::Gradients::GradientInfo gi_prune;
		gi_prune.add_source(new AIEcho::Gradients::Entities::Ressource(PRUNE));
		//You want to be ontop of the prune trees
		bo_prune->add_constraint(new AIEcho::Construction::MaximumDistance(gi_prune, 0));
		//Add the building order to the list of orders
		unsigned int id_prune=echo.add_building_order(bo_prune);
		ManagementOrder* mo_completion_prune=new ChangeFlagSize(4, id_prune);
		echo.add_management_order(mo_completion_prune);



		exploration_on_fruit=true;
	}
	update_fruit_alliances(echo);
}


void NewNicowar::update_fruit_alliances(AIEcho::Echo& echo)
{
	bool activated=fruit_phase;

	for(enemy_team_iterator i(echo); i!=enemy_team_iterator(); ++i)
	{
		ManagementOrder* mo_alliance=new ChangeAlliances(*i, indeterminate, indeterminate, indeterminate, activated, indeterminate);
		echo.add_management_order(mo_alliance);
	}
}

