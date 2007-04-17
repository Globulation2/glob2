/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
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

UnitDeathAnimation::UnitDeathAnimation(int x, int y, Team *team)
{
	this->x = x;
	this->y = y;
	this->team = team;
	this->ticksLeft = globalContainer->deathAnimation->getFrameCount() - 1;
}

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
	
	for (std::list<UnitDeathAnimation *>::iterator it=deathAnimations.begin();it!=deathAnimations.end();it++)
		delete (*it);
	deathAnimations.clear();
	
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

void Sector::step(void)
{
	assert(map);
	assert(game);
	
	for (std::list<Bullet *>::iterator it=bullets.begin();it!=bullets.end();++it)
	{
		Bullet *bullet = (*it);
		if ( bullet->ticksLeft > 0 )
		{
			bullet->step();
		}
		else
		{
			Uint16 gid = map->getGroundUnit(bullet->targetX, bullet->targetY);
			Uint16 airgid = map->getAirUnit(bullet->targetX, bullet->targetY);
			if (gid != NOGUID)
			{
				// we have hit a unit
				int team = Unit::GIDtoTeam(gid);
				int id = Unit::GIDtoID(gid);
				
				game->teams[team]->setEvent(bullet->targetX, bullet->targetY, Team::UNIT_UNDER_ATTACK_EVENT, gid, team);
				if (bullet->revealW > 0 && bullet->revealH > 0)
					game->map.setMapDiscovered(bullet->revealX, bullet->revealY, bullet->revealW, bullet->revealH, Team::teamNumberToMask(team));
				
				int degats = bullet->shootDamage - game->teams[team]->myUnits[id]->getRealArmor(false);
				if (degats <= 0)
					degats = 1;
				game->teams[team]->myUnits[id]->hp -= degats;
			}
			//following maybe should be merged into the above?
			else if (airgid != NOGUID)
			{
				// we have hit an air unit
				// yes, it hits ground units, then air units, then buildings
				// totally illogical, but matches the preference of guard towers
				// (ugh, hard-coded preference...)
				int team = Unit::GIDtoTeam(airgid);
				int id = Unit::GIDtoID(airgid);
				
				game->teams[team]->setEvent(bullet->targetX, bullet->targetY, Team::UNIT_UNDER_ATTACK_EVENT, airgid, team);
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

					game->teams[team]->setEvent(bullet->targetX, bullet->targetY, Team::BUILDING_UNDER_ATTACK_EVENT, gid, team);
					if (bullet->revealW > 0 && bullet->revealH > 0)
						game->map.setMapDiscovered(bullet->revealX, bullet->revealY, bullet->revealW, bullet->revealH, Team::teamNumberToMask(team));
					
					Building *building = game->teams[team]->myBuildings[id];
					int damage = bullet->shootDamage-building->type->armor; 
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
	for (std::list<BulletExplosion *>::iterator it=explosions.begin();it!=explosions.end();++it)
	{
		if ( (*it)->ticksLeft > 0 )
		{
			(*it)->ticksLeft--;
		}
		else
		{
			delete *it;
			it = explosions.erase(it);
		}
	}
	
	// handle death animation
	for (std::list<UnitDeathAnimation *>::iterator it=deathAnimations.begin();it!=deathAnimations.end();++it)
	{
		if ( (*it)->ticksLeft > 0 )
		{
			(*it)->ticksLeft--;
		}
		else
		{
			delete *it;
			it = deathAnimations.erase(it);
		}
	}
}

