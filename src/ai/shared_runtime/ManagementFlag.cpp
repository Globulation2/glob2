// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "shared_runtime/Runtime.h"
#include "shared_runtime/ObservationAreaOrders.h"
#include "FileFormatVersions.h"
#include "Order.h"
#include "Brush.h"
#include <algorithm>

using namespace AISharedRuntime;
using namespace AISharedRuntime::Management;
using std::shared_ptr;


namespace
{
	int priority_to_int(AdjustPriority::BuildingPriority priority)
	{
		switch(priority)
		{
			case AdjustPriority::Low:    return AI_SHARED_RUNTIME_PRIORITY_LOW;
			case AdjustPriority::Medium: return AI_SHARED_RUNTIME_PRIORITY_MEDIUM;
			case AdjustPriority::High:   return AI_SHARED_RUNTIME_PRIORITY_HIGH;
		}
		return AI_SHARED_RUNTIME_PRIORITY_MEDIUM;
	}

	AdjustPriority::BuildingPriority int_to_priority(int p)
	{
		if(p == AI_SHARED_RUNTIME_PRIORITY_LOW)  return AdjustPriority::Low;
		if(p == AI_SHARED_RUNTIME_PRIORITY_HIGH) return AdjustPriority::High;
		return AdjustPriority::Medium;
	}

	void apply_area_modification(Runtime& runtime, AreaType areatype,
	                             const std::vector<position>& locations,
	                             Uint8 mode)
	{
		BrushAccumulator acc;
		for(std::vector<position>::const_iterator i=locations.begin(); i!=locations.end(); ++i)
		{
			acc.applyBrush(BrushApplication(runtime.readPlayer()->map->normalizeX(i->x), runtime.readPlayer()->map->normalizeY(i->y), 0), runtime.readPlayer()->map->getW(), runtime.readPlayer()->map->getH());
		}
		if(acc.getApplicationCount()==0)
			return;
		Uint8 team = runtime.readPlayer()->team->teamNumber;

		switch(areatype)
		{
			case ClearingArea:
				runtime.push_order(observationAreaOrder<OrderAlterClearArea>(team,mode,acc));
				break;
			case ForbiddenArea:
				runtime.push_order(observationAreaOrder<OrderAlterForbidden>(team,mode,acc));
				break;
			case GuardArea:
				runtime.push_order(observationAreaOrder<OrderAlterGuardArea>(team,mode,acc));
				break;
			case FarmArea:
				// Only issued when the game carries the farm-areas experiment.
				runtime.push_order(observationAreaOrder<OrderAlterFarmArea>(team,mode,acc));
				break;
		}
	}
}


ChangeFlagSize::ChangeFlagSize(int size, int building_id) : size(size), building_id(building_id)
{

}



void ChangeFlagSize::modify(Runtime& runtime)
{
	auto* building=runtime.get_building_register().get_building(building_id);
 runtime.push_order(std::make_shared<OrderModifyFlag>(building->gid,std::clamp(size,0,building->type->maxUnitStayRange)));
}



tribool ChangeFlagSize::wait(Runtime& runtime)
{
	return wait_for_building(runtime, building_id);
}



bool ChangeFlagSize::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("ChangeFlagSize");
	ManagementOrder::load(stream, player, versionMinor);
	size=stream->readCount("size");
	building_id=stream->readUint32("building_id");
	stream->readLeaveSection();
	return true;
}



void ChangeFlagSize::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("ChangeFlagSize");
	ManagementOrder::save(stream);
	stream->writeUint32(size, "size");
	stream->writeUint32(building_id, "building_id");
	stream->writeLeaveSection();
}



ChangeFlagMinimumLevel::ChangeFlagMinimumLevel(int minimum_level, int building_id, int targetRole) : minimum_level(minimum_level), building_id(building_id), targetRole(targetRole)
{

}



void ChangeFlagMinimumLevel::modify(Runtime& runtime)
{
	const auto* building=runtime.get_building_register().get_building(building_id);
	if(!building)return;
	const bool explorers=targetRole==1 || (targetRole<0 && building->type->zonable[EXPLORER]
		&& !building->type->zonable[WORKER] && !building->type->zonable[WARRIOR]);
	const int requirement=explorers ? (targetRole<0 ? minimum_level>1 : minimum_level!=0)
		: minimum_level-AI_SHARED_RUNTIME_LEVEL_OFFSET_USER_TO_ENGINE;
	runtime.push_order(std::make_shared<OrderModifyMinLevelToFlag>(building->gid,requirement,explorers ? 1 : 0));
}



tribool ChangeFlagMinimumLevel::wait(Runtime& runtime)
{
	return wait_for_building(runtime, building_id);
}



bool ChangeFlagMinimumLevel::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("ChangeFlagMinimumLevel");
	ManagementOrder::load(stream, player, versionMinor);
	minimum_level=stream->readUint32("minimum_level");
	building_id=stream->readUint32("building_id");
	targetRole=versionMinor>=FILE_FORMAT_VERSION_BUILDING_CATALOG ? stream->readSint32("target_role") : -1;
	if(targetRole < -1 || targetRole > 1) return false;
	stream->readLeaveSection();
	return true;
}



void ChangeFlagMinimumLevel::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("ChangeFlagMinimumLevel");
	ManagementOrder::save(stream);
	stream->writeUint32(minimum_level, "minimum_level");
	stream->writeUint32(building_id, "building_id");
	stream->writeSint32(targetRole,"target_role");
	stream->writeLeaveSection();
}



ChangeFlagPosition::ChangeFlagPosition(int x, int y, int building_id)
	: x(x), y(y), building_id(building_id)
{

}


void ChangeFlagPosition::modify(Runtime& runtime)
{
	const auto* building = runtime.get_building_register().get_building(building_id);
	if (building && building->type->semantics.relocatable)
		runtime.push_order(shared_ptr<Order>(new OrderMoveFlag(building->gid, x, y, true)));
}



tribool ChangeFlagPosition::wait(Runtime& runtime)
{
	return wait_for_building(runtime, building_id);
}



ManagementOrderType ChangeFlagPosition::get_type()
{
	return MChangeFlagPosition;
}



bool ChangeFlagPosition::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("ChangeFlagPosition");
	ManagementOrder::load(stream, player, versionMinor);
	building_id=stream->readUint32("building_id");
	x=stream->readUint32("x");
	y=stream->readUint32("y");
	stream->readLeaveSection();
	return true;
}



void ChangeFlagPosition::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("ChangeFlagPosition");
	ManagementOrder::save(stream);
	stream->writeUint32(building_id, "building_id");
	stream->writeUint32(x, "x");
	stream->writeUint32(y, "y");
	stream->writeLeaveSection();
}



AdjustPriority::AdjustPriority(int building_id, AdjustPriority::BuildingPriority priority)
	: building_id(building_id), priority(priority)
{

}


void AdjustPriority::modify(Runtime& runtime)
{
    if(const auto* building=runtime.get_building_register().get_building(building_id))
        runtime.push_order(shared_ptr<Order>(new OrderChangePriority(building->gid, priority_to_int(priority))));
}



tribool AdjustPriority::wait(Runtime& runtime)
{
	return wait_for_building(runtime, building_id);
}



ManagementOrderType AdjustPriority::get_type()
{
	return MAdjustPriority;
}



bool AdjustPriority::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("AdjustPriority");
	ManagementOrder::load(stream, player, versionMinor);
	building_id=stream->readUint32("building_id");
	priority = int_to_priority(stream->readSint32("priority"));
	stream->readLeaveSection();
	return true;
}



void AdjustPriority::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("AdjustPriority");
	ManagementOrder::save(stream);
	stream->writeUint32(building_id, "building_id");
	stream->writeSint32(priority_to_int(priority), "priority");
	stream->writeLeaveSection();
}




AddArea::AddArea(AreaType areatype) : areatype(areatype)
{

}



void AddArea::add_location(int x, int y)
{
	locations.push_back(position(x, y));
}



void AddArea::modify(Runtime& runtime)
{
	apply_area_modification(runtime, areatype, locations, BrushTool::MODE_ADD);
}



tribool AddArea::wait(Runtime& runtime)
{
	return true;
}



bool AddArea::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("AddArea");
	ManagementOrder::load(stream, player, versionMinor);
	const Uint32 savedArea=stream->readUint32("area_type");
	if (savedArea > FarmArea) return false;
	areatype=static_cast<AreaType>(savedArea);
	stream->readEnterSection("locations");
	Uint32 size=stream->readCount("size");
	locations.resize(size);
	for(Uint32 location_index=0; location_index<size; ++location_index)
	{
		stream->readEnterSection(location_index);
		// Stream reads must be sequenced: argument evaluation order varies by compiler.
		const Uint32 x = stream->readUint32("posx");
		const Uint32 y = stream->readUint32("posy");
		locations[location_index] = position(x, y);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	stream->readLeaveSection();
	return true;
}



void AddArea::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("AddArea");
	ManagementOrder::save(stream);
	stream->writeUint32(areatype, "area_type");
	stream->writeEnterSection("locations");
	stream->writeUint32(locations.size(), "size");
	for(Uint32 location_index=0; location_index<locations.size(); ++location_index)
	{
		stream->writeEnterSection(location_index);
		stream->writeUint32(locations[location_index].x, "posx");
		stream->writeUint32(locations[location_index].y, "posy");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}



RemoveArea::RemoveArea(AreaType areatype) : areatype(areatype)
{

}



void RemoveArea::add_location(int x, int y)
{
	locations.push_back(position(x, y));
}



void RemoveArea::modify(Runtime& runtime)
{
	apply_area_modification(runtime, areatype, locations, BrushTool::MODE_DEL);
}



tribool RemoveArea::wait(Runtime& runtime)
{
	return true;
}



bool RemoveArea::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("RemoveArea");
	ManagementOrder::load(stream, player, versionMinor);
	const Uint32 savedArea=stream->readUint32("area_type");
	if (savedArea > FarmArea) return false;
	areatype=static_cast<AreaType>(savedArea);
	stream->readEnterSection("locations");
	Uint32 size=stream->readCount("size");
	locations.resize(size);
	for(Uint32 location_index=0; location_index<size; ++location_index)
	{
		stream->readEnterSection(location_index);
		// Stream reads must be sequenced: argument evaluation order varies by compiler.
		const Uint32 x = stream->readUint32("posx");
		const Uint32 y = stream->readUint32("posy");
		locations[location_index] = position(x, y);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	stream->readLeaveSection();
	return true;
}



void RemoveArea::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("RemoveArea");
	ManagementOrder::save(stream);
	stream->writeUint32(areatype, "area_type");
	stream->writeEnterSection("locations");
	stream->writeUint32(locations.size(), "size");
	for(Uint32 location_index=0; location_index<locations.size(); ++location_index)
	{
		stream->writeEnterSection(location_index);
		stream->writeUint32(locations[location_index].x, "posx");
		stream->writeUint32(locations[location_index].y, "posy");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}
