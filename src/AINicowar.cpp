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


NicowarStrategy::NicowarStrategy()
{

}



void NicowarStrategy::loadFromConfigFile(const ConfigBlock *configBlock)
{
	configBlock->load(growth_phase_unit_max, "growth_phase_unit_max");
	configBlock->load(skilled_work_phase_unit_min, "skilled_work_phase_unit_min");
	configBlock->load(upgrading_phase_1_school_min, "upgrading_phase_1_school_min");
	configBlock->load(upgrading_phase_1_unit_min, "upgrading_phase_1_unit_min");
	configBlock->load(upgrading_phase_1_trained_worker_min, "upgrading_phase_1_trained_worker_min");
	configBlock->load(upgrading_phase_2_school_min, "upgrading_phase_2_school_min");
	configBlock->load(upgrading_phase_2_unit_min, "upgrading_phase_2_unit_min");
	configBlock->load(upgrading_phase_2_trained_worker_min, "upgrading_phase_2_trained_worker_min");
	configBlock->load(minimum_warrior_level_for_trained, "minimum_warrior_level_for_trained");
	configBlock->load(war_preperation_phase_unit_min, "war_preperation_phase_unit_min");
	configBlock->load(war_preperation_phase_barracks_max, "war_preperation_phase_barracks_max");
	configBlock->load(war_preperation_phase_trained_warrior_max, "war_preperation_phase_trained_warrior_max");
	configBlock->load(war_phase_trained_warrior_min, "war_phase_trained_warrior_min");
	configBlock->load(fruit_phase_unit_min, "fruit_phase_unit_min");
	configBlock->load(starvation_recovery_phase_starving_no_inn_min_percent, "starvation_recovery_phase_starving_no_inn_min_percent");
	configBlock->load(starving_recovery_phase_unfed_per_new_inn, "starving_recovery_phase_unfed_per_new_inn");
	configBlock->load(no_workers_phase_free_worker_minimum_percent, "no_workers_phase_free_worker_minimum_percent");
	configBlock->load(level_1_inn_units_can_feed, "level_1_inn_units_can_feed");
	configBlock->load(level_2_inn_units_can_feed, "level_2_inn_units_can_feed");
	configBlock->load(level_3_inn_units_can_feed, "level_3_inn_units_can_feed");
	configBlock->load(growth_phase_units_per_swarm, "growth_phase_units_per_swarm");
	configBlock->load(non_growth_phase_units_per_swarm, "non_growth_phase_units_per_swarm");
	configBlock->load(growth_phase_maximum_swarms, "growth_phase_maximum_swarms");
	configBlock->load(skilled_work_phase_number_of_racetracks, "skilled_work_phase_number_of_racetracks");
	configBlock->load(skilled_work_phase_number_of_swimmingpools, "skilled_work_phase_number_of_swimmingpools");
	configBlock->load(skilled_work_phase_number_of_schools, "skilled_work_phase_number_of_schools");
	configBlock->load(war_preparation_phase_number_of_barracks, "war_preparation_phase_number_of_barracks");
	configBlock->load(base_number_of_hospitals, "base_number_of_hospitals");
	configBlock->load(war_preperation_phase_warriors_per_hospital, "war_preperation_phase_warriors_per_hospital");
	configBlock->load(base_number_of_construction_sites, "base_number_of_construction_sites");
	configBlock->load(starving_recovery_phase_number_of_extra_construction_sites, "starving_recovery_phase_number_of_extra_construction_sites");
	configBlock->load(level_1_inn_low_wheat_trigger_ammount, "level_1_inn_low_wheat_trigger_ammount");
	configBlock->load(level_2_inn_low_wheat_trigger_ammount, "level_2_inn_low_wheat_trigger_ammount");
	configBlock->load(level_3_inn_low_wheat_trigger_ammount, "level_3_inn_low_wheat_trigger_ammount");
	configBlock->load(level_1_inn_units_assigned_normal_wheat, "level_1_inn_units_assigned_normal_wheat");
	configBlock->load(level_2_inn_units_assigned_normal_wheat, "level_2_inn_units_assigned_normal_wheat");
	configBlock->load(level_3_inn_units_assigned_normal_wheat, "level_3_inn_units_assigned_normal_wheat");
	configBlock->load(level_1_inn_units_assigned_low_wheat, "level_1_inn_units_assigned_low_wheat");
	configBlock->load(level_2_inn_units_assigned_low_wheat, "level_2_inn_units_assigned_low_wheat");
	configBlock->load(level_3_inn_units_assigned_low_wheat, "level_3_inn_units_assigned_low_wheat");
	configBlock->load(base_swarm_units_assigned, "base_swarm_units_assigned");
	configBlock->load(base_swarm_low_wheat_trigger_ammount, "base_swarm_low_wheat_trigger_ammount");
	configBlock->load(base_swarm_hungry_reduce_trigger_percent, "base_swarm_hungry_reduce_trigger_percent");
	configBlock->load(growth_phase_swarm_worker_ratio, "growth_phase_swarm_worker_ratio");
	configBlock->load(non_growth_phase_swarm_worker_ratio, "non_growth_phase_swarm_worker_ratio");
	configBlock->load(base_number_of_explorers, "base_number_of_explorers");
	configBlock->load(fruit_phase_extra_number_of_explorers, "fruit_phase_extra_number_of_explorers");
	configBlock->load(base_swarm_explorer_ratio, "base_swarm_explorer_ratio");
	configBlock->load(war_preperation_swarm_warrior_ratio, "war_preperation_swarm_warrior_ratio");
	configBlock->load(upgrading_phase_1_inn_chance, "upgrading_phase_1_inn_chance");
	configBlock->load(upgrading_phase_1_hospital_chance, "upgrading_phase_1_hospital_chance");
	configBlock->load(upgrading_phase_1_racetrack_chance, "upgrading_phase_1_racetrack_chance");
	configBlock->load(upgrading_phase_1_swimmingpool_chance, "upgrading_phase_1_swimmingpool_chance");
	configBlock->load(upgrading_phase_1_barracks_chance, "upgrading_phase_1_barracks_chance");
	configBlock->load(upgrading_phase_1_school_chance, "upgrading_phase_1_school_chance");
	configBlock->load(upgrading_phase_1_tower_chance, "upgrading_phase_1_tower_chance");
	configBlock->load(upgrading_phase_2_inn_chance, "upgrading_phase_2_inn_chance");
	configBlock->load(upgrading_phase_2_hospital_chance, "upgrading_phase_2_hospital_chance");
	configBlock->load(upgrading_phase_2_racetrack_chance, "upgrading_phase_2_racetrack_chance");
	configBlock->load(upgrading_phase_2_swimmingpool_chance, "upgrading_phase_2_swimmingpool_chance");
	configBlock->load(upgrading_phase_2_barracks_chance, "upgrading_phase_2_barracks_chance");
	configBlock->load(upgrading_phase_2_school_chance, "upgrading_phase_2_school_chance");
	configBlock->load(upgrading_phase_2_tower_chance, "upgrading_phase_2_tower_chance");
	configBlock->load(upgrading_phase_1_units_assigned, "upgrading_phase_1_units_assigned");
	configBlock->load(upgrading_phase_2_units_assigned, "upgrading_phase_2_units_assigned");
	configBlock->load(upgrading_phase_1_num_upgrades_at_once, "upgrading_phase_1_num_upgrades_at_once");
	configBlock->load(upgrading_phase_2_num_upgrades_at_once, "upgrading_phase_2_num_upgrades_at_once");
	configBlock->load(war_phase_war_flag_units_assigned, "war_phase_war_flag_units_assigned");
	configBlock->load(war_phase_num_attack_flags, "war_phase_num_attack_flags");
	
}


std::string NicowarStrategy::getStrategyName()
{
	return name;
}
	

void NicowarStrategy::setStrategyName(const std::string& name)
{
	this->name=name;
}


NicowarStrategyLoader::NicowarStrategyLoader()
{
	ConfigVector<NicowarStrategy>::load("data/nicowar.default.txt", true);
	ConfigVector<NicowarStrategy>::load("data/nicowar.txt");
}


	
NicowarStrategy NicowarStrategyLoader::chooseRandomStrategy()
{
	int chosen = syncRand() % entries.size();
	entries[chosen]->setStrategyName(entriesToName[chosen]);
	return *entries[chosen];
}



NicowarStrategy NicowarStrategyLoader::getParticularStrategy(const std::string& name)
{
	entries[nameToEntries[name]]->setStrategyName(name);
	return *entries[nameToEntries[name]];
}



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
	can_swim=false;
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
		if(versionMinor >= 60)
		{
			std::string strategyName = stream->readText("strategy_name");
			NicowarStrategyLoader loader;
			strategy = loader.getParticularStrategy(strategyName);
		}
		else
		{
			NicowarStrategyLoader loader;
			strategy = loader.getParticularStrategy("default");
		}
		growth_phase=stream->readUint8("growth_phase");
		skilled_work_phase=stream->readUint8("skilled_work_phase");
		upgrading_phase_1=stream->readUint8("upgrading_phase_1");
		upgrading_phase_2=stream->readUint8("upgrading_phase_2");
		war_preperation=stream->readUint8("war_preperation");
		war=stream->readUint8("war");
		fruit_phase=stream->readUint8("fruit_phase");
		starving_recovery=stream->readUint8("starving_recovery");
		no_workers_phase=stream->readUint8("no_workers_phase");
		if(versionMinor >= 60)
			can_swim=stream->readUint8("can_swim");
		
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
			placement_queue.push_back(bp);
			stream->readLeaveSection();
		}
		stream->readLeaveSection();

		stream->readEnterSection("construction_queue");
		size = stream->readUint16("size");
		for(size_t n = 0; n<size; ++n)
		{
			stream->readEnterSection(n);
			BuildingPlacement bp = static_cast<BuildingPlacement>(stream->readUint8("placement"));
			construction_queue.push_back(bp);
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
	stream->writeText(strategy.getStrategyName(), "strategy_name");
	stream->writeUint8(growth_phase, "growth_phase");
	stream->writeUint8(skilled_work_phase, "skilled_work_phase");
	stream->writeUint8(upgrading_phase_1, "upgrading_phase_1");
	stream->writeUint8(upgrading_phase_2, "upgrading_phase_2");
	stream->writeUint8(war_preperation, "war_preperation");
	stream->writeUint8(war, "war");
	stream->writeUint8(fruit_phase, "fruit_phase");
	stream->writeUint8(starving_recovery, "starving_recovery");
	stream->writeUint8(no_workers_phase, "no_workers_phase");
	stream->writeUint8(can_swim, "can_swim");
	stream->writeUint8(starving_recovery_inns, "starving_recovery_inns");
	stream->writeUint32(buildings_under_construction, "buildings_under_construction");
	for(int n=0; n<PlacementSize; ++n)
	{
		stream->writeUint8(buildings_under_construction_per_type[n], FormatableString("buildings_under_construction_per_type[%0]").arg(n).c_str());
	}
		
	stream->writeEnterSection("placement_queue");
	stream->writeUint16(placement_queue.size(), "size");
	size_t n = 0;
	for(std::list<BuildingPlacement>::iterator i = placement_queue.begin(); i!=placement_queue.end(); ++i)
	{
		stream->writeEnterSection(n);
		stream->writeUint8(static_cast<Uint8>(*i), "placement");
		stream->writeLeaveSection();
		n+=1;
	}
	stream->writeLeaveSection();

	stream->writeEnterSection("construction_queue");
	stream->writeUint16(construction_queue.size(), "size");
	n = 0;
	for(std::list<BuildingPlacement>::iterator i = construction_queue.begin(); i!=construction_queue.end(); ++i)
	{
		stream->writeEnterSection(n);
		stream->writeUint8(static_cast<Uint8>(*i), "placement");
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



void NewNicowar::selectStrategy()
{
	NicowarStrategyLoader loader;
	strategy = loader.chooseRandomStrategy();
	//strategy = loader.getParticularStrategy("default");
}



void NewNicowar::initialize(Echo& echo)
{
	selectStrategy();

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
	///1) Less than strategy.growth_phase_unit_max units
	if(stat->totalUnit<strategy.growth_phase_unit_max)
	{
		growth_phase=true;
	}
	else
	{
		growth_phase=false;
	}

	///Qualifications for the skilled work phase:
	///1) More than strategy.skilled_work_phase_unit_min units
	if(stat->totalUnit>=strategy.skilled_work_phase_unit_min)
	{
		skilled_work_phase=true;
	}
	else
	{
		skilled_work_phase=false;
	}

	///Qualifications for the upgrading phase 1:
	///1) Atleast strategy.upgrading_phase_1_school_min schools
	///2) Atleast strategy.upgrading_phase_1_unit_min units
	///3) Atleast strategy.upgrading_phase_1_trained_worker_min of them are trained for upgrading to level 2
	BuildingSearch schools(echo);
	schools.add_condition(new SpecificBuildingType(IntBuildingType::SCIENCE_BUILDING));
	schools.add_condition(new NotUnderConstruction);
	const int school_counts=schools.count_buildings();
	const int trained_count=echo.get_team_stats().upgradeState[BUILD][1];

	if(stat->totalUnit>=strategy.upgrading_phase_1_unit_min && school_counts>strategy.upgrading_phase_1_school_min && trained_count>strategy.upgrading_phase_1_trained_worker_min)
	{
		upgrading_phase_1=true;
	}
	else
	{
		upgrading_phase_1=false;
	}

	///Qualifications for the upgrading phase 2:
	///1) Atleast strategy.upgrading_phase_2_school_min level 2 or level 3 schools
	///2) Atleast strategy.upgrading_phase_2_unit_min units
	///3) Atleast strategy.upgrading_phase_2_trained_worker_min of them are trained for upgrading to level 3
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

	if(stat->totalUnit>=strategy.upgrading_phase_2_unit_min && school_counts_2>strategy.upgrading_phase_2_school_min && trained_count_2>strategy.upgrading_phase_2_trained_worker_min)
	{
		upgrading_phase_2=true;
	}
	else
	{
		upgrading_phase_2=false;
	}

	///Qualifications for the war preperation phase:
	///1) Atleast strategy.war_preperation_phase_unit_min units
	///2) Less than strategy.war_preperation_phase_barracks_max barracks OR
	///3) Less than strategy.war_preperation_phase_trained_warrior_max trained warriors
	BuildingSearch barracks(echo);
	barracks.add_condition(new SpecificBuildingType(IntBuildingType::ATTACK_BUILDING));
	int barracks_count=barracks.count_buildings();

	int warrior_count=0;
	for(int i=strategy.minimum_warrior_level_for_trained; i<=3; ++i)
	{
		warrior_count += stat->upgradeState[ATTACK_SPEED][i];
	}

	if(stat->totalUnit>=strategy.war_preperation_phase_unit_min && (warrior_count < strategy.war_preperation_phase_trained_warrior_max || barracks_count<strategy.war_preperation_phase_barracks_max))
	{
		war_preperation=true;
	}
	else
	{
		war_preperation=false;
	}

	///Qualifications for the war phase:
	///Atleast strategy.war_phase_trained_warrior_min trained warriors
	if(warrior_count >= strategy.war_phase_trained_warrior_min)
	{
		war=true;
	}
	else
	{
		war=false;
	}

	///Qualifcations for the fruit phase:
	///Atleast strategy.fruit_phase_unit_min units, and fruits on the map
	if(echo.is_fruit_on_map() && stat->totalUnit >= strategy.fruit_phase_unit_min)
	{
		fruit_phase=true;
	}
	else
	{
		fruit_phase=false;
	}
	
	///Qualifications for the starving recovery phase:
	///1) More than strategy.starvation_recovery_phase_starving_no_inn_min_percent % units hungry but not able to eat
	///2) Atleast one unit (because of devision by 0)
	if(stat->totalUnit > 1)
	{
		int total_starving_percent = stat->needFoodNoInns * 100 / stat->totalUnit;
		if(total_starving_percent >= strategy.starvation_recovery_phase_starving_no_inn_min_percent)
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
	///1) More than strategy.no_workers_phase_free_worker_minimum_percen % workers free
	///2) Atleast one worker (because of devision by 0)
	if(stat->numberUnitPerType[WORKER] > 0)
	{
		const int workers_free = stat->isFree[WORKER] * 100 / stat->numberUnitPerType[WORKER];
		if(workers_free > strategy.no_workers_phase_free_worker_minimum_percent)
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
	
	///Qualifications for the can swim phase:
	///1) Atleast one worker that can swim
	int total_can_swim=0;
	for(int i=0; i<4; ++i)
		total_can_swim += stat->upgradeStatePerType[WORKER][SWIM][i];
	if(total_can_swim>0)
	{
		can_swim=true;
	}
	else
	{
		can_swim=false;
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

	const int score =
		  number1*strategy.level_1_inn_units_can_feed
		+ number2*strategy.level_2_inn_units_can_feed
		+ number3*strategy.level_3_inn_units_can_feed;
	///(by default), A level 1 Inn can handle 8 units, a level 2 can handle 12 and a level 3 can handle 16
	if((total_workers+total_explorers+total_warriors)>=score)
	{
		placement_queue.push_back(RegularInn);
	}
	
	//Place for starving recovery inns
	if(starving_recovery)
	{
		int total_starving = stat->needFoodNoInns;
		int required_inns = total_starving / strategy.starving_recovery_phase_unfed_per_new_inn;
		if(starving_recovery_inns < required_inns)
		{
			starving_recovery_inns += 1;
			placement_queue.push_back(StarvingRecoveryInn);
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
		demand = std::min(strategy.growth_phase_maximum_swarms, 1 + total_unit/strategy.growth_phase_units_per_swarm);
	}
	else
	{
		demand = (total_unit/strategy.non_growth_phase_units_per_swarm);
	}

	if(demand > swarm_count)
	{
		placement_queue.push_back(RegularSwarm);
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
		demand=strategy.skilled_work_phase_number_of_racetracks;
	}

	if(demand > racetrack_count)
	{
		placement_queue.push_back(RegularRacetrack);
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
		demand=strategy.skilled_work_phase_number_of_swimmingpools;
	}

	if(demand > swimmingpool_count)
	{
		placement_queue.push_back(RegularSwimmingpool);
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
		demand=strategy.skilled_work_phase_number_of_schools;
	}

	if(demand > school_count)
	{
		placement_queue.push_back(RegularSchool);
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
		demand=strategy.war_preparation_phase_number_of_barracks;
	}

	if(demand > barracks_count)
	{
		placement_queue.push_back(RegularBarracks);
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

	int demand=strategy.base_number_of_hospitals;
	if(war_preperation || war)
	{
		demand+=total_warrior/strategy.war_preperation_phase_warriors_per_hospital;
	}

	if(demand > hospital_count)
	{
		placement_queue.push_back(RegularHospital);
	}
}



void NewNicowar::order_buildings(Echo& echo)
{
	while(!placement_queue.empty())
	{
		BuildingPlacement b=placement_queue.front();
		placement_queue.erase(placement_queue.begin());
		construction_queue.push_back(b);
		buildings_under_construction_per_type[int(b)]+=1;
	}
	///Increase the maximum number of buildings under construction when starving recovery is active
	int maximum_under_construction = strategy.base_number_of_construction_sites;
	if(starving_recovery)
		maximum_under_construction += strategy.starving_recovery_phase_number_of_extra_construction_sites;

	while(!construction_queue.empty() && buildings_under_construction < maximum_under_construction)
	{
		int id=-1;
		BuildingPlacement b=construction_queue.front();
		construction_queue.erase(construction_queue.begin());
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
	if(!can_swim)
		gi_building.add_obstacle(new AIEcho::Gradients::Entities::Water);
	//You want to be close to other buildings, but wheat is more important
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 4));

	AIEcho::Gradients::GradientInfo gi_building_construction;
	gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
	gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	if(!can_swim)
		gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::Water);
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
	if(!can_swim)
		gi_building.add_obstacle(new AIEcho::Gradients::Entities::Water);
	//You want to be close to other buildings, but wheat is more important
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 1));

	AIEcho::Gradients::GradientInfo gi_building_construction;
	gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
	gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	if(!can_swim)
		gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::Water);
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
	if(!can_swim)
		gi_building.add_obstacle(new AIEcho::Gradients::Entities::Water);
	//You want to be close to other buildings, but wheat is more important
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 2));

	//Constraints arround water. Can't be too close to sand.
	AIEcho::Gradients::GradientInfo gi_sand;
	gi_sand.add_source(new AIEcho::Gradients::Entities::Sand); 
	bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_sand, 2));
	
	AIEcho::Gradients::GradientInfo gi_building_construction;
	gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
	gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	if(!can_swim)
		gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::Water);
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
	if(!can_swim)
		gi_building.add_obstacle(new AIEcho::Gradients::Entities::Water);
	//You want to be close to other buildings, but wheat is more important
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 2));

	//Constraints arround water. Can't be too close to sand.
	AIEcho::Gradients::GradientInfo gi_sand;
	gi_sand.add_source(new AIEcho::Gradients::Entities::Sand); 
	bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_sand, 2));

	AIEcho::Gradients::GradientInfo gi_building_construction;
	gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
	gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	if(!can_swim)
		gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::Water);
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
	if(!can_swim)
		gi_building.add_obstacle(new AIEcho::Gradients::Entities::Water);
	//You want to be close to other buildings
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 2));

	AIEcho::Gradients::GradientInfo gi_building_construction;
	gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
	gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	if(!can_swim)
		gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::Water);
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
	if(!can_swim)
		gi_building.add_obstacle(new AIEcho::Gradients::Entities::Water);
	//You want to be close to other buildings
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 2));

	AIEcho::Gradients::GradientInfo gi_building_construction;
	gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
	gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	if(!can_swim)
		gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::Water);
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
	if(!can_swim)
		gi_building.add_obstacle(new AIEcho::Gradients::Entities::Water);
	//You want to be close to other buildings
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 3));

	AIEcho::Gradients::GradientInfo gi_building_construction;
	gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
	gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	if(!can_swim)
		gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::Water);
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
	if(level==1 && total_ressource_level>(strategy.level_1_inn_low_wheat_trigger_ammount*25))
		to_assign=strategy.level_1_inn_units_assigned_normal_wheat;
	else if(level==1 && total_ressource_level<=(strategy.level_1_inn_low_wheat_trigger_ammount*25))
		to_assign=strategy.level_1_inn_units_assigned_low_wheat;

	if(level==2 && total_ressource_level>(strategy.level_2_inn_low_wheat_trigger_ammount*25))
		to_assign=strategy.level_2_inn_units_assigned_normal_wheat;
	else if(level==2 && total_ressource_level<=(strategy.level_2_inn_low_wheat_trigger_ammount*25))
		to_assign=strategy.level_2_inn_units_assigned_low_wheat;
	
	if(level==3 && total_ressource_level>(strategy.level_3_inn_low_wheat_trigger_ammount*25))
		to_assign=strategy.level_3_inn_units_assigned_normal_wheat;
	else if(level==3 && total_ressource_level<=(strategy.level_3_inn_low_wheat_trigger_ammount*25))
		to_assign=strategy.level_3_inn_units_assigned_low_wheat;
	
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


	to_assign=strategy.base_swarm_units_assigned;
	///Assign double workers to swarms during the growth phase and the war_preperation phase
	if(growth_phase || war_preperation)
	{
		to_assign*=2;
	}

	///Double units when ressource level is low
	if(total_ressource_level <= (strategy.base_swarm_low_wheat_trigger_ammount * 25))
		to_assign*=2;

	///Half units if world is hungry
	if((total_starving_percent + total_hungry_percent) > strategy.base_swarm_hungry_reduce_trigger_percent)
		to_assign/=2;
	
	///No units when the world is starving
	if(starving_recovery)
		to_assign=0;


	///The ratio of workers during the growth phase is different, due to the fact
	///that most explorers are made during the growth phase
	if(growth_phase)
	{
		worker_ratio=strategy.growth_phase_swarm_worker_ratio;

	}
	else
	{
		if(no_workers_phase)
			worker_ratio=0;
		else
			worker_ratio=strategy.non_growth_phase_swarm_worker_ratio;
	}

	int needed_explorers=strategy.base_number_of_explorers;
	if(fruit_phase)
		needed_explorers+=strategy.fruit_phase_extra_number_of_explorers;

	if(total_explorers<needed_explorers)
		explorer_ratio=strategy.base_swarm_explorer_ratio;
	else
		explorer_ratio=0;

	///Warriors are constructed during the war preperation phase
	if(war_preperation)
	{
		warrior_ratio=strategy.war_preperation_swarm_warrior_ratio;
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
	int school_chance = strategy.upgrading_phase_1_school_chance;
	if(school_counts>0)
		school_chance=0;

	return choose_building_upgrade_type(echo, 1, strategy.upgrading_phase_1_inn_chance, strategy.upgrading_phase_1_hospital_chance, strategy.upgrading_phase_1_racetrack_chance, strategy.upgrading_phase_1_swimmingpool_chance, strategy.upgrading_phase_1_barracks_chance, school_chance, strategy.upgrading_phase_1_tower_chance);
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

	///Schools are only upgraded one at a time
	int school_chance = strategy.upgrading_phase_2_school_chance;
	if(school_counts_upgrading>0 || (school_counts_level2 + school_counts_level3)<2)
		school_chance=0;

	return choose_building_upgrade_type(echo, 2, strategy.upgrading_phase_2_inn_chance, strategy.upgrading_phase_2_hospital_chance, strategy.upgrading_phase_2_racetrack_chance, strategy.upgrading_phase_2_swimmingpool_chance, strategy.upgrading_phase_2_barracks_chance, school_chance, strategy.upgrading_phase_2_tower_chance);
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
	buildings.reserve(100);
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
		for(int n=0; n<swimmingpool_ratio; ++n)
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
		num_to_upgrade_level1=strategy.upgrading_phase_1_num_upgrades_at_once;
	}
	else
	{
		num_to_upgrade_level1=0;
	}

	if(upgrading_phase_2)
	{
		num_to_upgrade_level2=strategy.upgrading_phase_2_num_upgrades_at_once;
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
			
			ManagementOrder* mo_assign=new AssignWorkers(strategy.upgrading_phase_1_units_assigned, id);
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
			
			ManagementOrder* mo_assign=new AssignWorkers(strategy.upgrading_phase_2_units_assigned, id);
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
			if(!dig_out_enemy(echo))
			{
				target = -1;
			}
		return;
	}
	BuildingOrder* bo = new BuildingOrder(IntBuildingType::WAR_FLAG, strategy.war_phase_war_flag_units_assigned);
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
			number_attacks=strategy.war_phase_num_attack_flags;
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
	AIEcho::Gradients::GradientInfo gi_building;
	gi_building.add_source(new Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
	gi_building.add_obstacle(new Entities::AnyRessource);
	Gradient& gradient=echo.get_gradient_manager().get_gradient(gi_building);

	if(target==-1 || !echo.player->game->teams[target]->isAlive)
	{
		std::vector<int> available_targets;		
		for(enemy_team_iterator i(echo); i!=enemy_team_iterator(); ++i)
		{
			if(echo.player->game->teams[*i]->isAlive)
			{
				enemy_building_iterator ebi(echo, *i, -1, -1, indeterminate);
				/* Make sure we know of at least one
				   building that we can directly attack
				   before committing to a particular enemy.
				   It used to be that we did not (normally)
				   need to test this, because all starting
				   buildings were known. But that was
				   cheating and has been fixed. */
				for(; ebi != enemy_building_iterator(); ++ebi)
				{
					Building* b=echo.player->game->teams[*i]->myBuildings[Building::GIDtoID(*ebi)];
					if(gradient.get_height(b->posX, b->posY) != -2)
					{
						available_targets.push_back(*i);
						break;
					}
				}
			}
		}
		if(available_targets.size()==0)
			target=-1;
		else
			target=available_targets[syncRand() % available_targets.size()];
	}
}



bool NewNicowar::dig_out_enemy(Echo& echo)
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
		return false;

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
	
	return true;
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

