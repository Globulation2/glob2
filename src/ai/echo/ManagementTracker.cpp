// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "echo/Echo.h"
#include "Building.h"

using namespace AIEcho;
using namespace AIEcho::Management;
using namespace boost::logic;


ResourceTracker::ResourceTracker(Echo& echo, int building_id, int length, int resource) : record(length, 0), position(0), timer(0), length(length), echo(echo), building_id(building_id), resource(resource)
{

}



void ResourceTracker::tick()
{
	timer++;
	if((timer%AI_ECHO_TRACKER_SAMPLE_INTERVAL_TICKS)==0)
	{
		Building* b = echo.get_building_register().get_building(building_id);
		record[position]=b->resources[resource];
		position++;
		if(position>=record.size())
			position=0;
	}
}


int ResourceTracker::get_total_level()
{
	int sum=0;
	for(unsigned int n=0; n<record.size(); ++n)
	{
		sum+=record[n];
	}
	return sum;
}



bool ResourceTracker::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("RessourceTracker");
	stream->readEnterSection("record");
	Uint32 recordsize=stream->readUint32("size");
	record.resize(recordsize);
	for(unsigned int record_index=0; record_index<recordsize; ++record_index)
	{
		stream->readEnterSection(record_index);
		record[record_index]=stream->readUint32("quantity_of_ressources");
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	position=stream->readUint32("position");
	timer=stream->readUint32("timer");
	building_id=stream->readUint32("building_id");
	length=stream->readUint32("length");
	resource=stream->readUint32("ressource");
	stream->readLeaveSection();
	return true;
}



void ResourceTracker::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("RessourceTracker");
	stream->writeEnterSection("record");
	stream->writeUint32(record.size(), "size");
	for(unsigned int record_index=0; record_index<record.size(); ++record_index)
	{
		stream->writeEnterSection(record_index);
		stream->writeUint32(record[record_index], "quantity_of_ressources");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeUint32(position, "position");
	stream->writeUint32(timer, "timer");
	stream->writeUint32(building_id, "building_id");
	stream->writeUint32(length, "length");
	stream->writeUint32(resource, "ressource");
	stream->writeLeaveSection();
}



AddResourceTracker::AddResourceTracker(int length, int resource, int building_id) : length(length), building_id(building_id), resource(resource)
{

}



void AddResourceTracker::modify(Echo& echo)
{
	echo.add_resource_tracker(new ResourceTracker(echo, building_id, length, resource), building_id);
}



boost::logic::tribool AddResourceTracker::wait(Echo& echo)
{
	return wait_for_building(echo, building_id);
}



bool AddResourceTracker::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("AddRessourceTracker");
	ManagementOrder::load(stream, player, versionMinor);
	length=stream->readUint32("length");
	building_id=stream->readUint32("building_id");
	resource=stream->readUint32("ressource");
	stream->readLeaveSection();
	return true;
}



void AddResourceTracker::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("AddRessourceTracker");
	ManagementOrder::save(stream);
	stream->writeUint32(length, "length");
	stream->writeUint32(building_id, "building_id");
	stream->writeUint32(resource, "ressource");
	stream->writeLeaveSection();
}



PauseResourceTracker::PauseResourceTracker(int building_id) : building_id(building_id)
{

}



void PauseResourceTracker::modify(Echo& echo)
{
	echo.pause_resource_tracker(building_id);
}



boost::logic::tribool PauseResourceTracker::wait(Echo& echo)
{
	return wait_for_building(echo, building_id);
}



bool PauseResourceTracker::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("PauseRessourceTracker");
	ManagementOrder::load(stream, player, versionMinor);
	building_id=stream->readUint32("building_id");
	stream->readLeaveSection();
	return true;
}



void PauseResourceTracker::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("PauseRessourceTracker");
	ManagementOrder::save(stream);
	stream->writeUint32(building_id, "building_id");
	stream->writeLeaveSection();
}



UnPauseResourceTracker::UnPauseResourceTracker(int building_id) : building_id(building_id)
{

}



void UnPauseResourceTracker::modify(Echo& echo)
{
	echo.unpause_resource_tracker(building_id);
}



boost::logic::tribool UnPauseResourceTracker::wait(Echo& echo)
{
	return wait_for_building(echo, building_id);
}



bool UnPauseResourceTracker::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("UnPauseRessourceTracker");
	ManagementOrder::load(stream, player, versionMinor);
	building_id=stream->readUint32("building_id");
	stream->readLeaveSection();
	return true;
}



void UnPauseResourceTracker::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("UnPauseRessourceTracker");
	ManagementOrder::save(stream);
	stream->writeUint32(building_id, "building_id");
	stream->writeLeaveSection();
}


