// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "AIEcho.h"
#include "Building.h"
#include <stack>
#include <queue>
#include <map>
#include <limits>
#include <algorithm>
#include "building_type.h"
#include "IntBuildingType.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Order.h"
#include <iterator>
#include "Utilities.h"
#include <tuple>
#include "Brush.h"

using namespace AIEcho;
using namespace AIEcho::Gradients;
using namespace AIEcho::Construction;
using namespace AIEcho::Management;
using namespace AIEcho::Conditions;
using namespace AIEcho::SearchTools;
using namespace boost::logic;
using std::shared_ptr;


Entities::Entity* Entities::Entity::load_entity(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("Entity");
	EntityType type = static_cast<EntityType>(stream->readUint32("type"));
	Entity* entity = NULL;
	switch(type)
	{
		case Entities::EBuilding:
			entity = new Entities::Building;
			entity->load(stream, player, versionMinor);
		break;
		case Entities::EAnyTeamBuilding:
			entity = new Entities::AnyTeamBuilding;
			entity->load(stream, player, versionMinor);
		break;
		case Entities::EAnyBuilding:
			entity = new Entities::AnyBuilding;
			entity->load(stream, player, versionMinor);
		break;
		case Entities::ERessource:
			entity = new Entities::Ressource;
			entity->load(stream, player, versionMinor);
		break;
		case Entities::EAnyRessource:
			entity = new Entities::AnyRessource;
			entity->load(stream, player, versionMinor);
		break;
		case Entities::EWater:
			entity = new Entities::Water;
			entity->load(stream, player, versionMinor);
		break;
		case Entities::EPosition:
			entity = new Entities::Position;
			entity->load(stream, player, versionMinor);
		break;
		case Entities::ESand:
			entity = new Entities::Sand;
			entity->load(stream, player, versionMinor);
		break;
	};
	stream->readLeaveSection();
	return entity;
}



void Entities::Entity::save_entity(Entity* entity, GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("Entity");
	stream->writeUint32(entity->get_type(), "type");
	entity->save(stream);
	stream->writeLeaveSection();
}



Entities::Building::Building(int building_type, int team, bool under_construction) : building_type(building_type), team(team), under_construction(under_construction)
{

}


bool Entities::Building::is_entity(Map* map, int posx, int posy)
{
	int building_id=map->getBuilding(posx, posy);
	if(building_id!=NOGBID)
	{
		int team_id=::Building::GIDtoTeam(building_id);
		if(team_id==team &&
		   map->game->teams[team_id]->myBuildings[::Building::GIDtoID(building_id)]->typeNum==building_type &&
		   (map->game->teams[team_id]->myBuildings[::Building::GIDtoID(building_id)]->constructionResultState==::Building::NO_CONSTRUCTION || under_construction)
		   )
		{
			return true;
		}
	}
	return false;
}

bool Entities::Building::operator==(const Entity& rhs)
{
	if(typeid(rhs)==typeid(Entities::Building) &&
	   static_cast<const Entities::Building&>(rhs).building_type==building_type &&
	   static_cast<const Entities::Building&>(rhs).team==team &&
	   static_cast<const Entities::Building&>(rhs).under_construction==under_construction 
	    )
		return true;
	return false;
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
	if(building_id!=NOGBID)
	{
		int team_id=::Building::GIDtoTeam(building_id);
		if(team_id==team &&
		   (map->game->teams[team_id]->myBuildings[::Building::GIDtoID(building_id)]->constructionResultState==::Building::NO_CONSTRUCTION || under_construction)
		   )
		{
			return true;
		}
	}
	return false;
}



bool Entities::AnyTeamBuilding::operator==(const Entity& rhs)
{
	if(typeid(rhs)==typeid(Entities::AnyTeamBuilding) &&
	   static_cast<const Entities::AnyTeamBuilding&>(rhs).team==team &&
	   static_cast<const Entities::AnyTeamBuilding&>(rhs).under_construction==under_construction 
	    )
		return true;
	return false;
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
	if(building_id!=NOGBID)
	{
		int team_id=::Building::GIDtoTeam(building_id);
		if(map->game->teams[team_id]->myBuildings[::Building::GIDtoID(building_id)]->constructionResultState==::Building::NO_CONSTRUCTION || under_construction)
			return true;
	}
	return false;
}



bool Entities::AnyBuilding::operator==(const Entity& rhs)
{
	if(typeid(rhs)==typeid(Entities::AnyBuilding) &&
	   static_cast<const Entities::AnyBuilding&>(rhs).under_construction==under_construction
           )
		return true;
	return false;
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



Entities::Ressource::Ressource(int ressource_type) : ressource_type(ressource_type)
{

}



bool Entities::Ressource::is_entity(Map* map, int posx, int posy)
{
	if(map->isRessourceTakeable(posx, posy, ressource_type))
	{
		return true;
	}
	return false;
}



bool Entities::Ressource::operator==(const Entity& rhs)
{
	if(typeid(rhs)==typeid(Entities::Ressource) &&
	   static_cast<const Entities::Ressource&>(rhs).ressource_type==ressource_type
	    )
		return true;
	return false;
}



bool Entities::Ressource::can_change()
{
	if(ressource_type==WOOD || ressource_type==CORN || ressource_type==ALGA)
		return true;
	return false;
}



Entities::EntityType Entities::Ressource::get_type()
{
	return Entities::ERessource;
}



bool Entities::Ressource::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("Ressource");
	ressource_type = stream->readSint32("ressource_type");
	stream->readLeaveSection();
	return true;
}



void Entities::Ressource::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("Ressource");
	stream->writeSint32(ressource_type, "ressource_type");
	stream->writeLeaveSection();
}



Entities::AnyRessource:: AnyRessource()
{

}



bool Entities::AnyRessource:: is_entity(Map* map, int posx, int posy)
{
	if(map->isRessource(posx, posy))
	{
		return true;
	}
	return false;
}



bool Entities::AnyRessource::operator==(const Entity& rhs)
{
	if(typeid(rhs)==typeid(Entities::AnyRessource))
		return true;
	return false;
}



bool Entities::AnyRessource::can_change()
{
	return true;
}



Entities::EntityType Entities::AnyRessource::get_type()
{
	return Entities::EAnyRessource;
}



bool Entities::AnyRessource::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("AnyRessource");
	stream->readLeaveSection();
	return true;
}



void Entities::AnyRessource::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("AnyRessource");
	stream->writeLeaveSection();
}



Entities::Water::Water()
{

}



bool Entities::Water::is_entity(Map* map, int posx, int posy)
{
	if(map->isWater(posx, posy))
	{
		return true;
	}
	return false;
}



bool Entities::Water::operator==(const Entity& rhs)
{
	if(typeid(rhs)==typeid(Entities::Water))
		return true;
	return false;
}



bool Entities::Water::can_change()
{
	return false;
}



Entities::EntityType Entities::Water::get_type()
{
	return Entities::EWater;
}



bool Entities::Water::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("Water");
	stream->readLeaveSection();
	return true;
}



void Entities::Water::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("Water");
	stream->writeLeaveSection();
}


Entities::Position::Position(int x, int y) : x(x), y(y)
{

}


bool Entities::Position::is_entity(Map* map, int posx, int posy)
{
	if(x==posx && y==posy)
	{
		return true;
	}
	return false;
}


bool Entities::Position::operator==(const Entity& rhs)
{
	if(typeid(rhs)==typeid(Entities::Position) && 
	   static_cast<const Entities::Position&>(rhs).x==x &&
	   static_cast<const Entities::Position&>(rhs).y==y) 
		return true;
	return false;
}


bool Entities::Position::can_change()
{
	return false;
}


Entities::EntityType Entities::Position::get_type()
{
	return Entities::EPosition;
}


bool Entities::Position::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("Position");
	x=stream->readSint32("posX");
	y=stream->readSint32("posY");
	stream->readLeaveSection();
	return false;
}


void Entities::Position::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("Position");
	stream->writeSint32(x, "posX");
	stream->writeSint32(y, "posy");
	stream->writeLeaveSection();
}



Entities::Sand::Sand()
{

}



bool Entities::Sand::is_entity(Map* map, int posx, int posy)
{
	if(map->hasSand(posx, posy))
	{
		return true;
	}
	return false;
}



bool Entities::Sand::operator==(const Entity& rhs)
{
	if(typeid(rhs)==typeid(Entities::Sand))
		return true;
	return false;
}



bool Entities::Sand::can_change()
{
	return false;
}



Entities::EntityType Entities::Sand::get_type()
{
	return Entities::ESand;
}



bool Entities::Sand::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("Sand");
	stream->readLeaveSection();
	return true;
}



void Entities::Sand::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("Sand");
	stream->writeLeaveSection();
}


