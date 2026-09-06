// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "echo/Echo.h"

using namespace AIEcho;
using namespace AIEcho::Gradients;


Entities::Resource::Resource(int resource_type) : resource_type(resource_type)
{
}

bool Entities::Resource::is_entity(Map* map, int posx, int posy)
{
	return map->isResourceTakeable(posx, posy, resource_type);
}

bool Entities::Resource::operator==(const Entity& rhs) const
{
	if(typeid(rhs)!=typeid(Entities::Resource))
		return false;
	return static_cast<const Entities::Resource&>(rhs).resource_type==resource_type;
}

bool Entities::Resource::can_change()
{
	return resource_type==WOOD || resource_type==CORN || resource_type==ALGA;
}

Entities::EntityType Entities::Resource::get_type()
{
	return Entities::EResource;
}

bool Entities::Resource::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("Ressource");
	resource_type = stream->readSint32("ressource_type");
	stream->readLeaveSection();
	return true;
}

void Entities::Resource::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("Ressource");
	stream->writeSint32(resource_type, "ressource_type");
	stream->writeLeaveSection();
}


Entities::AnyResource::AnyResource()
{
}

bool Entities::AnyResource::is_entity(Map* map, int posx, int posy)
{
	return map->isResource(posx, posy);
}

bool Entities::AnyResource::operator==(const Entity& rhs) const
{
	return typeid(rhs)==typeid(Entities::AnyResource);
}

bool Entities::AnyResource::can_change()
{
	return true;
}

Entities::EntityType Entities::AnyResource::get_type()
{
	return Entities::EAnyResource;
}

bool Entities::AnyResource::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("AnyRessource");
	stream->readLeaveSection();
	return true;
}

void Entities::AnyResource::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("AnyRessource");
	stream->writeLeaveSection();
}
