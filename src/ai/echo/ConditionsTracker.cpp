// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "echo/Echo.h"

using namespace AIEcho;
using namespace AIEcho::Conditions;


ResourceTrackerAmount::ResourceTrackerAmount(int amount, TrackerMethod tracker_method) : amount(amount), tracker_method(tracker_method)
{

}



bool ResourceTrackerAmount::passes(Echo& echo, int id)
{
	if(tracker_method==Greater)
	{
		return echo.get_resource_tracker(id)->get_total_level() > amount;
	}
	else if(tracker_method==Lesser)
	{
		return echo.get_resource_tracker(id)->get_total_level() < amount;
	}
	return false;
}



BuildingConditionType ResourceTrackerAmount::get_type()
{
	return CResourceTrackerAmount;
}



bool ResourceTrackerAmount::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("RessourceTrackerAmount");
	amount=stream->readUint32("amount");
	tracker_method=static_cast<TrackerMethod>(stream->readUint32("tracker_method"));
	stream->readLeaveSection();
	return true;
}



void ResourceTrackerAmount::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("RessourceTrackerAmount");
	stream->writeUint32(amount, "amount");
	stream->writeUint32(static_cast<Uint32>(tracker_method), "tracker_method");
	stream->writeLeaveSection();
}



ResourceTrackerAge::ResourceTrackerAge(int age, TrackerMethod tracker_method) : age(age), tracker_method(tracker_method)
{

}



bool ResourceTrackerAge::passes(Echo& echo, int id)
{
	if(tracker_method==Greater)
	{
		return echo.get_resource_tracker(id)->get_age() > age;
	}
	else if(tracker_method==Lesser)
	{
		return echo.get_resource_tracker(id)->get_age() < age;
	}
	return false;
}



BuildingConditionType ResourceTrackerAge::get_type()
{
	return CResourceTrackerAge;
}



bool ResourceTrackerAge::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("RessourceTrackerAge");
	age=stream->readUint32("age");
	tracker_method=static_cast<TrackerMethod>(stream->readUint32("tracker_method"));
	stream->readLeaveSection();
	return true;
}



void ResourceTrackerAge::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("RessourceTrackerAge");
	stream->writeUint32(age, "age");
	stream->writeUint32(static_cast<Uint32>(tracker_method), "tracker_method");
	stream->writeLeaveSection();
}
