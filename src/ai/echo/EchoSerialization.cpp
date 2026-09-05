// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "echo/Echo.h"
#include "Order.h"
#include <tuple>

using namespace AIEcho;
using namespace AIEcho::Construction;
using namespace AIEcho::Management;

bool Echo::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("EchoAI");
	signature_check(stream, player, versionMinor);

	stream->readEnterSection("orders");
	Uint32 ordersSize = stream->readUint32("size");
	for (Uint32 ordersIndex = 0; ordersIndex < ordersSize; ordersIndex++)
	{
		stream->readEnterSection(ordersIndex);
		size_t size=stream->readUint32("size");
		Uint8* buffer = new Uint8[size+1];
		stream->read(buffer, size+1, "data");
		orders.push_back(Order::getOrder(buffer, size+1, versionMinor));
		delete[] buffer;
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	signature_check(stream, player, versionMinor);

	br.load(stream, player, versionMinor);

	signature_check(stream, player, versionMinor);

	fm.load(stream, player, versionMinor);

	signature_check(stream, player, versionMinor);


	stream->readEnterSection("management_orders");
	Uint32 managementSize=stream->readUint32("size");
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
	Uint32 buildingSize=stream->readUint32("size");
	building_orders.resize(buildingSize);
	for(Uint32 buildingIndex = 0; buildingIndex < buildingSize; ++buildingIndex)
	{
		stream->readEnterSection(buildingIndex);
		building_orders[buildingIndex]=std::shared_ptr<BuildingOrder>(new BuildingOrder);
		building_orders[buildingIndex]->load(stream, player, versionMinor);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();


	signature_check(stream, player, versionMinor);

	stream->readEnterSection("ressource_trackers");
	Uint32 ressourceTrackerSize=stream->readUint32("size");
	for(Uint32 ressourceTrackerIndex=0; ressourceTrackerIndex<ressourceTrackerSize; ++ressourceTrackerIndex)
	{
		stream->readEnterSection(ressourceTrackerIndex);
		int id=stream->readUint32("echo_building_id");
		std::shared_ptr<RessourceTracker> rt(new RessourceTracker(*this, stream, player, versionMinor));
		bool activated=stream->readUint8("active");
		ressource_trackers[id]=std::make_tuple(rt, activated);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	signature_check(stream, player, versionMinor);

	stream->readEnterSection("starting_buildings");
	Uint32 startingBuildingSize=stream->readUint32("size");
	for(Uint32 startingBuildingIndex=0; startingBuildingIndex<startingBuildingSize; ++startingBuildingIndex)
	{
		stream->readEnterSection(startingBuildingIndex);
		starting_buildings.insert(stream->readUint32("gid"));
		stream->readLeaveSection();
	}
	stream->readLeaveSection();


	signature_check(stream, player, versionMinor);

	timer=stream->readUint32("timer");
	update_gm=stream->readUint8("update_gm");

	allies=stream->readUint32("allies");
	enemies=stream->readUint32("enemies");
	inn_view=stream->readUint32("inn_view");
	market_view=stream->readUint32("market_view");
	other_view=stream->readUint32("other_view");

	signature_check(stream, player, versionMinor);

	echoai->load(stream, player, versionMinor);


	signature_check(stream, player, versionMinor);

	stream->readLeaveSection();
	signature_check(stream, player, versionMinor);


	return true;
}



void Echo::save(GAGCore::OutputStream *stream)
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
	stream->writeUint32(ressource_trackers.size(), "size");
	Uint32 ressourceTrackerIndex=0;
	for(tracker_iterator i=ressource_trackers.begin(); i!=ressource_trackers.end(); ++ressourceTrackerIndex, ++i)
	{
		stream->writeEnterSection(ressourceTrackerIndex);
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
	stream->writeUint8(update_gm, "update_gm");

	stream->writeUint32(allies, "allies");
	stream->writeUint32(enemies, "enemies");
	stream->writeUint32(inn_view, "inn_view");
	stream->writeUint32(market_view, "market_view");
	stream->writeUint32(other_view, "other_view");

	signature_write(stream);

	echoai->save(stream);


	signature_write(stream);

	stream->writeLeaveSection();
	signature_write(stream);
}
