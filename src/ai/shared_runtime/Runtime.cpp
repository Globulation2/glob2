// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "Material.h"
#include "AITelemetryFields.h"
#include "shared_runtime/Runtime.h"
#include "Building.h"
#include <map>
#include "shared_runtime/BuildingDemands.h"
#include "Game.h"
#include "AIRules.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "AIRuleOrders.h"
#include "Version.h"
#include <tuple>

using namespace AISharedRuntime;
using namespace AISharedRuntime::Gradients;
using namespace AISharedRuntime::Management;
using namespace AISharedRuntime::Conditions;
using namespace AISharedRuntime::SearchTools;
using std::shared_ptr;



bool Runtime::ensure_production(const std::array<int,3>& desired,int workers,int futureWorkers)
{
    OwnerObservationScope scope(*this);
    std::vector<int> pending;
    for(const auto& order:building_orders) pending.push_back(order->get_concrete_type());
    for(const auto& [id,record]:br.pending_buildings) pending.push_back(std::get<2>(record));
    auto order=AIEngine::ObservationQueries::missingProductionOrder(observation(),teamNumber(),desired,workers,futureWorkers,pending);
    if(!order) return false;
    const auto& create=static_cast<const OrderCreate&>(*order);
    const int id=br.register_building();br.issue_order(id,create.posX,create.posY,create.typeNum);
    push_order(order);
    auto* ratios=new ChangeSwarm(desired[WORKER],desired[EXPLORER],desired[WARRIOR],id);
    ratios->add_condition(new ParticularBuilding(new NotUnderConstruction,id));add_management_order(ratios);
    auto* tracker=new AddMaterialTracker(AI_SHARED_RUNTIME_RTI_TRACKER_LENGTH,RecurringInputStock,id);
    tracker->add_condition(new ParticularBuilding(new NotUnderConstruction,id));add_management_order(tracker);
    return true;
}

void AISharedRuntime::signature_write(GAGCore::OutputStream *stream)
{
	// This marker is part of existing save files; renaming it would break loading.
	stream->write("EchoSig", AI_SHARED_RUNTIME_SIGNATURE_LENGTH, "signature");
}



void AISharedRuntime::signature_check(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	char signature[AI_SHARED_RUNTIME_SIGNATURE_LENGTH];
	stream->read(signature, AI_SHARED_RUNTIME_SIGNATURE_LENGTH, "signature");
	if (memcmp(signature,"EchoSig", AI_SHARED_RUNTIME_SIGNATURE_LENGTH)!=0)
	{

		throw std::runtime_error("Invalid saved AI signature");
	}
}




Runtime::Runtime(RuntimeAI* runtimeai, Player* player) : player(player), runtimeai(runtimeai), gm(), br(player, *this), fm(*this), timer(0)
{
	previous_building_id=-1;
	from_load_timer=0;
	is_fruit=false;
}


unsigned int Runtime::add_building_order(Construction::BuildingOrder* bo)
{
	building_orders.push_back(std::shared_ptr<Construction::BuildingOrder>(bo));
	telemetry.count(telemetry.series && telemetry.series->implementation == 4
						? AITrace::AI4::shared_runtime_building_queued
						: AITrace::AI5::shared_runtime_building_queued);
	bo->queue_gradients(get_gradient_manager());
	unsigned int id=br.register_building();
	bo->id=id;
    const auto intent=buildingIntent(bo->get_building_type());
    if(intent==AIPlanning::BuildingIntent::AttractWarriors) begin_attraction(id,1u<<WARRIOR);
    if(intent==AIPlanning::BuildingIntent::AttractExplorers) begin_attraction(id,1u<<EXPLORER);
    if(intent==AIPlanning::BuildingIntent::AttractWorkers || intent==AIPlanning::BuildingIntent::ClearResources) begin_attraction(id,1u<<WORKER);
	return id;
}


bool Runtime::begin_attraction(int id,unsigned unitMask)
{
    auto found=retired_attractions.find(id);
    if(found==retired_attractions.end() || !(found->second&unitMask)) return false;
    found->second&=~unitMask;
    if(!found->second) retired_attractions.erase(found);
    return true;
}

unsigned Runtime::complete_attraction_retirement(int buildingId,unsigned unitMask)
{
    return retired_attractions[buildingId]|=unitMask;
}

bool Runtime::attraction_retired_or_destroyed(int buildingId,unsigned unitMask) const
{
    const auto found=retired_attractions.find(buildingId);
    if(found!=retired_attractions.end() && (found->second&unitMask)==unitMask) return true;
    return !br.is_building_found(buildingId) && !br.is_building_pending(buildingId);
}

void Runtime::add_management_order(Management::ManagementOrder* mo)
{
	management_orders.push_back(std::shared_ptr<Management::ManagementOrder>(mo));
	telemetry.count(telemetry.series && telemetry.series->implementation == 4
						? AITrace::AI4::shared_runtime_management_queued
						: AITrace::AI5::shared_runtime_management_queued);
}


void Runtime::update_management_orders()
{
	for(std::vector<std::shared_ptr<Management::ManagementOrder> >::iterator i=management_orders.begin(); i!=management_orders.end();)
	{
		tribool passes=(*i)->passes_conditions(*this);
		if(passes)
		{
			size_t pos = i - management_orders.begin();
			(*i)->modify(*this);
			telemetry.count(telemetry.series && telemetry.series->implementation == 4
								? AITrace::AI4::shared_runtime_management_applied
								: AITrace::AI5::shared_runtime_management_applied);
			management_orders.erase(management_orders.begin() + pos);
			i = management_orders.begin() + pos;
			continue;
		}
		else if(!passes)
		{
		}
		else
		{
			size_t pos = i - management_orders.begin();
			telemetry.count(telemetry.series && telemetry.series->implementation == 4
								? AITrace::AI4::shared_runtime_management_invalid
								: AITrace::AI5::shared_runtime_management_invalid);
			management_orders.erase(i);
			i = management_orders.begin() + pos;
			continue;
		}
		++i;
	}
}



void Runtime::add_material_tracker(Management::MaterialTracker* rt, int building_id)
{
	material_trackers[building_id]=std::make_tuple(std::shared_ptr<MaterialTracker>(rt), true);
}



std::shared_ptr<Management::MaterialTracker> Runtime::get_material_tracker(int building_id)
{
	if(material_trackers.find(building_id)==material_trackers.end())
		return std::shared_ptr<Management::MaterialTracker>();
	return std::get<0>(material_trackers[building_id]);
}



void Runtime::pause_material_tracker(int building_id)
{
	std::get<1>(material_trackers[building_id])=false;
}



void Runtime::unpause_material_tracker(int building_id)
{
	std::get<1>(material_trackers[building_id])=true;
}



void Runtime::update_material_trackers()
{
	for(std::map<int, std::tuple<std::shared_ptr<Management::MaterialTracker>, bool> >::iterator i = material_trackers.begin(); i!=material_trackers.end();)
	{
		if(!br.is_building_found(i->first) && !br.is_building_pending(i->first))
		{
			std::map<int, std::tuple<std::shared_ptr<Management::MaterialTracker>, bool> >::iterator current=i;
			++i;
			material_trackers.erase(current);
			continue;
		}
		else if(br.is_building_found(i->first))
		{
			if(std::get<1>(i->second))
				std::get<0>(i->second)->tick();
		}
		++i;
	}
}



void Runtime::update_building_orders()
{
	for(std::vector<std::shared_ptr<Construction::BuildingOrder> >::iterator i=building_orders.begin(); i!=building_orders.end();)
	{
		// A restored placement owns a register entry even before a building exists.
		// Release both pieces before evaluating prerequisites that can never pass.
		if (!(*i)->bind(*this))
		{ br.remove_building((*i)->id); i=building_orders.erase(i); continue; }
		tribool passes=(*i)->passes_conditions(*this);
		if(passes)
		{
			if(!(previous_building_id==-1 || br.is_building_found(previous_building_id) || !br.is_building_pending(previous_building_id)))
				break;
			position p=(*i)->find_location(*this, observation(), *gm);
			if(p.x >= 0 && p.y >= 0)
			{
    const int type=(*i)->get_concrete_type();
    br.issue_order((*i)->id,p.x,p.y,type);
    ManagementOrder* staffing=new AssignWorkers((*i)->get_number_of_workers(),(*i)->id);
    if(observation().catalog->at(type).site)
     staffing->add_condition(new ParticularBuilding(new UnderConstruction,(*i)->id));
    add_management_order(staffing);
				push_order(AIEngine::ObservationQueries::createOrder(observation(),teamNumber(),p.x,p.y,type,1,1));
				telemetry.count(telemetry.series && telemetry.series->implementation == 4
									? AITrace::AI4::shared_runtime_building_emitted
									: AITrace::AI5::shared_runtime_building_emitted);
				previous_building_id=(*i)->id;
				i=building_orders.erase(i);
				break;
			}
			else
			{
				br.remove_building((*i)->id);
				i=building_orders.erase(i);
				continue;
			}
		}
		else if(!passes)
		{
		}
		else
		{
			br.remove_building((*i)->id);
			i=building_orders.erase(i);
			continue;
		}
		++i;
	}
}



void Runtime::init_starting_buildings()
{
	for(int t=0; t<Team::MAX_COUNT; ++t)
	{
		if(std::size_t(t)<observation().teams.size())
		{
			for(int bu=0; bu<Building::MAX_COUNT; ++bu)
			{
				auto* b=observation().buildingSlots(t)[bu];
				if(b)
				{
					starting_buildings.insert(b->gid);
				}
			}
		}
	}
}

void Runtime::check_fruit()
{
	MapInfo mi(*this);
	for(int x=0; x<mi.get_width(); ++x)
	{
		for(int y=0; y<mi.get_height(); ++y)
		{
			if(mi.is_resource(x, y, materialIndex(MaterialId::Cherries)))
				is_fruit=true;
			if(mi.is_resource(x, y, materialIndex(MaterialId::Oranges)))
				is_fruit=true;
			if(mi.is_resource(x, y, materialIndex(MaterialId::Prunes)))
				is_fruit=true;
			if(is_fruit)
				return;
		}
	}
}


std::shared_ptr<Order> Runtime::decide()
{
	if(!gm)
		gm=std::make_unique<GradientManager>(observation());
    gm->bindWorld(observation());

	if(from_load_timer==0)
	{
		check_fruit();
	}

	if(timer==0)
	{
		br.initiate();
		init_starting_buildings();
		allies=observedTeam().allies;
		enemies=observedTeam().enemies;
		market_view=observedTeam().exchangeVision;
		inn_view=observedTeam().foodVision;
		other_view=observedTeam().otherVision;
	}

	while (!orders.empty() && (!AIEngine::selectedTargetExists(*orders.front(),observation())
        || !AIEngine::ObservationQueries::permittedQueuedOrder(observation(),*orders.front()))) {
        orderExecutionCompleted(*orders.front(),false);
        orders.erase(orders.begin());
    }
	if(!orders.empty())
	{
		std::shared_ptr<Order> order=orders.front();
		orders.erase(orders.begin());
		return order;
	}
	gm->update();
	br.tick();
    std::erase_if(retired_attractions,[&](const auto& entry){return !br.is_building_found(entry.first) && !br.is_building_pending(entry.first);});
	update_material_trackers();
	update_management_orders();
	runtimeai->telemetry = telemetry;
	runtimeai->tick(*this);
	update_management_orders();
	update_building_orders();
	timer++;
	from_load_timer++;
	return std::shared_ptr<Order>(new NullOrder());
}

void Runtime::orderExecutionCompleted(const Order& order, bool accepted)
{
    if (const auto* construction=dynamic_cast<const OrderConstruction*>(&order))
        br.order_execution_completed(construction->gid,accepted,
            order.aiSelectedTarget ? std::optional<Uint32>(order.aiSelectedTarget->generation) : std::nullopt);
}

void Runtime::releaseObservation()
{
    if(gm) gm->unbindWorld();
    currentObservation.reset();
}
Runtime::OwnerObservationScope::OwnerObservationScope(Runtime& value)
    : runtime(value), active(!value.deciding)
{
    if(active) ++runtime.ownerObservationDepth;
}
Runtime::OwnerObservationScope::~OwnerObservationScope()
{
    if(active && --runtime.ownerObservationDepth==0 && !runtime.deciding)
        runtime.releaseObservation();
}
void Runtime::refreshOwnerObservation()
{
    if(deciding) return;
    if(!ownerObservationDepth) throw std::logic_error("Runtime owner observation requires a scoped borrow");
    releaseObservation();
    observation();
}
const AIEngine::AIWorldView& Runtime::observation()
{
    if(!currentObservation) {
        if(deciding || !ownerObservationDepth)
            throw std::logic_error("Runtime query requires a decision or owner observation scope");
        observationCatalog=AIEngine::AIWorldView::captureCatalog(*player->game);
        currentObservation=AIEngine::AIWorldView::capture(*player->game,observationCatalog);
        observedPlayerNumber=player->number;observedTeamNumber=player->team->teamNumber;
    }
    return *currentObservation;
}
std::shared_ptr<Order> Runtime::getOrder()
{
    if(!observationCatalog) observationCatalog=AIEngine::AIWorldView::captureCatalog(*player->game);
    auto view=AIEngine::AIWorldView::capture(*player->game,observationCatalog);
    const std::vector<AIEngine::ExecutionReceipt> receipts;
    return getOrder(AIEngine::DecisionContext{*view,unsigned(player->number),unsigned(player->team->teamNumber),receipts,view});
}
std::shared_ptr<Order> Runtime::getOrder(const AIEngine::DecisionContext& context)
{
    // Cached field values and ages persist; the engine lease is borrowed only
    // for this invocation and released on every exit.
    deciding=true;
    const auto release=[this] {
        deciding=false;
        releaseObservation();
    };
    try {
    observedPlayerNumber=context.player;observedTeamNumber=context.team;
    currentObservation=context.observation ? context.observation : std::make_shared<AIEngine::AIWorldView>(context.world.components());
    observationCatalog=context.world.catalog;
    for(const auto& receipt:context.receipts) {
        if(receipt.command.empty()) continue;
        auto order=Order::getOrder(receipt.command.data(),receipt.command.size(),VERSION_MINOR);
        if(order) {
            order->aiSelectedTarget=receipt.selectedTarget;
            orderExecutionCompleted(*order,receipt.status==AIEngine::ExecutionStatus::Accepted);
        }
    }
        auto order=decide();release();return order;
    }
    catch(...) {release();throw;}
}

std::optional<Uint64> AISharedRuntime::Runtime::retainedQueryVectorBytes() const
{
    return (gm ? gm->retainedVectorBytes() : 0) + fm.flagmap.capacity() * sizeof(int)
        + (runtimeai ? runtimeai->retainedQueryVectorBytes() : 0);
}
