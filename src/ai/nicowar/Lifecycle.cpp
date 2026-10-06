// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "AITelemetryFields.h"
#include "AINicowar.h"
#include "AIStateSerialization.h"
#include "FormatableString.h"
#include <string>
#include "Utilities.h"
#include "Game.h"
#include "Unit.h"

using namespace AISharedRuntime;
using namespace AISharedRuntime::Gradients;
using namespace AISharedRuntime::Construction;
using namespace AISharedRuntime::Management;
using namespace AISharedRuntime::Conditions;
using namespace AISharedRuntime::SearchTools;



NewNicowar::NewNicowar()
{
	timer=0;
	buildings_under_construction=0;
	growth_phase=false;
	skilled_work_phase=0;
	upgrading_phase_1=false;
	upgrading_phase_2=false;
	war_preparation=false;
	war=false;
	fruit_phase=false;
	starving_recovery=false;
	no_workers_phase=false;
	can_swim=false;
	defend_explorers=false;
	explorer_attack_preparation_phase=false;
	explorer_attack_phase=false;
	starving_recovery_inns = 0;
	exploration_on_fruit=false;
	for(int n=0; n<PlacementSize; ++n)
		buildings_under_construction_per_type[n]=0;
	target=AI_NICOWAR_NO_TARGET;
	attack_flags.clear();
	is_digging_out=false;
}


bool NewNicowar::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	// Direct reloads must replace collections, rather than append to them.
	*this = NewNicowar();
	GAGCore::BinaryInputStream::CheckedReads checked(stream);
	stream->readEnterSection("NewNicowar");
	timer=stream->readUint32("timer");
	if(versionMinor >= AI_NICOWAR_SAVE_FORMAT_V59)
	{
		if(versionMinor >= AI_NICOWAR_SAVE_FORMAT_V60)
		{
			std::string strategyName = stream->readText("strategy_name");
			NicowarStrategyLoader loader;
			if (!strategyName.empty()) strategy = loader.getParticularStrategy(strategyName);
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
		war_preparation=stream->readUint8("war_preperation");
		war=stream->readUint8("war");
		fruit_phase=stream->readUint8("fruit_phase");
		starving_recovery=stream->readUint8("starving_recovery");
		no_workers_phase=stream->readUint8("no_workers_phase");
		if(versionMinor >= AI_NICOWAR_SAVE_FORMAT_V60)
			can_swim=stream->readUint8("can_swim");

		starving_recovery_inns=stream->readUint8("starving_recovery_inns");
		buildings_under_construction=stream->readUint32("buildings_under_construction");
		for(int n=0; n<PlacementSize; ++n)
		{
			buildings_under_construction_per_type[n]=stream->readUint8(FormattableString("buildings_under_construction_per_type[%0]").arg(n).c_str());
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

		if(versionMinor >= AI_NICOWAR_SAVE_FORMAT_V66)
		{
			stream->readEnterSection("defense_flags");
			size = stream->readUint16("size");
			for(size_t n = 0; n<size; ++n)
			{
				stream->readEnterSection(n);
				int flag = stream->readUint32("flag");
				defense_flags.push_back(flag);
				stream->readLeaveSection();
			}
			stream->readLeaveSection();

			stream->readEnterSection("explorer_attack_flags");
			size = stream->readUint16("size");
			for(size_t n = 0; n<size; ++n)
			{
				stream->readEnterSection(n);
				int flag = stream->readUint32("flag");
				explorer_attack_flags.push_back(flag);
				stream->readLeaveSection();
			}
			stream->readLeaveSection();
		}

		exploration_on_fruit=stream->readUint8("exploration_on_fruit");
		if (versionMinor >= AI_NICOWAR_SAVE_FORMAT_CONTINUATION)
		{
			const int defense = AIStateSerialization::readSint32(stream, "defend_explorers");
			const int preparation = AIStateSerialization::readSint32(stream, "explorer_attack_preparation_phase");
			const int attack = AIStateSerialization::readSint32(stream, "explorer_attack_phase");
			if (defense < 0 || defense > 1 || preparation < 0 || preparation > 1 || attack < 0 || attack > 1)
				return false;
			defend_explorers = defense;
			explorer_attack_preparation_phase = preparation;
			explorer_attack_phase = attack;
			starving_recovery_inns = AIStateSerialization::readSint32(stream, "starving_recovery_inns_full");
			for (int n = 0; n < PlacementSize; ++n)
			{
				stream->readEnterSection(n);
				buildings_under_construction_per_type[n] = AIStateSerialization::readSint32(stream, "construction_count_full");
				stream->readLeaveSection();
			}
		}
	}
	stream->readLeaveSection();
	return stream->isValid();
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
	stream->writeUint8(war_preparation, "war_preperation");
	stream->writeUint8(war, "war");
	stream->writeUint8(fruit_phase, "fruit_phase");
	stream->writeUint8(starving_recovery, "starving_recovery");
	stream->writeUint8(no_workers_phase, "no_workers_phase");
	stream->writeUint8(can_swim, "can_swim");
	stream->writeUint8(starving_recovery_inns, "starving_recovery_inns");
	stream->writeUint32(buildings_under_construction, "buildings_under_construction");
	for(int n=0; n<PlacementSize; ++n)
	{
		stream->writeUint8(buildings_under_construction_per_type[n], FormattableString("buildings_under_construction_per_type[%0]").arg(n).c_str());
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

	stream->writeEnterSection("defense_flags");
	stream->writeUint16(defense_flags.size(), "size");
	for(n = 0; n<defense_flags.size(); ++n)
	{
		stream->writeEnterSection(n);
		stream->writeUint32(defense_flags[n], "flag");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	stream->writeEnterSection("explorer_attack_flags");
	stream->writeUint16(explorer_attack_flags.size(), "size");
	for(n = 0; n<explorer_attack_flags.size(); ++n)
	{
		stream->writeEnterSection(n);
		stream->writeUint32(explorer_attack_flags[n], "flag");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	stream->writeUint8(exploration_on_fruit, "exploration_on_fruit");
	stream->writeSint32(defend_explorers, "defend_explorers");
	stream->writeSint32(explorer_attack_preparation_phase, "explorer_attack_preparation_phase");
	stream->writeSint32(explorer_attack_phase, "explorer_attack_phase");
	// Legacy byte fields above cannot represent all reachable queue counts.
	stream->writeSint32(starving_recovery_inns, "starving_recovery_inns_full");
	for (int n = 0; n < PlacementSize; ++n)
	{
		stream->writeEnterSection(n);
		stream->writeSint32(buildings_under_construction_per_type[n], "construction_count_full");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
}


void NewNicowar::tick(Runtime& runtime)
{
	telemetry.count(AITrace::AI5::NewNicowar_tick_calls);
	timer++;
	if(timer==AI_NICOWAR_INIT_TICK)
	{
		selectStrategy();
		check_phases(runtime);
		initialize(runtime);
	}
	if(timer%AI_NICOWAR_DECISION_CYCLE_TICKS == AI_NICOWAR_QUEUE_BUILDINGS_PHASE)
	{
		queue_buildings(runtime);
	}
	if(timer%AI_NICOWAR_DECISION_CYCLE_TICKS == AI_NICOWAR_CHECK_PHASES_PHASE)
	{
		check_phases(runtime);
	}
	if(timer%AI_NICOWAR_DECISION_CYCLE_TICKS == AI_NICOWAR_MANAGE_BUILDINGS_PHASE)
	{
		manage_buildings(runtime);
	}
	if(timer%AI_NICOWAR_DECISION_CYCLE_TICKS == AI_NICOWAR_UPGRADE_PHASE)
	{
		if (!runtime.player->game->gameHeader.isUnitUpgradesDisabled()) upgrade_buildings(runtime);
	}
	if(timer%AI_NICOWAR_DECISION_CYCLE_TICKS == AI_NICOWAR_CONTROL_ATTACKS_PHASE)
	{
		control_attacks(runtime);
	}
	if(timer%AI_NICOWAR_DECISION_CYCLE_TICKS == AI_NICOWAR_DEFENSE_FLAG_PHASE)
	{
		if (!runtime.player->game->gameHeader.isPeacefulModeEnabled()) compute_defense_flag_positioning(runtime);
	}
	if(timer%AI_NICOWAR_FARMING_INTERVAL_TICKS == 0)
	{
		update_farming(runtime);
	}
	if(timer%AI_NICOWAR_FARMING_INTERVAL_TICKS == AI_NICOWAR_FRUIT_PHASE_OFFSET)
	{
		update_fruit_flags(runtime);
	}
	if(timer%AI_NICOWAR_EXPLORER_ATTACK_INTERVAL_TICKS == AI_NICOWAR_EXPLORER_ATTACK_OFFSET)
	{
		if (!runtime.player->game->gameHeader.isPeacefulModeEnabled()) compute_explorer_flag_attack_positioning(runtime);
	}

	order_buildings(runtime);
}


void NewNicowar::handle_message(Runtime& runtime, const std::string& message)
{
	if(message.substr(0,19) == "building completed ")
	{
		int placement_num=std::stoi(message.substr(19, message.size()-1));
		buildings_under_construction-=1;
		buildings_under_construction_per_type[placement_num]-=1;
	}
	if(message.substr(0,22) == "update clearing zone1 ")
	{
		MapInfo mi(runtime);
		int id=std::stoi(message.substr(22, message.size()-1));
		Building* b = runtime.get_building_register().get_building(id);
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
		runtime.add_management_order(mo_clearing);
		runtime.add_management_order(mo_remove_clearing);
	}
	if(message.substr(0,22) == "update clearing zone2 ")
	{
		MapInfo mi(runtime);
		int id=std::stoi(message.substr(22, message.size()-1));
		Building* b = runtime.get_building_register().get_building(id);
		AddArea* mo_clearing=new AddArea(ClearingArea);
		RemoveArea* mo_remove_clearing=new RemoveArea(ClearingArea);
		mo_remove_clearing->add_condition(new BuildingDestroyed(id));
		for(int nx=-1; nx<b->type->width+1; ++nx)
		{
			for(int ny=-1; ny<b->type->height+1; ++ny)
			{
				mo_clearing->add_location(b->posX+nx, b->posY+ny);
				mo_remove_clearing->add_location(b->posX+nx, b->posY+ny);
			}
		}
		runtime.add_management_order(mo_clearing);
		runtime.add_management_order(mo_remove_clearing);
	}
 if(message.starts_with("update services ")) {
  const int id=std::stoi(message.substr(16));
  if((runtime.get_building_register().provides(id,BuildingDemand::ProduceWorker) || runtime.get_building_register().provides(id,static_cast<int>(AIPlanning::BuildingIntent::ProduceExplorer)) || runtime.get_building_register().provides(id,static_cast<int>(AIPlanning::BuildingIntent::ProduceWarrior)))) manage_swarm(runtime,id);
  if(runtime.get_building_register().provides(id,BuildingDemand::Feed)) manage_inn(runtime,id);
 }
	if(message.substr(0,13) == "update swarm ")
	{
		int id=std::stoi(message.substr(13, message.size()-1));
		manage_swarm(runtime, id);
	}
	if(message.substr(0,11) == "update inn ")
	{
		int id=std::stoi(message.substr(11, message.size()-1));
		manage_inn(runtime, id);
	}
	if(message.substr(0,16)  == "attack finished ")
	{
		int id=std::stoi(message.substr(16, message.size()-1));
		if(auto found=std::find(attack_flags.begin(),attack_flags.end(),id);found!=attack_flags.end()) attack_flags.erase(found);
	}
	if(message.substr(0,19)  == "guard flag deleted ")
	{
		int id=std::stoi(message.substr(19, message.size()-1));
		if(auto found=std::find(defense_flags.begin(),defense_flags.end(),id);found!=defense_flags.end()) defense_flags.erase(found);
	}
	if(message.substr(0,29)  == "explorer attack flag deleted ")
	{
		int id=std::stoi(message.substr(29, message.size()-1));
		if(auto found=std::find(explorer_attack_flags.begin(),explorer_attack_flags.end(),id);found!=explorer_attack_flags.end()) explorer_attack_flags.erase(found);
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



void NewNicowar::initialize(Runtime& runtime)
{
	BuildingSearch bs(runtime);
	for(building_search_iterator i = bs.begin(); i!=bs.end(); ++i)
	{
		if((runtime.get_building_register().provides(*i,BuildingDemand::ProduceWorker) || runtime.get_building_register().provides(*i,static_cast<int>(AIPlanning::BuildingIntent::ProduceExplorer)) || runtime.get_building_register().provides(*i,static_cast<int>(AIPlanning::BuildingIntent::ProduceWarrior))))
		{
			ManagementOrder* mo_tracker=new AddResourceTracker(AI_NICOWAR_RESOURCE_TRACKER_DEPTH, RecurringInputStock, *i);
			mo_tracker->add_condition(new ParticularBuilding(new NotUnderConstruction, *i));
			runtime.add_management_order(mo_tracker);
		}
		if(runtime.get_building_register().provides(*i,BuildingDemand::Feed))
		{
			ManagementOrder* mo_tracker=new AddResourceTracker(AI_NICOWAR_RESOURCE_TRACKER_DEPTH, RecurringInputStock, *i);
			mo_tracker->add_condition(new ParticularBuilding(new NotUnderConstruction, *i));
			runtime.add_management_order(mo_tracker);
		}
	}

	manage_buildings(runtime);
}

