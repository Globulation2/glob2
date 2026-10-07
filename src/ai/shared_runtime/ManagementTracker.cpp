// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "shared_runtime/Runtime.h"
#include "Building.h"
#include "FileFormatVersions.h"

#include <bit>
#include <limits>
#include <stdexcept>

using namespace AISharedRuntime;
using namespace AISharedRuntime::Management;


ResourceTracker::ResourceTracker(Runtime& runtime, int building_id, int length, int resource) : record(length, 0), position(0), timer(0), length(length), runtime(runtime), building_id(building_id), resource(resource)
{

}



void ResourceTracker::tick()
{
	if (record.empty() || position >= record.size() || resource < 0 || resource > RecurringInputStock)
		return;
	timer = (timer == std::numeric_limits<int>::max()) ? 0 : timer + 1;
	if((timer%AI_SHARED_RUNTIME_TRACKER_SAMPLE_INTERVAL_TICKS)==0)
	{
		const AIEngine::BuildingView* b = runtime.get_building_register().get_building(building_id);
		if (!b) return;
		if(resource==RecurringInputStock) {
   int amount=0;
   const auto& semantics=AIEngine::ObservationQueries::buildingType(runtime.observation(),*b).semantics;
   unsigned inputs=semantics.feeding.enabled ? semantics.feeding.costMask : 0;
   for(const auto& recipe:semantics.production.recipes)
    if(recipe.enabled) inputs|=recipe.costMask;
   while(inputs) {
    const unsigned input=std::countr_zero(inputs);
    amount+=runtime.observation().buildingResources(*b)[input];
    inputs&=inputs-1;
   }
   record[position]=amount;
  } else record[position]=runtime.observation().buildingResources(*b)[resource];
		position++;
		if(position>=record.size())
			position=0;
	}
}


int ResourceTracker::get_total_level()
{
	long long sum=0;
	for(unsigned int n=0; n<record.size(); ++n)
	{
		sum+=record[n];
		if (sum > std::numeric_limits<int>::max())
			return std::numeric_limits<int>::max();
	}
	return static_cast<int>(sum);
}



bool ResourceTracker::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("RessourceTracker");
	stream->readEnterSection("record");
	Uint32 recordsize=stream->readCount("size");
	if (recordsize == 0)
		throw std::runtime_error("Invalid saved resource tracker length");
	record.resize(recordsize);
	for(unsigned int record_index=0; record_index<recordsize; ++record_index)
	{
		stream->readEnterSection(record_index);
		const Uint32 quantity=stream->readUint32("quantity_of_ressources");
		if (quantity > static_cast<Uint32>(std::numeric_limits<int>::max()))
			throw std::runtime_error("Invalid saved resource tracker quantity");
		record[record_index]=static_cast<int>(quantity);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	position=stream->readUint32("position");
	const Uint32 rawTimer=stream->readUint32("timer");
	const Uint32 rawBuildingId=stream->readUint32("building_id");
	const Uint32 rawLength=stream->readUint32("length");
	const Uint32 rawResource=stream->readUint32("ressource");
	// These members are signed and participate in arithmetic or lookups. Reject
	// out-of-domain wire values before narrowing instead of relying on an
	// implementation-defined Uint32-to-int conversion.
	if (rawTimer > static_cast<Uint32>(std::numeric_limits<int>::max()) ||
		rawBuildingId > static_cast<Uint32>(std::numeric_limits<int>::max()) ||
		rawLength > static_cast<Uint32>(std::numeric_limits<int>::max()) ||
		rawResource > (versionMinor>=FILE_FORMAT_VERSION_BUILDING_CATALOG ? RecurringInputStock : MAX_RESOURCES-1) || position >= record.size() || rawLength != record.size())
		throw std::runtime_error("Invalid saved resource tracker");
	timer=static_cast<int>(rawTimer);
	building_id=static_cast<int>(rawBuildingId);
	length=static_cast<int>(rawLength);
	resource=static_cast<int>(rawResource);
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



void AddResourceTracker::modify(Runtime& runtime)
{
	if (length <= 0 || length > 1048576 || resource < 0 || resource > RecurringInputStock)
		return;
	runtime.add_resource_tracker(new ResourceTracker(runtime, building_id, length, resource), building_id);
}



tribool AddResourceTracker::wait(Runtime& runtime)
{
	return wait_for_building(runtime, building_id);
}



bool AddResourceTracker::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("AddRessourceTracker");
	ManagementOrder::load(stream, player, versionMinor);
	const Uint32 rawLength=stream->readUint32("length");
	const Uint32 rawBuildingId=stream->readUint32("building_id");
	const Uint32 rawResource=stream->readUint32("ressource");
	if (rawLength == 0 || rawLength > 1048576 ||
		rawBuildingId > static_cast<Uint32>(std::numeric_limits<int>::max()) || rawResource > (versionMinor>=FILE_FORMAT_VERSION_BUILDING_CATALOG ? RecurringInputStock : MAX_RESOURCES-1))
		throw std::runtime_error("Invalid resource tracker order");
	length=static_cast<int>(rawLength);
	building_id=static_cast<int>(rawBuildingId);
	resource=static_cast<int>(rawResource);
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



void PauseResourceTracker::modify(Runtime& runtime)
{
	runtime.pause_resource_tracker(building_id);
}



tribool PauseResourceTracker::wait(Runtime& runtime)
{
	return wait_for_building(runtime, building_id);
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



void UnPauseResourceTracker::modify(Runtime& runtime)
{
	runtime.unpause_resource_tracker(building_id);
}



tribool UnPauseResourceTracker::wait(Runtime& runtime)
{
	return wait_for_building(runtime, building_id);
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
