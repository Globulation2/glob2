// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "shared_runtime/Runtime.h"
#include "Order.h"
#include <algorithm>

using namespace AISharedRuntime;
using namespace AISharedRuntime::Management;
using namespace AISharedRuntime::Conditions;
using std::shared_ptr;


bool ManagementOrder::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("ManagementOrder");
	stream->readEnterSection("conditions");
	Uint32 size = stream->readCount("size");
	conditions.resize(size);
	for(unsigned x=0; x<size; ++x)
	{
		stream->readEnterSection(x);
		conditions[x] = std::shared_ptr<Condition>(Condition::load_condition(stream, player, versionMinor));
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	stream->readLeaveSection();
	return true;
}



void ManagementOrder::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("ManagementOrder");
	stream->writeEnterSection("conditions");
	stream->writeUint32(conditions.size(), "size");
	for(unsigned x=0; x<conditions.size(); ++x)
	{
		stream->writeEnterSection(x);
		Condition::save_condition(conditions[x].get(), stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}



void ManagementOrder::add_condition(Condition* condition)
{
	conditions.push_back(std::shared_ptr<Condition>(condition));
}



tribool ManagementOrder::passes_conditions(Runtime& runtime)
{
	for(unsigned int i=0; i<conditions.size(); ++i)
	{
		tribool passes=conditions[i]->passes(runtime);
		if(passes)
			continue;
		else if(!passes)
			return false;
		else
			return indeterminate;

	}

	tribool passes=wait(runtime);
	if(passes)
		return true;
	if(!passes)
		return false;
	return indeterminate;
}



AssignWorkers::AssignWorkers(int number_of_workers, int building_id) : number_of_workers(number_of_workers), building_id(building_id)
{

}


void AssignWorkers::modify(Runtime& runtime)
{
	auto* building=runtime.get_building_register().get_building(building_id);
 int requested=number_of_workers;
 const auto& spec=building->type->semantics;
 int services=spec.feeding.enabled+spec.healing.enabled+(building->type->shootingRange>0);
 services+=std::any_of(spec.production.recipes.begin(),spec.production.recipes.end(),[](const auto& recipe){return recipe.enabled;});
 services+=std::any_of(spec.training.begin(),spec.training.end(),[](const auto& training){return training.enabled;});
 services+=spec.market.interTeamFruitExchange || spec.market.suppliesStock || spec.market.suppliesDirectStock;
 services+=building->type->zonable[WORKER] || building->type->zonable[WARRIOR] || building->type->zonable[EXPLORER];
 if(services>1 && !building->type->isBuildingSite) requested=std::max(requested,building->maxUnitWorking);
 runtime.push_order(std::make_shared<OrderModifyBuilding>(building->gid,std::clamp(requested,0,building->type->semantics.assignmentLimit)));
}



tribool AssignWorkers::wait(Runtime& runtime)
{
	return wait_for_building(runtime, building_id);
}



bool AssignWorkers::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("AssignWorkers");
	ManagementOrder::load(stream, player, versionMinor);
	number_of_workers=stream->readUint32("number_of_workers");
	building_id=stream->readUint32("building_id");
	stream->readLeaveSection();
	return true;
}



void AssignWorkers::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("AssignWorkers");
	ManagementOrder::save(stream);
	stream->writeUint32(number_of_workers, "number_of_workers");
	stream->writeUint32(building_id, "building_id");
	stream->writeLeaveSection();
}



ChangeSwarm::ChangeSwarm(int worker_ratio, int explorer_ratio, int warrior_ratio, int building_id) : worker_ratio(worker_ratio), explorer_ratio(explorer_ratio), warrior_ratio(warrior_ratio), building_id(building_id)
{

}


void ChangeSwarm::modify(Runtime& runtime)
{
	Sint32 ratio[NB_UNIT_TYPE];
	ratio[0]=worker_ratio;
	ratio[1]=explorer_ratio;
	ratio[2]=warrior_ratio;
	auto* building=runtime.get_building_register().get_building(building_id);
 for(int unit=0;unit<NB_UNIT_TYPE;++unit)
  if(!building->type->semantics.production.recipes[unit].enabled || (unit==WARRIOR && runtime.player->game->gameHeader.isPeacefulModeEnabled())) ratio[unit]=0;
 runtime.push_order(std::make_shared<OrderModifySwarm>(building->gid,ratio));
}



tribool ChangeSwarm::wait(Runtime& runtime)
{
	return wait_for_building(runtime, building_id);
}



bool ChangeSwarm::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("ChangeSwarm");
	ManagementOrder::load(stream, player, versionMinor);
	worker_ratio=stream->readUint32("worker_ratio");
	explorer_ratio=stream->readUint32("explorer_ratio");
	warrior_ratio=stream->readUint32("warrior_ratio");
	building_id=stream->readUint32("building_id");
	stream->readLeaveSection();
	return true;

}



void ChangeSwarm::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("ChangeSwarm");
	ManagementOrder::save(stream);
	stream->writeUint32(worker_ratio, "worker_ratio");
	stream->writeUint32(explorer_ratio, "explorer_ratio");
	stream->writeUint32(warrior_ratio, "warrior_ratio");
	stream->writeUint32(building_id, "building_id");
	stream->writeLeaveSection();
}



DestroyBuilding::DestroyBuilding(int building_id) : building_id(building_id)
{

}



void DestroyBuilding::modify(Runtime& runtime)
{
    if(auto* building=runtime.get_building_register().get_building(building_id))
        runtime.push_order(std::make_shared<OrderDelete>(building->gid));
}

void RetireAttraction::modify(Runtime& runtime)
{
    auto* building=runtime.get_building_register().get_building(building_id);
    if(!building) return;
    const auto& spec=building->type->semantics;
    if(spec.feeding.enabled || spec.healing.enabled || building->type->shootingRange>0
        || spec.market.interTeamFruitExchange || spec.market.suppliesStock || spec.market.suppliesDirectStock
        || std::any_of(spec.production.recipes.begin(),spec.production.recipes.end(),[](const auto& r){return r.enabled;})
        || std::any_of(spec.training.begin(),spec.training.end(),[](const auto& r){return r.enabled;})) return;
    if(spec.instantPlacement && !spec.occupiesGround) DestroyBuilding::modify(runtime);
    else runtime.push_order(std::make_shared<OrderModifyBuilding>(building->gid,0));
}

void RetireFeeding::modify(Runtime& runtime)
{
    auto* building=runtime.get_building_register().get_building(building_id);
    if(!building) return;
    const auto& index=runtime.player->game->buildingCapabilities();
    constexpr auto feeding=AIPlanning::BuildingIntent::Feed;
    // A free feeding service cannot be starved of input, and a mixed provider
    // must remain available to its other strategic consumers.
    if(!index.matches(building->typeNum,feeding) || !building->type->semantics.feeding.costMask
        || (index.intentMask(building->typeNum)&~(std::uint64_t(1)<<static_cast<unsigned>(feeding)))) return;
    DestroyBuilding::modify(runtime);
}



tribool DestroyBuilding::wait(Runtime& runtime)
{
	return wait_for_building(runtime, building_id);
}



bool DestroyBuilding::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("DestroyBuilding");
	ManagementOrder::load(stream, player, versionMinor);
	building_id=stream->readUint32("building_id");
	stream->readLeaveSection();
	return true;
}



void DestroyBuilding::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("DestroyBuilding");
	ManagementOrder::save(stream);
	stream->writeUint32(building_id, "building_id");
	stream->writeLeaveSection();
}
