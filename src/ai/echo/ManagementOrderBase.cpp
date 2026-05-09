// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "echo/Echo.h"
#include "Order.h"

using namespace AIEcho;
using namespace AIEcho::Management;
using namespace AIEcho::Conditions;
using namespace boost::logic;
using std::shared_ptr;


bool ManagementOrder::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("ManagementOrder");
	stream->readEnterSection("conditions");
	Uint32 size = stream->readUint32("size");
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



boost::logic::tribool ManagementOrder::passes_conditions(Echo& echo)
{
	for(unsigned int i=0; i<conditions.size(); ++i)
	{
		boost::logic::tribool passes=conditions[i]->passes(echo);
		if(passes)
			continue;
		else if(!passes)
			return false;
		else
			return indeterminate;

	}

	boost::logic::tribool passes=wait(echo);
	if(passes)
		return true;
	if(!passes)
		return false;
	return indeterminate;
}



AssignWorkers::AssignWorkers(int number_of_workers, int building_id) : number_of_workers(number_of_workers), building_id(building_id)
{

}


void AssignWorkers::modify(Echo& echo)
{
	echo.push_order(shared_ptr<Order>(new OrderModifyBuilding(echo.get_building_register().get_building(building_id)->gid, number_of_workers)));
}



boost::logic::tribool AssignWorkers::wait(Echo& echo)
{
	return wait_for_building(echo, building_id);
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


void ChangeSwarm::modify(Echo& echo)
{
	Sint32 ratio[NB_UNIT_TYPE];
	ratio[0]=worker_ratio;
	ratio[1]=explorer_ratio;
	ratio[2]=warrior_ratio;
	echo.push_order(shared_ptr<Order>(new OrderModifySwarm(echo.get_building_register().get_building(building_id)->gid, ratio)));
}



boost::logic::tribool ChangeSwarm::wait(Echo& echo)
{
	return wait_for_building(echo, building_id);
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



void DestroyBuilding::modify(Echo& echo)
{
	echo.push_order(shared_ptr<Order>(new OrderDelete(echo.get_building_register().get_building(building_id)->gid)));
}



boost::logic::tribool DestroyBuilding::wait(Echo& echo)
{
	return wait_for_building(echo, building_id);
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


