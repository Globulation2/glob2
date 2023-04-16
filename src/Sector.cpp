/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "Bullet.h"
#include "Game.h"
#include "Sector.h"
#include "Unit.h"
#include "BuildingType.h"
#include "GlobalContainer.h"
#include <GraphicContext.h>
#include <Stream.h>

#ifndef YOG_SERVER_ONLY
UnitDeathAnimation::UnitDeathAnimation(int x, int y, Team *team)
{
	this->x = x;
	this->y = y;
	this->team = team;
	this->ticksLeft = globalContainer->deathAnimation->getFrameCount() - 1;
}
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
	
	for (std::list<BulletExplosion *>::iterator it=explosions.begin();it!=explosions.end();it++)
		delete (*it);
	explosions.clear();

#ifndef YOG_SERVER_ONLY
	for (std::list<UnitDeathAnimation *>::iterator it=deathAnimations.begin();it!=deathAnimations.end();it++)
		delete (*it);
	deathAnimations.clear();
#endif  // !YOG_SERVER_ONLY

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
				
				
				boost::shared_ptr<GameEvent> event(new UnitUnderAttackEvent(game->stepCounter, bullet->targetX, bullet->targetY, game->teams[team]->myUnits[id]->typeNum));
				game->teams[team]->pushGameEvent(event);
		
				if (bullet->revealW > 0 && bullet->revealH > 0)
					game->map.setMapDiscovered(bullet->revealX, bullet->revealY, bullet->revealW, bullet->revealH, Team::teamNumberToMask(team));
				
				int degats = bullet->shootDamage - game->teams[team]->myUnits[id]->getRealArmor(false);
				if (degats <= 0)
					degats = 1;
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
					
					boost::shared_ptr<GameEvent> event(new BuildingUnderAttackEvent(game->stepCounter, bullet->targetX, bullet->targetY, building->shortTypeNum));
					game->teams[team]->pushGameEvent(event);
					
					if (damage > 0)
						building->hp -= damage;
					else
						building->hp--;
					if (building->hp <= 0)
						building->kill();
				}
			}
			
			if (!globalContainer->runNoX)
			{
				// create new explosion
				BulletExplosion *explosion = new BulletExplosion();
				explosion->x = bullet->targetX;
				explosion->y = bullet->targetY;
				explosion->ticksLeft = globalContainer->bulletExplosion->getFrameCount();
				explosions.push_front(explosion);
			}

			// remove bullet
			delete bullet;
			it = bullets.erase(it);
		}
	}
	
	// handle explosions timeout
	for (std::list<BulletExplosion *>::iterator it=explosions.begin();it!=explosions.end();)
	{
		if ( (*it)->ticksLeft > 0 )
		{
			(*it)->ticksLeft--;
			++it;
		}
		else
		{
			delete *it;
			it = explosions.erase(it);
		}
	}

	// handle death animation
	for (std::list<UnitDeathAnimation *>::iterator it=deathAnimations.begin();it!=deathAnimations.end();)
	{
		if ( (*it)->ticksLeft > 0 )
		{
			(*it)->ticksLeft--;
			++it;
		}
		else
		{
			delete *it;
			it = deathAnimations.erase(it);
		}
	}
}
#endif  // !YOG_SERVER_ONLY

