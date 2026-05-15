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
	if(map->isWater(posx, posy))
	{
		return true;
	}
	return false;
}



bool Entities::Water::operator==(const Entity& rhs) const
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


bool Entities::Position::operator==(const Entity& rhs) const
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



bool Entities::Sand::operator==(const Entity& rhs) const
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

