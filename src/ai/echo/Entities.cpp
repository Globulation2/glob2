// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "echo/Echo.h"
#include "Building.h"
#include <stack>
#include <queue>
#include <map>
#include <limits>
#include <algorithm>
#include "BuildingType.h"
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

