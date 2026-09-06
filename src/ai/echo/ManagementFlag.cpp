// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "echo/Echo.h"
#include "Order.h"
#include "Brush.h"

using namespace AIEcho;
using namespace AIEcho::Management;
using namespace boost::logic;
using std::shared_ptr;


namespace
{
	int priority_to_int(AdjustPriority::BuildingPriority priority)
	{
		switch(priority)
		{
			case AdjustPriority::Low:    return AI_ECHO_PRIORITY_LOW;
			case AdjustPriority::Medium: return AI_ECHO_PRIORITY_MEDIUM;
			case AdjustPriority::High:   return AI_ECHO_PRIORITY_HIGH;
		}
		return AI_ECHO_PRIORITY_MEDIUM;
	}

	AdjustPriority::BuildingPriority int_to_priority(int p)
	{
		if(p == AI_ECHO_PRIORITY_LOW)  return AdjustPriority::Low;
		if(p == AI_ECHO_PRIORITY_HIGH) return AdjustPriority::High;
		return AdjustPriority::Medium;
	}

	void apply_area_modification(Echo& echo, AreaType areatype,
	                             const std::vector<position>& locations,
	                             Uint8 mode)
	{
		BrushAccumulator acc;
		for(std::vector<position>::const_iterator i=locations.begin(); i!=locations.end(); ++i)
		{
			acc.applyBrush(BrushApplication(echo.player->map->normalizeX(i->x), echo.player->map->normalizeY(i->y), 0), echo.player->map);
		}
		if(acc.getApplicationCount()==0)
			return;
		Uint8 team = echo.player->team->teamNumber;
		const Map* map = echo.player->map;
		switch(areatype)
		{
			case ClearingArea:
				echo.push_order(shared_ptr<Order>(new OrderAlterateClearArea(team, mode, &acc, map)));
				break;
			case ForbiddenArea:
				echo.push_order(shared_ptr<Order>(new OrderAlterateForbidden(team, mode, &acc, map)));
				break;
			case GuardArea:
				echo.push_order(shared_ptr<Order>(new OrderAlterateGuardArea(team, mode, &acc, map)));
				break;
		}
	}
}


ChangeFlagSize::ChangeFlagSize(int size, int building_id) : size(size), building_id(building_id)
{

}



void ChangeFlagSize::modify(Echo& echo)
{
	echo.push_order(shared_ptr<Order>(new OrderModifyFlag(echo.get_building_register().get_building(building_id)->gid, size)));
}



boost::logic::tribool ChangeFlagSize::wait(Echo& echo)
{
	return wait_for_building(echo, building_id);
}



bool ChangeFlagSize::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("ChangeFlagSize");
	ManagementOrder::load(stream, player, versionMinor);
	size=stream->readUint32("size");
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



ChangeFlagMinimumLevel::ChangeFlagMinimumLevel(int minimum_level, int building_id) : minimum_level(minimum_level), building_id(building_id)
{

}



void ChangeFlagMinimumLevel::modify(Echo& echo)
{
	echo.push_order(shared_ptr<Order>(new OrderModifyMinLevelToFlag(echo.get_building_register().get_building(building_id)->gid, minimum_level-AI_ECHO_LEVEL_OFFSET_USER_TO_ENGINE)));
}



boost::logic::tribool ChangeFlagMinimumLevel::wait(Echo& echo)
{
	return wait_for_building(echo, building_id);
}



bool ChangeFlagMinimumLevel::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("ChangeFlagMinimumLevel");
	ManagementOrder::load(stream, player, versionMinor);
	minimum_level=stream->readUint32("minimum_level");
	building_id=stream->readUint32("building_id");
	stream->readLeaveSection();
	return true;
}



void ChangeFlagMinimumLevel::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("ChangeFlagMinimumLevel");
	ManagementOrder::save(stream);
	stream->writeUint32(minimum_level, "minimum_level");
	stream->writeUint32(building_id, "building_id");
	stream->writeLeaveSection();
}



ChangeFlagPosition::ChangeFlagPosition(int x, int y, int building_id)
	: x(x), y(y), building_id(building_id)
{

}


void ChangeFlagPosition::modify(Echo& echo)
{
	echo.push_order(shared_ptr<Order>(new OrderMoveFlag(echo.get_building_register().get_building(building_id)->gid, x, y, true)));
}



boost::logic::tribool ChangeFlagPosition::wait(Echo& echo)
{
	return wait_for_building(echo, building_id);
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


void AdjustPriority::modify(Echo& echo)
{
	echo.push_order(shared_ptr<Order>(new OrderChangePriority(echo.get_building_register().get_building(building_id)->gid, priority_to_int(priority))));
}



boost::logic::tribool AdjustPriority::wait(Echo& echo)
{
	return wait_for_building(echo, building_id);
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



void AddArea::modify(Echo& echo)
{
	apply_area_modification(echo, areatype, locations, BrushTool::MODE_ADD);
}



boost::logic::tribool AddArea::wait(Echo& echo)
{
	return true;
}



bool AddArea::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("AddArea");
	ManagementOrder::load(stream, player, versionMinor);
	areatype=static_cast<AreaType>(stream->readUint32("area_type"));
	stream->readEnterSection("locations");
	Uint32 size=stream->readUint32("size");
	locations.resize(size);
	for(Uint32 location_index=0; location_index<size; ++location_index)
	{
		stream->readEnterSection(location_index);
		locations[location_index]=position(stream->readUint32("posx"), stream->readUint32("posy"));
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



void RemoveArea::modify(Echo& echo)
{
	apply_area_modification(echo, areatype, locations, BrushTool::MODE_DEL);
}



boost::logic::tribool RemoveArea::wait(Echo& echo)
{
	return true;
}



bool RemoveArea::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("RemoveArea");
	ManagementOrder::load(stream, player, versionMinor);
	areatype=static_cast<AreaType>(stream->readUint32("area_type"));
	stream->readEnterSection("locations");
	Uint32 size=stream->readUint32("size");
	locations.resize(size);
	for(Uint32 location_index=0; location_index<size; ++location_index)
	{
		stream->readEnterSection(location_index);
		locations[location_index]=position(stream->readUint32("posx"), stream->readUint32("posy"));
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

