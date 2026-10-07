// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "shared_runtime/Runtime.h"

using namespace AISharedRuntime;
using namespace AISharedRuntime::Conditions;


MaterialTrackerAmount::MaterialTrackerAmount(int amount, TrackerMethod tracker_method) : amount(amount), tracker_method(tracker_method)
{

}



bool MaterialTrackerAmount::passes(Runtime& runtime, int id)
{
	if(tracker_method==Greater)
	{
		return runtime.get_material_tracker(id)->get_total_level() > amount;
	}
	else if(tracker_method==Lesser)
	{
		return runtime.get_material_tracker(id)->get_total_level() < amount;
	}
	return false;
}



BuildingConditionType MaterialTrackerAmount::get_type()
{
	return CMaterialTrackerAmount;
}



bool MaterialTrackerAmount::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("RessourceTrackerAmount");
	amount=stream->readUint32("amount");
	tracker_method=static_cast<TrackerMethod>(stream->readUint32("tracker_method"));
	stream->readLeaveSection();
	return true;
}



void MaterialTrackerAmount::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("RessourceTrackerAmount");
	stream->writeUint32(amount, "amount");
	stream->writeUint32(static_cast<Uint32>(tracker_method), "tracker_method");
	stream->writeLeaveSection();
}



MaterialTrackerAge::MaterialTrackerAge(int age, TrackerMethod tracker_method) : age(age), tracker_method(tracker_method)
{

}



bool MaterialTrackerAge::passes(Runtime& runtime, int id)
{
	if(tracker_method==Greater)
	{
		return runtime.get_material_tracker(id)->get_age() > age;
	}
	else if(tracker_method==Lesser)
	{
		return runtime.get_material_tracker(id)->get_age() < age;
	}
	return false;
}



BuildingConditionType MaterialTrackerAge::get_type()
{
	return CMaterialTrackerAge;
}



bool MaterialTrackerAge::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("RessourceTrackerAge");
	age=stream->readUint32("age");
	tracker_method=static_cast<TrackerMethod>(stream->readUint32("tracker_method"));
	stream->readLeaveSection();
	return true;
}



void MaterialTrackerAge::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("RessourceTrackerAge");
	stream->writeUint32(age, "age");
	stream->writeUint32(static_cast<Uint32>(tracker_method), "tracker_method");
	stream->writeLeaveSection();
}
