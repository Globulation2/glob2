/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

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
	this->game=game;
	this->map=&(game->map);
	bullets.clear();
}

void Sector::free(void)
{
	for (std::list<Bullet *>::iterator it=bullets.begin();it!=bullets.end();it++)
		delete (*it);
	
	bullets.clear();
	game=NULL;
	map=NULL;
}

void Sector::save(SDL_RWops *stream)
{
	SDL_WriteBE32(stream, bullets.size());
	// we write the number of bullets here
	
	for (std::list<Bullet *>::iterator it=bullets.begin();it!=bullets.end();it++)
		(*it)->save(stream);
	
}

bool Sector::load(SDL_RWops *stream, Game *game)
{
	int nbUsed;

	free();
	nbUsed=SDL_ReadBE32(stream);
	for (int i=0; i<nbUsed; i++)
	{
		bullets.push_front(new Bullet(stream));
	}
	
	this->game=game;
	this->map=&(game->map);
	return true;
}

void Sector::step(void)
{
	std::list<Bullet*>::iterator ittemp;
	assert(map);
	assert(game);
	
	for (std::list<Bullet *>::iterator it=bullets.begin();it!=bullets.end();++it)
	{
		if ( (*it)->ticksLeft > 0 )
		{
			(*it)->step();
		}
		else
		{
			Uint16 gid=map->getGroundUnit((*it)->targetX, (*it)->targetY);
			if (gid!=NOGUID)
			{
				int team=Unit::GIDtoTeam(gid);
				int id=Unit::GIDtoID(gid);

				game->teams[team]->setEvent((*it)->targetX, (*it)->targetY, Team::UNIT_UNDER_ATTACK_EVENT, gid);
				game->teams[team]->myUnits[id]->hp-=(*it)->shootDamage;
			}
			else
			{
				Uint16 gid=map->getBuilding((*it)->targetX, (*it)->targetY);
				if (gid!=NOGBID)
				{
					int team=Building::GIDtoTeam(gid);
					int id=Building::GIDtoID(gid);

					game->teams[team]->setEvent((*it)->targetX, (*it)->targetY, Team::BUILDING_UNDER_ATTACK_EVENT, gid);
					Building *b=game->teams[team]->myBuildings[id];
					int damage=(*it)->shootDamage-b->type->armor; 
					if (damage>0)
						b->hp-=damage;
					else
						b->hp--;
					if (b->hp<=0)
						b->kill();

					//printf("bullet hitted building %d \n", (int)b);
				}
			}

			ittemp=it;
			it=bullets.erase(ittemp);
		}
	}
}

