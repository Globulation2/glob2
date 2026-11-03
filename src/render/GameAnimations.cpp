// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière


#include "GameAnimations.h"

#include "GlobalContainer.h"
#include "SpawnEffects.h"
#include "map/Map.h"

UnitDeathAnimation::UnitDeathAnimation(int x, int y, Team *team)
{
	this->x = x;
	this->y = y;
	this->team = team;
	this->ticksLeft = globalContainer->deathAnimation->getFrameCount() - 1;
}

GameAnimations::GameAnimations(bool enabled, int sectorCount)
	: enabled(enabled)
{
	resize(sectorCount);
}

GameAnimations::~GameAnimations()
{
	clear();
}

void GameAnimations::resize(int sectorCount)
{
	clear();
	sectorExplosions.resize(sectorCount);
	sectorDeathAnimations.resize(sectorCount);
	sectorSpawnAnimations.resize(sectorCount);
}

void GameAnimations::clear()
{
	for (auto& bucket : sectorExplosions)
	{
		for (BulletExplosion *e : bucket)
			delete e;
		bucket.clear();
	}
	for (auto& bucket : sectorDeathAnimations)
	{
		for (UnitDeathAnimation *a : bucket)
			delete a;
		bucket.clear();
	}
	for (auto& bucket : sectorSpawnAnimations)
		bucket.clear();
}

void GameAnimations::onBulletImpact(const Map& map, int x, int y)
{
	if (!enabled)
		return;
	int idx = map.getSectorIndex(x, y);
	BulletExplosion *explosion = new BulletExplosion();
	explosion->x = x;
	explosion->y = y;
	explosion->ticksLeft = globalContainer->bulletExplosion->getFrameCount();
	sectorExplosions[idx].push_front(explosion);
}

void GameAnimations::onUnitDeath(const Map& map, int x, int y, Team *team)
{
	if (!enabled)
		return;
	int idx = map.getSectorIndex(x, y);
	sectorDeathAnimations[idx].push_back(new UnitDeathAnimation(x, y, team));
}

void GameAnimations::onUnitSpawn(const Map& map, int x, int y, Team *team, Uint16 unitGid,
	Uint16 buildingGid, int dx, int dy)
{
	if (!enabled)
		return;
	// step() runs later in the same tick, so the effect is first captured at age 0.
	sectorSpawnAnimations[map.getSectorIndex(x, y)].push_back(
		{x, y, -1, team, unitGid, buildingGid, Sint8(dx), Sint8(dy)});
}

void GameAnimations::step()
{
	if (!enabled)
		return;
	for (auto& bucket : sectorSpawnAnimations)
	{
		for (auto& spawn : bucket)
			++spawn.age;
		std::erase_if(bucket, [](const UnitSpawnAnimation& spawn) { return spawn.age >= SpawnEffect::LifetimeTicks; });
	}
	for (auto& bucket : sectorExplosions)
	{
		for (auto it = bucket.begin(); it != bucket.end(); )
		{
			if ((*it)->ticksLeft > 0)
			{
				(*it)->ticksLeft--;
				++it;
			}
			else
			{
				delete *it;
				it = bucket.erase(it);
			}
		}
	}
	for (auto& bucket : sectorDeathAnimations)
	{
		for (auto it = bucket.begin(); it != bucket.end(); )
		{
			if ((*it)->ticksLeft > 0)
			{
				(*it)->ticksLeft--;
				++it;
			}
			else
			{
				delete *it;
				it = bucket.erase(it);
			}
		}
	}
}

