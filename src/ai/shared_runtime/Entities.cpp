// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "shared_runtime/Runtime.h"
#include <memory>

using namespace AISharedRuntime;
using namespace AISharedRuntime::Gradients;


Entities::Entity* Entities::Entity::load_entity(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	GAGCore::InputStream::NestedRead nesting(*stream);
	stream->readEnterSection("Entity");
	const Uint32 type=stream->readUint32("type");
	std::unique_ptr<Entity> entity;
	switch(type)
	{
		case Entities::EBuilding:        entity.reset(new Entities::Building); break;
		case Entities::EAnyTeamBuilding: entity.reset(new Entities::AnyTeamBuilding); break;
		case Entities::EAnyBuilding:     entity.reset(new Entities::AnyBuilding); break;
		case Entities::EMaterialSources: entity.reset(new Entities::MaterialSources); break;
		case Entities::EMaterialSource:       entity.reset(new Entities::MaterialSource); break;
        case Entities::EResourceGroundObstacle: entity.reset(new Entities::ResourceGroundObstacle); break;
        case Entities::EResourceBuildingObstacle: entity.reset(new Entities::ResourceBuildingObstacle); break;
		case Entities::EAnyResource:    entity.reset(new Entities::AnyResource); break;
		case Entities::EUnwalkable:      entity.reset(new Entities::Unwalkable); break;
		case Entities::EWater:           entity.reset(new Entities::Water); break;
		case Entities::EPosition:        entity.reset(new Entities::Position); break;
		case Entities::ESand:            entity.reset(new Entities::Sand); break;
	};
	if(entity)
		if (!entity->load(stream, player, versionMinor)) throw std::runtime_error("Invalid saved AI object");
	stream->readLeaveSection();
	if (!entity) throw std::runtime_error("Unknown saved AI object type");
	return entity.release();
}



void Entities::Entity::save_entity(Entity* entity, GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("Entity");
	stream->writeUint32(entity->get_type(), "type");
	entity->save(stream);
	stream->writeLeaveSection();
}
