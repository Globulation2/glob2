// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "shared_runtime/Runtime.h"
#include "Order.h"
#include "Game.h"
#include "FileFormatVersions.h"
#include "AIStateSerialization.h"
#include <tuple>

using namespace AISharedRuntime;
using namespace AISharedRuntime::Construction;
using namespace AISharedRuntime::Management;

bool Runtime::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	GAGCore::BinaryInputStream::CheckedReads checked(stream);
	this->player=player;
    readWorld.reset();currentObservation.reset();observationCatalog.reset();readCatalog.reset();
	gm.reset();
    // Loading helpers read restored map metadata through one owner-scoped borrow.
    // The borrow is released on success, early return and exceptions.
    OwnerObservationScope observationScope(*this);
	orders.clear();
	management_orders.clear();
	building_orders.clear();
	resource_trackers.clear();
	starting_buildings.clear();
    retired_attractions.clear();
	previous_building_id=-1;
	from_load_timer=0;
	is_fruit=false;
	stream->readEnterSection("EchoAI");
	signature_check(stream, player, versionMinor);

	stream->readEnterSection("orders");
	Uint32 ordersSize = stream->readCount("size");
	for (Uint32 ordersIndex = 0; ordersIndex < ordersSize; ordersIndex++)
	{
		stream->readEnterSection(ordersIndex);
		size_t size=stream->readCount("size");
		std::vector<Uint8> buffer(size+1);
		if (dynamic_cast<GAGCore::TextInputStream *>(stream)) {
			buffer[0]=stream->readUint8("type");
			stream->read(buffer.data()+1,size,"data");
		} else stream->read(buffer.data(),buffer.size(),"data");
		auto order = Order::getOrder(buffer.data(), buffer.size(), versionMinor);
		if (!order) return false;
		AIStateSerialization::normalizeLegacyOrderStaffing(*player->game,*order,versionMinor);
        AIEngine::loadSelectedTarget(*stream,*order,versionMinor);
		orders.push_back(order);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	signature_check(stream, player, versionMinor);

	if (!br.load(stream, player, versionMinor)) return false;

	signature_check(stream, player, versionMinor);

	if (!fm.load(stream, player, versionMinor)) return false;

	signature_check(stream, player, versionMinor);


	stream->readEnterSection("management_orders");
	Uint32 managementSize=stream->readCount("size");
	for(Uint32 managementIndex = 0; managementIndex < managementSize; ++managementIndex)
	{
		stream->readEnterSection(managementIndex);
		signature_check(stream, player, versionMinor);
		signature_check(stream, player, versionMinor);
		std::shared_ptr<ManagementOrder> mo=std::shared_ptr<ManagementOrder>(ManagementOrder::load_order(stream, player, versionMinor));
		management_orders.push_back(mo);
		signature_check(stream, player, versionMinor);
		signature_check(stream, player, versionMinor);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	signature_check(stream, player, versionMinor);

	stream->readEnterSection("building_orders");
	Uint32 buildingSize=stream->readCount("size");
	building_orders.resize(buildingSize);
	for(Uint32 buildingIndex = 0; buildingIndex < buildingSize; ++buildingIndex)
	{
		stream->readEnterSection(buildingIndex);
		building_orders[buildingIndex]=std::shared_ptr<BuildingOrder>(new BuildingOrder);
		building_orders[buildingIndex]->load(stream, player, versionMinor);
		// A save from before the id was serialised leaves it at -1. Hand out a
		// fresh registration rather than a sentinel: the id is used as a
		// BuildingRegister map key and passed to AssignWorkers, so it has to be
		// a real one. br is already loaded at this point.
		if (building_orders[buildingIndex]->id < 0)
			building_orders[buildingIndex]->id = static_cast<int>(br.register_building());
		stream->readLeaveSection();
	}
	stream->readLeaveSection();


	signature_check(stream, player, versionMinor);

	stream->readEnterSection("ressource_trackers");
	Uint32 resourceTrackerSize=stream->readCount("size");
	for(Uint32 resourceTrackerIndex=0; resourceTrackerIndex<resourceTrackerSize; ++resourceTrackerIndex)
	{
		stream->readEnterSection(resourceTrackerIndex);
		int id=stream->readUint32("echo_building_id");
		std::shared_ptr<ResourceTracker> rt(new ResourceTracker(*this, stream, player, versionMinor));
		bool activated=stream->readUint8("active");
		resource_trackers[id]=std::make_tuple(rt, activated);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	signature_check(stream, player, versionMinor);

	stream->readEnterSection("starting_buildings");
	Uint32 startingBuildingSize=stream->readCount("size");
	for(Uint32 startingBuildingIndex=0; startingBuildingIndex<startingBuildingSize; ++startingBuildingIndex)
	{
		stream->readEnterSection(startingBuildingIndex);
		starting_buildings.insert(stream->readUint32("gid"));
		stream->readLeaveSection();
	}
	stream->readLeaveSection();


	signature_check(stream, player, versionMinor);

	timer=stream->readUint32("timer");
	if(versionMinor<FILE_FORMAT_VERSION_SHARED_RUNTIME_PRIVATE_GRADIENTS)
		stream->readUint8("update_gm");

	allies=stream->readUint32("allies");
	enemies=stream->readUint32("enemies");
	inn_view=stream->readUint32("inn_view");
	market_view=stream->readUint32("market_view");
	other_view=stream->readUint32("other_view");

	signature_check(stream, player, versionMinor);

	if (!runtimeai->load(stream, player, versionMinor)) return false;


	signature_check(stream, player, versionMinor);

	if(versionMinor>=FILE_FORMAT_VERSION_SHARED_RUNTIME_CONTINUATION)
	{
		stream->readEnterSection("continuation");
		previous_building_id=stream->readSint32("previousBuildingId");
		from_load_timer=stream->readSint32("fromLoadTimer");
		is_fruit=stream->readUint8("isFruit")!=0;
		if(versionMinor>=FILE_FORMAT_VERSION_SHARED_RUNTIME_PRIVATE_GRADIENTS)
		{
			const Uint8 hasManager=stream->readUint8("hasGradientManager");
			if(hasManager>1) return false;
			if(hasManager)
			{
				gm=std::make_unique<Gradients::GradientManager>(player->map);
				if(!gm->load(stream,player,versionMinor)) return false;
			}
		}
		else
		{
			const int owner=stream->readSint32("gradientOwner");
			if(owner < -1 || owner > player->number) return false;
			if(owner==player->number)
			{
				gm=std::make_unique<Gradients::GradientManager>(player->map);
				if(!gm->load(stream,player,versionMinor)) return false;
			}
			else if(owner>=0)
			{
				// Older saves stored one mutable manager for multiple controllers.
				// Restore its full scheduling state, then give this AI a private copy.
				Player* prior=player->team->game->players[owner];
				Runtime* runtime=prior && prior->type>=BasePlayer::P_AI && prior->ai ? dynamic_cast<Runtime*>(prior->ai->aiImplementation) : nullptr;
				if(!runtime || !runtime->gm) return false;
				gm=runtime->gm->clone();
			}
		}
        if(versionMinor>=FILE_FORMAT_VERSION_BUILDING_CATALOG) {
            stream->readEnterSection("retiredAttractions");
            const auto count=stream->readCount("size");
            for(Uint32 i=0;i<count;++i) {
                stream->readEnterSection(i);
                const int id=stream->readSint32("id");
                const unsigned mask=stream->readUint8("unitMask");
                if(id<0 || !mask || (mask&~((1u<<NB_UNIT_TYPE)-1)) || !retired_attractions.emplace(id,mask).second) return false;
                stream->readLeaveSection();
            }
            stream->readLeaveSection();
        }
		stream->readLeaveSection();
	}

	stream->readLeaveSection();
	signature_check(stream, player, versionMinor);


	return true;
}



void Runtime::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("EchoAI");

	signature_write(stream);

	stream->writeEnterSection("orders");
	stream->writeUint32((Uint32)orders.size(), "size");
	Uint32 ordersIndex = 0;
	for (std::list<std::shared_ptr<Order> >::iterator i = orders.begin(); i!=orders.end(); ++i)
	{
		stream->writeEnterSection(ordersIndex);
		stream->writeUint32((*i)->getDataLength(), "size");
		///one byte indicating the type is required to be written for order.
		stream->writeUint8((*i)->getOrderType(), "type");
		stream->write((*i)->getData(), (*i)->getDataLength(), "data");
        AIEngine::saveSelectedTarget(*stream,**i);
		stream->writeLeaveSection();
		ordersIndex++;
	}
	stream->writeLeaveSection();

	signature_write(stream);

	br.save(stream);

	signature_write(stream);

	fm.save(stream);

	signature_write(stream);


	stream->writeEnterSection("management_orders");
	stream->writeUint32(management_orders.size(), "size");
	for(Uint32 managementIndex = 0; managementIndex < management_orders.size(); ++managementIndex)
	{
		stream->writeEnterSection(managementIndex);
		signature_write(stream);
		signature_write(stream);
		Management::ManagementOrder::save_order(management_orders[managementIndex].get(), stream);
		signature_write(stream);
		signature_write(stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	signature_write(stream);

	stream->writeEnterSection("building_orders");
	stream->writeUint32(building_orders.size(), "size");
	for(Uint32 buildingIndex = 0; buildingIndex < building_orders.size(); ++buildingIndex)
	{
		stream->writeEnterSection(buildingIndex);
		building_orders[buildingIndex]->save(stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	signature_write(stream);

	stream->writeEnterSection("ressource_trackers");
	stream->writeUint32(resource_trackers.size(), "size");
	Uint32 resourceTrackerIndex=0;
	for(tracker_iterator i=resource_trackers.begin(); i!=resource_trackers.end(); ++resourceTrackerIndex, ++i)
	{
		stream->writeEnterSection(resourceTrackerIndex);
		stream->writeUint32(i->first, "echo_building_id");
		std::get<0>(i->second)->save(stream);
		stream->writeUint8(std::get<1>(i->second), "active");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	signature_write(stream);

	stream->writeEnterSection("starting_buildings");
	Uint32 startingBuildingIndex=0;
	stream->writeUint32(starting_buildings.size(), "size");
	for(std::set<int>::iterator i=starting_buildings.begin(); i!=starting_buildings.end(); ++i, ++startingBuildingIndex)
	{
		stream->writeEnterSection(startingBuildingIndex);
		stream->writeUint32(*i, "gid");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	signature_write(stream);

	stream->writeUint32(timer, "timer");

	stream->writeUint32(allies, "allies");
	stream->writeUint32(enemies, "enemies");
	stream->writeUint32(inn_view, "inn_view");
	stream->writeUint32(market_view, "market_view");
	stream->writeUint32(other_view, "other_view");

	signature_write(stream);

	runtimeai->save(stream);


	signature_write(stream);

	stream->writeEnterSection("continuation");
	stream->writeSint32(previous_building_id,"previousBuildingId");
	stream->writeSint32(from_load_timer,"fromLoadTimer");
	stream->writeUint8(is_fruit,"isFruit");
	stream->writeUint8(gm != nullptr,"hasGradientManager");
	if(gm) {
        // Save runs on the simulation owner after the ordered worker stream is
        // drained. Only refresh the validity comparison, never the field.
        auto observed=AIEngine::AIWorldView::capture(*player->game,observationCatalog ? observationCatalog : AIEngine::AIWorldView::captureCatalog(*player->game));
        gm->bindWorld(*observed);
        try {gm->save(stream);gm->unbindWorld();}
        catch(...) {gm->unbindWorld();throw;}
    }
    stream->writeEnterSection("retiredAttractions");
    stream->writeUint32(retired_attractions.size(),"size");
    Uint32 retiredIndex=0;
    for(const auto& [id,mask]:retired_attractions) {
        stream->writeEnterSection(retiredIndex++);
        stream->writeSint32(id,"id");stream->writeUint8(mask,"unitMask");
        stream->writeLeaveSection();
    }
    stream->writeLeaveSection();
	stream->writeLeaveSection();

	stream->writeLeaveSection();
	signature_write(stream);
}
