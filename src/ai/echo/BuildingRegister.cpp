// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "echo/Echo.h"
#include "Building.h"
#include "BuildingType.h"
#include "IntBuildingType.h"
#include <tuple>

using namespace AIEcho;
using namespace AIEcho::Construction;
using namespace AIEcho::SearchTools;
using namespace boost::logic;


FlagMap::FlagMap(Echo& echo) : flagmap(echo.player->map->getW()*echo.player->map->getH(), NOGBID), width(echo.player->map->getW()), echo(echo)
{
}



int FlagMap::get_flag(int x, int y)
{
	return flagmap[y*width+x];
}



void FlagMap::set_flag(int x, int y, int gid)
{
	flagmap[y*width+x]=gid;
}



bool FlagMap::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("FlagMap");
	stream->readEnterSection("flagmap");
	Uint32 size=stream->readUint32("size");
	flagmap.resize(size);
	for (Uint32 flagmap_index = 0; flagmap_index < size; flagmap_index++)
	{
		stream->readEnterSection(flagmap_index);
		flagmap[flagmap_index]=stream->readUint32("gid");
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	width=stream->readUint32("width");
	stream->readLeaveSection();
	return true;
}



void FlagMap::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("FlagMap");
	stream->writeEnterSection("flagmap");
	stream->writeUint32(flagmap.size(), "size");
	for (Uint32 flagmap_index = 0; flagmap_index < flagmap.size(); flagmap_index++)
	{
		stream->writeEnterSection(flagmap_index);
		stream->writeUint32(flagmap[flagmap_index], "gid");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeUint32(width, "width");
	stream->writeLeaveSection();
}



BuildingRegister::BuildingRegister(Player* player, Echo& echo) : building_id(0), player(player), echo(echo)
{

}



void BuildingRegister::initiate()
{
	for(int i=0; i<Building::MAX_COUNT; ++i)
	{
		Building* b=player->team->myBuildings[i];
		if(b!=NULL)
		{
			found_buildings[building_id++]=std::make_tuple(b->posX, b->posY, b->type->shortTypeNum, b->gid, false);
		}
	}
}



unsigned int BuildingRegister::register_building()
{
	pending_buildings[building_id]=std::make_tuple(-1, -1, -1, AI_ECHO_PENDING_NOT_ISSUED);
	return building_id++;
}



void BuildingRegister::issue_order(int id, int x, int y, int building_type)
{
	pending_buildings[id]=std::make_tuple(x, y, building_type, 0);
}



void BuildingRegister::remove_building(int id)
{
	pending_buildings.erase(id);
}



bool BuildingRegister::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("BuildingRegister");

	stream->readEnterSection("pending_buildings");
	Uint32 pending_size=stream->readUint32("size");
	for(Uint32 pending_index=0; pending_index<pending_size; ++pending_index)
	{
		stream->readEnterSection(pending_index);
		Uint32 id=stream->readSint32("echo_building_id");
		Uint32 x=stream->readSint32("xpos");
		Uint32 y=stream->readSint32("ypos");
		Uint32 type=stream->readSint32("building_type");
		Uint32 ticks=stream->readSint32("ticks_since_registered");
		pending_buildings[id]=std::make_tuple(x, y, type, ticks);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	stream->readEnterSection("found_buildings");
	Uint32 found_size=stream->readUint32("size");
	for(Uint32 found_index=0; found_index<found_size; ++found_index)
	{
		stream->readEnterSection(found_index);
		Uint32 id=stream->readUint32("echo_building_id");
		Uint32 xpos=stream->readUint32("xpos");
		Uint32 ypos=stream->readUint32("ypos");
		Uint32 building_type=stream->readUint32("building_type");
		Uint32 gid=stream->readUint32("gid");
		Uint8 upgrade_status=stream->readUint8("upgrade_status");
		boost::logic::tribool t;
		if(upgrade_status==AI_ECHO_TRIBOOL_FALSE)
			t=false;
		else if(upgrade_status==AI_ECHO_TRIBOOL_TRUE)
			t=true;
		else
			t=indeterminate;
		found_buildings[id]=std::make_tuple(xpos, ypos, building_type, gid, t);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	building_id=stream->readUint32("building_id");
	stream->readLeaveSection();
	return true;
}



void BuildingRegister::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("BuildingRegister");

	stream->writeEnterSection("pending_buildings");
	unsigned int pending_size=0;
	stream->writeUint32(pending_buildings.size(), "size");
	for(pending_iterator i=pending_buildings.begin(); i!=pending_buildings.end(); ++i)
	{
		stream->writeEnterSection(pending_size);
		stream->writeSint32(i->first, "echo_building_id");
		stream->writeSint32(std::get<0>(i->second), "xpos");
		stream->writeSint32(std::get<1>(i->second), "ypos");
		stream->writeSint32(std::get<2>(i->second), "building_type");
		stream->writeSint32(std::get<3>(i->second), "ticks_since_registered");
		stream->writeLeaveSection();
		pending_size++;
	}
	stream->writeLeaveSection();

	stream->writeEnterSection("found_buildings");
	unsigned int found_size=0;
	stream->writeUint32(found_buildings.size(), "size");
	for(found_iterator i=found_buildings.begin(); i!=found_buildings.end(); ++i)
	{
		stream->writeEnterSection(found_size);
		stream->writeUint32(i->first, "echo_building_id");
		stream->writeUint32(std::get<0>(i->second), "xpos");
		stream->writeUint32(std::get<1>(i->second), "ypos");
		stream->writeUint32(std::get<2>(i->second), "building_type");
		stream->writeUint32(std::get<3>(i->second), "gid");
		if(std::get<4>(i->second))
			stream->writeUint8(AI_ECHO_TRIBOOL_TRUE, "upgrade_status");
		else if(!std::get<4>(i->second))
			stream->writeUint8(AI_ECHO_TRIBOOL_FALSE, "upgrade_status");
		else
			stream->writeUint8(AI_ECHO_TRIBOOL_INDETERMINATE, "upgrade_status");
		stream->writeLeaveSection();
		found_size++;
	}
	stream->writeLeaveSection();

	stream->writeUint32(building_id, "building_id");
	stream->writeLeaveSection();
}



void BuildingRegister::set_upgrading(unsigned int id)
{
	std::get<4>(found_buildings[id])=indeterminate;
}




void BuildingRegister::tick()
{
	for(pending_iterator i=pending_buildings.begin(); i!=pending_buildings.end();)
	{
		//When get<3>() is AI_ECHO_PENDING_NOT_ISSUED, it means that the building order
		//hasen't been sent to the glob2 engine yet. This is used when the building is
		//registered, but awaiting conditions to be satisfied.
		if(std::get<3>(i->second)!=AI_ECHO_PENDING_NOT_ISSUED)
		{
			std::get<3>(i->second)++;
			if(std::get<3>(i->second) > AI_ECHO_PENDING_BUILDING_TIMEOUT_TICKS)
			{
				pending_iterator current=i;
				++i;
				pending_buildings.erase(current);
				continue;
			}
			int gbid=NOGBID;
			if(std::get<2>(i->second) > IntBuildingType::DEFENSE_BUILDING && std::get<2>(i->second) < IntBuildingType::STONE_WALL)
			{
				gbid=is_flag(echo, std::get<0>(i->second), std::get<1>(i->second));
			}
			else
			{
				gbid=player->map->getBuilding(std::get<0>(i->second), std::get<1>(i->second));
			}
			if(gbid!=NOGBID)
			{
				if(std::get<2>(i->second) > IntBuildingType::DEFENSE_BUILDING && std::get<2>(i->second) < IntBuildingType::STONE_WALL)
				{
					echo.get_flag_map().set_flag(std::get<0>(i->second), std::get<1>(i->second), gbid);
				}
				found_buildings[i->first]=std::make_tuple(std::get<0>(i->second), std::get<1>(i->second), std::get<2>(i->second), gbid, false);
				pending_iterator current=i;
				++i;
				pending_buildings.erase(current);
				continue;
			}
		}
		++i;
	}
	for(found_iterator i = found_buildings.begin(); i!=found_buildings.end();)
	{
		if(std::get<2>(i->second) > IntBuildingType::DEFENSE_BUILDING && std::get<2>(i->second) < IntBuildingType::STONE_WALL)
		{
			if(echo.get_flag_map().get_flag(std::get<0>(i->second), std::get<1>(i->second))==NOGBID)
			{
				found_iterator current=i;
				++i;
				found_buildings.erase(current);
				continue;
			}
			if(player->team->myBuildings[::Building::GIDtoID(std::get<3>(i->second))]==NULL)
			{
				echo.get_flag_map().set_flag(std::get<0>(i->second), std::get<1>(i->second), NOGBID);
				found_iterator current=i;
				++i;
				found_buildings.erase(current);
				continue;
			}
		}
		else
		{
			const int gbid=player->map->getBuilding(std::get<0>(i->second), std::get<1>(i->second));
			if(gbid==NOGBID || gbid != std::get<3>(i->second))
			{
				found_iterator current=i;
				++i;
				found_buildings.erase(current);
				continue;
			}
			Building* b=player->team->myBuildings[::Building::GIDtoID(gbid)];
			if(b==NULL)
			{
				found_iterator current=i;
				++i;
				found_buildings.erase(current);
				continue;
			}
			//True
			if(std::get<4>(i->second))
			{
				std::get<0>(i->second)=b->posX;
				std::get<1>(i->second)=b->posY;
				if(b->constructionResultState==::Building::NO_CONSTRUCTION)
				{
					std::get<4>(i->second)=false;
				}
			}
			//False
			else if(!std::get<4>(i->second))
			{

			}
			//Indeterminate
			else
			{
				if(b->constructionResultState!=::Building::NO_CONSTRUCTION)
				{
					std::get<4>(i->second)=true;
				}
			}
		}
		++i;
	}
}

bool BuildingRegister::is_building_pending(unsigned int id)
{
	if(pending_buildings.find(id)!=pending_buildings.end())
	{
		return true;
	}
	return false;
}



bool BuildingRegister::is_building_found(unsigned int id)
{
	if(found_buildings.find(id)!=found_buildings.end())
	{
		return true;
	}
	return false;
}




bool BuildingRegister::is_building_upgrading(unsigned int id)
{
	if(found_buildings.find(id)==found_buildings.end())
	{
		return false;
	}

	tribool v=std::get<4>(found_buildings[id]);
	if(v)
		return true;
	else if(!v)
		return false;
	return true;
}



Building* BuildingRegister::get_building(unsigned int id)
{
	if(found_buildings.find(id)==found_buildings.end())
	{
		return NULL;
	}
	return player->team->myBuildings[::Building::GIDtoID(std::get<3>(found_buildings[id]))];
}



BuildingType* BuildingRegister::get_building_type(unsigned int id)
{
	if(found_buildings.find(id)==found_buildings.end())
	{
		return NULL;
	}
	return player->team->myBuildings[::Building::GIDtoID(std::get<3>(found_buildings[id]))]->type;
}



int BuildingRegister::get_type(unsigned int id)
{
	if(found_buildings.find(id)==found_buildings.end())
	{
		return 0;
	}
	return std::get<2>(found_buildings[id]);
}



int BuildingRegister::get_level(unsigned int id)
{
	if(found_buildings.find(id)==found_buildings.end())
	{
		return 0;
	}
	return get_building(id)->type->level+1;
}



int BuildingRegister::get_assigned(unsigned int id)
{
	if(found_buildings.find(id)==found_buildings.end())
	{
		return 0;
	}
	return get_building(id)->maxUnitWorking;
}
