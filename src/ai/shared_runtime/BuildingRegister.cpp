// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "shared_runtime/Runtime.h"
#include "Building.h"
#include "Game.h"
#include "BuildingType.h"
#include "shared_runtime/BuildingDemands.h"
#include <tuple>
#include "FileFormatVersions.h"

using namespace AISharedRuntime;
using namespace AISharedRuntime::Construction;
using namespace AISharedRuntime::SearchTools;


FlagMap::FlagMap(Runtime& runtime) : flagmap(runtime.player->map->getW()*runtime.player->map->getH(), NOGBID), width(runtime.player->map->getW()), runtime(runtime)
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
	Uint32 size=stream->readCount("size");
	if (size != static_cast<Uint32>(player->map->getW()*player->map->getH())) return false;
	flagmap.resize(size);
	for (Uint32 flagmap_index = 0; flagmap_index < size; flagmap_index++)
	{
		stream->readEnterSection(flagmap_index);
		flagmap[flagmap_index]=stream->readUint32("gid");
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	width=stream->readUint32("width");
	if (width != player->map->getW()) return false;
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



BuildingRegister::BuildingRegister(Player* player, Runtime& runtime) : building_id(0), player(player), runtime(runtime)
{

}



void BuildingRegister::initiate()
{
	for(int i=0; i<Building::MAX_COUNT; ++i)
	{
		Building* b=player->team->myBuildings[i];
		if(b!=NULL)
		{
			found_buildings[building_id++]=std::make_tuple(b->posX, b->posY, b->typeNum, b->gid, false);
		}
	}
}



unsigned int BuildingRegister::register_building()
{
	pending_buildings[building_id]=std::make_tuple(-1, -1, -1, AI_SHARED_RUNTIME_PENDING_NOT_ISSUED);
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
	pending_buildings.clear();
	found_buildings.clear();
	stream->readEnterSection("BuildingRegister");

	stream->readEnterSection("pending_buildings");
	Uint32 pending_size=stream->readCount("size");
	for(Uint32 pending_index=0; pending_index<pending_size; ++pending_index)
	{
		stream->readEnterSection(pending_index);
		Uint32 id=stream->readSint32("echo_building_id");
		Uint32 x=stream->readSint32("xpos");
		Uint32 y=stream->readSint32("ypos");
		Uint32 type=stream->readSint32("building_type");
		Uint32 ticks=stream->readSint32("ticks_since_registered");
  if(versionMinor<FILE_FORMAT_VERSION_BUILDING_CATALOG && int(type)>=0) type=importLegacyBuildingId(*player->game,type,0,true);
		pending_buildings[id]=std::make_tuple(x, y, type, ticks);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	stream->readEnterSection("found_buildings");
	Uint32 found_size=stream->readCount("size");
	for(Uint32 found_index=0; found_index<found_size; ++found_index)
	{
		stream->readEnterSection(found_index);
		Uint32 id=stream->readUint32("echo_building_id");
		Uint32 xpos=stream->readUint32("xpos");
		Uint32 ypos=stream->readUint32("ypos");
		Uint32 building_type=stream->readUint32("building_type");
		Uint32 gid=stream->readUint32("gid");
		if (gid >= ::Building::MAX_COUNT * Team::MAX_COUNT || ::Building::GIDtoTeam(gid) != player->team->teamNumber) return false;
		if(versionMinor<FILE_FORMAT_VERSION_BUILDING_CATALOG) {
   auto* building=player->team->myBuildings[::Building::GIDtoID(gid)];
   building_type=building ? building->typeNum : importLegacyBuildingId(*player->game,building_type);
  }
  Uint8 upgrade_status=stream->readUint8("upgrade_status");
		tribool t;
		if(upgrade_status==AI_SHARED_RUNTIME_TRIBOOL_FALSE)
			t=false;
		else if(upgrade_status==AI_SHARED_RUNTIME_TRIBOOL_TRUE)
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
			stream->writeUint8(AI_SHARED_RUNTIME_TRIBOOL_TRUE, "upgrade_status");
		else if(!std::get<4>(i->second))
			stream->writeUint8(AI_SHARED_RUNTIME_TRIBOOL_FALSE, "upgrade_status");
		else
			stream->writeUint8(AI_SHARED_RUNTIME_TRIBOOL_INDETERMINATE, "upgrade_status");
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
 for(auto i=pending_buildings.begin();i!=pending_buildings.end();) {
  auto& pending=i->second;
  auto& ticks=std::get<3>(pending);
  if(ticks==AI_SHARED_RUNTIME_PENDING_NOT_ISSUED) { ++i; continue; }
  const int type=std::get<2>(pending);
  if(++ticks>AI_SHARED_RUNTIME_PENDING_BUILDING_TIMEOUT_TICKS || type<0 || size_t(type)>=player->game->buildingsTypes.size()) { i=pending_buildings.erase(i); continue; }
  const auto* definition=player->game->buildingsTypes.get(type);
  const int x=std::get<0>(pending),y=std::get<1>(pending);
  const int gid=definition->semantics.occupiesGround ? player->map->getBuilding(x,y) : is_flag(runtime,x,y);
  auto* building=gid!=NOGBID && ::Building::GIDtoTeam(gid)==player->team->teamNumber ? player->team->myBuildings[::Building::GIDtoID(gid)] : nullptr;
  if(building && (building->typeNum==type || (definition->isBuildingSite && building->typeNum==definition->nextLevel))) {
   if(!building->type->semantics.occupiesGround) runtime.get_flag_map().set_flag(x,y,gid);
   found_buildings[i->first]=std::make_tuple(x,y,building->typeNum,gid,false);
   i=pending_buildings.erase(i); continue;
  }
  ++i;
 }
 for(auto i=found_buildings.begin();i!=found_buildings.end();) {
  auto& found=i->second;
  const int gid=std::get<3>(found);
  auto* building=player->team->myBuildings[::Building::GIDtoID(gid)];
  const int oldX=std::get<0>(found),oldY=std::get<1>(found);
  if(runtime.get_flag_map().get_flag(oldX,oldY)==gid) runtime.get_flag_map().set_flag(oldX,oldY,NOGBID);
  if(!building) { i=found_buildings.erase(i); continue; }
  std::get<0>(found)=building->posX; std::get<1>(found)=building->posY; std::get<2>(found)=building->typeNum;
  if(!building->type->semantics.occupiesGround) runtime.get_flag_map().set_flag(building->posX,building->posY,gid);
  auto& upgrading=std::get<4>(found);
  if(building->constructionResultState!=::Building::NO_CONSTRUCTION) upgrading=true;
  // Queued engine orders drain before this observation. A transition can
  // finish between observations, including a repair that keeps the same type.
  else upgrading=false;
  ++i;
 }
}

bool BuildingRegister::is_building_pending(unsigned int id) const
{
	if(pending_buildings.find(id)!=pending_buildings.end())
	{
		return true;
	}
	return false;
}



bool BuildingRegister::is_building_found(unsigned int id) const
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
		return -1;
	}
	auto* building=get_building(id);
 return building ? building->typeNum : -1;
}



int BuildingRegister::get_level(unsigned int id)
{
	if(found_buildings.find(id)==found_buildings.end())
	{
		return 0;
	}
	return runtime.player->game->buildingCapabilities().lineagePosition(get_building(id)->typeNum);
}



int BuildingRegister::get_assigned(unsigned int id)
{
	if(found_buildings.find(id)==found_buildings.end())
	{
		return 0;
	}
	return get_building(id)->maxUnitWorking;
}

bool BuildingRegister::provides(unsigned int id,int demand)
{
 return buildingProvides(*player->game,get_type(id),demand);
}
