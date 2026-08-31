// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "echo/Echo.h"
#include "Building.h"
#include "Game.h"

using namespace AIEcho;
using namespace AIEcho::Gradients;


Entities::Building::Building(int building_type, int team, bool under_construction) : building_type(building_type), team(team), under_construction(under_construction)
{
}

bool Entities::Building::is_entity(Map* map, int posx, int posy)
{
	int building_id=map->getBuilding(posx, posy);
	if(building_id==NOGBID)
		return false;
	int team_id=::Building::GIDtoTeam(building_id);
	if(team_id!=team)
		return false;
	::Building* b = map->game->teams[team_id]->myBuildings[::Building::GIDtoID(building_id)];
	return b->typeNum==building_type &&
	       (b->constructionResultState==::Building::NO_CONSTRUCTION || under_construction);
}

bool Entities::Building::operator==(const Entity& rhs) const
{
	if(typeid(rhs)!=typeid(Entities::Building))
		return false;
	const Entities::Building& o = static_cast<const Entities::Building&>(rhs);
	return o.building_type==building_type && o.team==team && o.under_construction==under_construction;
}

bool Entities::Building::can_change()
{
	return true;
}

Entities::EntityType Entities::Building::get_type()
{
	return Entities::EBuilding;
}

bool Entities::Building::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("Building");
	building_type = stream->readSint32("building_type");
	team = stream->readSint32("team");
	under_construction = stream->readUint8("under_construction");
	stream->readLeaveSection();
	return true;
}

void Entities::Building::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("Building");
	stream->writeSint32(building_type, "building_type");
	stream->writeSint32(team, "team");
	stream->writeUint8(under_construction, "under_construction");
	stream->writeLeaveSection();
}


Entities::AnyTeamBuilding::AnyTeamBuilding(int team, bool under_construction) : team(team), under_construction(under_construction)
{
}

bool Entities::AnyTeamBuilding::is_entity(Map* map, int posx, int posy)
{
	int building_id=map->getBuilding(posx, posy);
	if(building_id==NOGBID)
		return false;
	int team_id=::Building::GIDtoTeam(building_id);
	if(team_id!=team)
		return false;
	::Building* b = map->game->teams[team_id]->myBuildings[::Building::GIDtoID(building_id)];
	return b->constructionResultState==::Building::NO_CONSTRUCTION || under_construction;
}

bool Entities::AnyTeamBuilding::operator==(const Entity& rhs) const
{
	if(typeid(rhs)!=typeid(Entities::AnyTeamBuilding))
		return false;
	const Entities::AnyTeamBuilding& o = static_cast<const Entities::AnyTeamBuilding&>(rhs);
	return o.team==team && o.under_construction==under_construction;
}

bool Entities::AnyTeamBuilding::can_change()
{
	return true;
}

Entities::EntityType Entities::AnyTeamBuilding::get_type()
{
	return Entities::EAnyTeamBuilding;
}

bool Entities::AnyTeamBuilding::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("AnyTeamBuilding");
	team = stream->readSint32("team");
	under_construction = stream->readUint8("under_construction");
	stream->readLeaveSection();
	return true;
}

void Entities::AnyTeamBuilding::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("AnyTeamBuilding");
	stream->writeSint32(team, "team");
	stream->writeUint8(under_construction, "under_construction");
	stream->writeLeaveSection();
}


Entities::AnyBuilding::AnyBuilding(bool under_construction) : under_construction(under_construction)
{
}

bool Entities::AnyBuilding::is_entity(Map* map, int posx, int posy)
{
	int building_id=map->getBuilding(posx, posy);
	if(building_id==NOGBID)
		return false;
	int team_id=::Building::GIDtoTeam(building_id);
	::Building* b = map->game->teams[team_id]->myBuildings[::Building::GIDtoID(building_id)];
	return b->constructionResultState==::Building::NO_CONSTRUCTION || under_construction;
}

bool Entities::AnyBuilding::operator==(const Entity& rhs) const
{
	if(typeid(rhs)!=typeid(Entities::AnyBuilding))
		return false;
	return static_cast<const Entities::AnyBuilding&>(rhs).under_construction==under_construction;
}

bool Entities::AnyBuilding::can_change()
{
	return true;
}

Entities::EntityType Entities::AnyBuilding::get_type()
{
	return Entities::EAnyBuilding;
}

bool Entities::AnyBuilding::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("AnyBuilding");
	under_construction = stream->readUint8("under_construction");
	stream->readLeaveSection();
	return true;
}

void Entities::AnyBuilding::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("AnyBuilding");
	stream->writeUint8(under_construction, "under_construction");
	stream->writeLeaveSection();
}
