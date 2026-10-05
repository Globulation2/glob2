// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Bullet.h"
#include "FileFormatVersions.h"
#include <algorithm>
#include "Game.h"
#include "Sector.h"
#include "Unit.h"
#include "UnitConsts.h"
#include "BuildingType.h"
#include <Stream.h>

#include "render/GameAnimations.h"

Sector::Sector(Game *game)
{
	this->game=game;
	this->map=game ? &(game->map) : nullptr;
}

Sector::~Sector(void)
{
	free();
}

void Sector::setGame(Game *game)
{
	free();
	this->game=game;
	this->map=game ? &(game->map) : nullptr;
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
	Uint32 bulletCount = stream->readCount("bulletCount");
	// read all the bullets
	stream->readEnterSection("bullets");
	for (Uint32 i=0; i<bulletCount; i++)
	{
		stream->readEnterSection(i);
		Bullet* bullet = new Bullet(stream, versionMinor);
		if (versionMinor >= FILE_FORMAT_VERSION_BUILDING_CATALOG) bullets.push_back(bullet);
		else bullets.push_front(bullet);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	this->game=game;
	this->map=game ? &(game->map) : nullptr;
	return true;
}

void Sector::step(void)
{
	assert(map);
	assert(game);

	for (std::list<Bullet *>::iterator it=bullets.begin();it!=bullets.end();)
	{
		Bullet *bullet = (*it);
		if (map->hasProjectileBlockingTerrain() &&
			(!map->projectilePathClear(bullet->px, bullet->py,
				bullet->px + (bullet->ticksLeft > 0 ? bullet->speedX : 0),
				bullet->py + (bullet->ticksLeft > 0 ? bullet->speedY : 0)) ||
			 (bullet->ticksLeft == 0 && map->terrainPropertiesAt(bullet->targetX,bullet->targetY).projectileBlocks)))
		{
			delete bullet;
			it = bullets.erase(it);
			continue;
		}
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

				const Unit* target = game->teams[team]->myUnits[id];
				const int baseDamage = bullet->unitDamage[target->typeNum];
				const int damage = baseDamage > 0 ? std::max(BULLET_MIN_DAMAGE, baseDamage - target->getRealArmor(false)) : 0;
				Unit *victim = game->teams[team]->myUnits[id];
				TeamStats::recordDamage(bullet->sourceTeam >= 0 ? game->teams[bullet->sourceTeam]
																: nullptr,
										victim->owner, GameplayMeasurements::TOWER,
										GameplayMeasurements::UNIT, victim->hp, damage);
				victim->recordLethalDamage(damage, GameplayMeasurements::COMBAT);
				game->teams[team]->myUnits[id]->hp -= damage;
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
					const int damage = bullet->shootDamage > 0 ? std::max(BULLET_MIN_DAMAGE, bullet->shootDamage-building->type->armor) : 0;

					game->teams[team]->pushGameEvent(GameEvent::buildingUnderAttack(game->stepCounter, bullet->targetX, bullet->targetY, building->typeNum));

					TeamStats::recordDamage(
						bullet->sourceTeam >= 0 ? game->teams[bullet->sourceTeam] : nullptr,
						building->owner, GameplayMeasurements::TOWER,
						GameplayMeasurements::BUILDING, building->hp,
						damage);
					building->hp -= damage;
					if (building->hp <= 0)
						building->kill(GameplayMeasurements::DESTROYED);
				}
			}

			game->animations->onBulletImpact(*map, bullet->targetX, bullet->targetY);

			// remove bullet
			delete bullet;
			it = bullets.erase(it);
		}
	}
}
