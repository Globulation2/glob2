// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Bullet.h"
#include "Game.h"
#include "Sector.h"
#include "Unit.h"
#include "UnitConsts.h"
#include "BuildingType.h"
#include <Stream.h>

#ifndef YOG_SERVER_ONLY
#include "render/GameAnimations.h"
#endif  // !YOG_SERVER_ONLY

Sector::Sector(Game *game)
{
	this->game=game;
	this->map=&(game->map);
}

Sector::~Sector(void)
{
	free();
}

void Sector::setGame(Game *game)
{
	free();
	this->game=game;
	this->map=&(game->map);
}

void Sector::free(void)
{
	for (std::list<Bullet *>::iterator it=bullets.begin();it!=bullets.end();it++)
		delete (*it);
	bullets.clear();

	game=NULL;
	map=NULL;
}

void Sector::save(GAGCore::OutputStream *stream)
{
	stream->writeUint32((Uint32)bullets.size(), "bulletCount");
	stream->writeEnterSection("bullets");
	unsigned i = 0;
	for (std::list<Bullet *>::iterator it=bullets.begin();it!=bullets.end();it++)
	{
		stream->writeEnterSection(i++);
		(*it)->save(stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
}

bool Sector::load(GAGCore::InputStream *stream, Game *game, Sint32 versionMinor)
{
	// destroy all actual bullets
	free();
	// read the number of bullets
	Uint32 bulletCount = stream->readUint32("bulletCount");
	// read all the bullets
	stream->readEnterSection("bullets");
	for (Uint32 i=0; i<bulletCount; i++)
	{
		stream->readEnterSection(i);
		bullets.push_front(new Bullet(stream, versionMinor));
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	this->game=game;
	this->map=&(game->map);
	return true;
}

#ifndef YOG_SERVER_ONLY
void Sector::step(void)
{
	assert(map);
	assert(game);

	for (std::list<Bullet *>::iterator it=bullets.begin();it!=bullets.end();)
	{
		Bullet *bullet = (*it);
		if ( bullet->ticksLeft > 0 )
		{
			bullet->step();
			++it;
		}
		else
		{
			Uint16 gid = map->getGroundUnit(bullet->targetX, bullet->targetY);
			if(gid == NOGUID)
				gid = map->getAirUnit(bullet->targetX, bullet->targetY);
			if (gid != NOGUID)
			{
				// we have hit a unit
				int team = Unit::GIDtoTeam(gid);
				int id = Unit::GIDtoID(gid);


				game->teams[team]->pushGameEvent(GameEvent::unitUnderAttack(game->stepCounter, bullet->targetX, bullet->targetY, game->teams[team]->myUnits[id]->typeNum));

				if (bullet->revealW > 0 && bullet->revealH > 0)
					game->map.setMapDiscovered(bullet->revealX, bullet->revealY, bullet->revealW, bullet->revealH, Team::teamNumberToMask(team));

				int degats = bullet->shootDamage - game->teams[team]->myUnits[id]->getRealArmor(false);
				if (degats <= 0)
					degats = BULLET_MIN_DAMAGE;
				game->teams[team]->myUnits[id]->hp -= degats;
			}
			else
			{
				Uint16 gid = map->getBuilding(bullet->targetX, bullet->targetY);
				if (gid != NOGBID)
				{
					// we have hit a building
					int team = Building::GIDtoTeam(gid);
					int id = Building::GIDtoID(gid);

					if (bullet->revealW > 0 && bullet->revealH > 0)
						game->map.setMapDiscovered(bullet->revealX, bullet->revealY, bullet->revealW, bullet->revealH, Team::teamNumberToMask(team));

					Building *building = game->teams[team]->myBuildings[id];
					int damage = bullet->shootDamage-building->type->armor;

					game->teams[team]->pushGameEvent(GameEvent::buildingUnderAttack(game->stepCounter, bullet->targetX, bullet->targetY, building->shortTypeNum));

					if (damage > 0)
						building->hp -= damage;
					else
						building->hp -= BULLET_MIN_DAMAGE;
					if (building->hp <= 0)
						building->kill();
				}
			}

			game->animations->onBulletImpact(*map, bullet->targetX, bullet->targetY);

			// remove bullet
			delete bullet;
			it = bullets.erase(it);
		}
	}
}
#endif  // !YOG_SERVER_ONLY
