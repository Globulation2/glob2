// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "echo/Echo.h"

using namespace AIEcho;
using namespace AIEcho::Gradients;


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

