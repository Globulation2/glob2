// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "echo/Echo.h"

using namespace AIEcho;
using namespace AIEcho::Gradients;


Entities::Water::Water()
{
}

bool Entities::Water::is_entity(Map* map, int posx, int posy)
{
	return map->isWater(posx, posy);
}

bool Entities::Water::operator==(const Entity& rhs) const
{
	return typeid(rhs)==typeid(Entities::Water);
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
	return x==posx && y==posy;
}

bool Entities::Position::operator==(const Entity& rhs) const
{
	if(typeid(rhs)!=typeid(Entities::Position))
		return false;
	const Entities::Position& o = static_cast<const Entities::Position&>(rhs);
	return o.x==x && o.y==y;
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
	return map->hasSand(posx, posy);
}

bool Entities::Sand::operator==(const Entity& rhs) const
{
	return typeid(rhs)==typeid(Entities::Sand);
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
