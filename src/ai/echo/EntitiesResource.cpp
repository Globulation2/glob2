// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "echo/Echo.h"

using namespace AIEcho;
using namespace AIEcho::Gradients;


Entities::Ressource::Ressource(int ressource_type) : ressource_type(ressource_type)
{
}

bool Entities::Ressource::is_entity(Map* map, int posx, int posy)
{
	return map->isRessourceTakeable(posx, posy, ressource_type);
}

bool Entities::Ressource::operator==(const Entity& rhs) const
{
	if(typeid(rhs)!=typeid(Entities::Ressource))
		return false;
	return static_cast<const Entities::Ressource&>(rhs).ressource_type==ressource_type;
}

bool Entities::Ressource::can_change()
{
	return ressource_type==WOOD || ressource_type==CORN || ressource_type==ALGA;
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


Entities::AnyRessource::AnyRessource()
{
}

bool Entities::AnyRessource::is_entity(Map* map, int posx, int posy)
{
	return map->isRessource(posx, posy);
}

bool Entities::AnyRessource::operator==(const Entity& rhs) const
{
	return typeid(rhs)==typeid(Entities::AnyRessource);
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
