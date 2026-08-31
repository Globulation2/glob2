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
		case Entities::EBuilding:        entity = new Entities::Building; break;
		case Entities::EAnyTeamBuilding: entity = new Entities::AnyTeamBuilding; break;
		case Entities::EAnyBuilding:     entity = new Entities::AnyBuilding; break;
		case Entities::ERessource:       entity = new Entities::Ressource; break;
		case Entities::EAnyRessource:    entity = new Entities::AnyRessource; break;
		case Entities::EWater:           entity = new Entities::Water; break;
		case Entities::EPosition:        entity = new Entities::Position; break;
		case Entities::ESand:            entity = new Entities::Sand; break;
	};
	if(entity)
		entity->load(stream, player, versionMinor);
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

