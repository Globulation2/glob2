// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#ifndef YOG_SERVER_ONLY

#include <list>
#include <vector>

class Map;
class Team;

//! Visual aftermath of a bullet impact. Render-only — not in Map::checkSum,
//! not network-replicated, not serialized to save/replay files.
struct BulletExplosion
{
	int x, y, ticksLeft;
};

//! Visual fade-out played when a unit dies. Render-only — not in Map::checkSum,
//! not network-replicated, not serialized to save/replay files. The team
//! pointer is used purely to color the sprite.
struct UnitDeathAnimation
{
	UnitDeathAnimation(int x, int y, Team *team);
	int x, y, ticksLeft;
	Team *team;
};

//! Per-game render container for bullet explosions and unit death animations.
//!
//! These two effect lists used to live on Sector, alongside the sim-state
//! bullets list. That mixed render state with sim state and forced sim sites
//! (Sector::step, Unit::syncStep death path) to gate their pushes on
//! globalContainer->runNoX. Lifting them into this dedicated render container
//! lets sim code call onBulletImpact / onUnitDeath unconditionally; the
//! enabled flag below is the single point where the runNoX gate now lives.
//!
//! Storage is per-sector (one list per Map sector) to preserve the existing
//! per-sector flush pattern of GraphicContext::finishDrawingSprite in
//! Game::drawMapBulletsExplosionsDeathAnimations.
class GameAnimations
{
public:
	//! @param enabled false when running headless (--nox); the push methods
	//!        become no-ops and step() does nothing. Construction is still
	//!        cheap so the lifetime matches Game.
	//! @param sectorCount number of Map sectors (Map::getSectorW() *
	//!        Map::getSectorH()). May be 0 at construction; resize() is
	//!        called when the map header is set.
	GameAnimations(bool enabled, int sectorCount);
	~GameAnimations();

	GameAnimations(const GameAnimations&) = delete;
	GameAnimations& operator=(const GameAnimations&) = delete;

	//! Resize per-sector storage. Called whenever the map dimensions change.
	//! Frees any existing entries.
	void resize(int sectorCount);

	//! Free all stored animations without changing the sector count.
	void clear();

	//! Record a bullet impact for later rendering. No-op when disabled.
	//! @param map needed to map (x,y) to a sector index.
	void onBulletImpact(const Map& map, int x, int y);

	//! Record a unit death for later rendering. No-op when disabled.
	//! @param map needed to map (x,y) to a sector index.
	void onUnitDeath(const Map& map, int x, int y, Team *team);

	//! Tick down ticksLeft on every stored animation and remove the finished
	//! ones. Called once per simulation tick from Map::syncStep.
	void step();

	//! Iteration access for the renderer.
	const std::list<BulletExplosion *>& getExplosions(int sectorIdx) const
		{ return sectorExplosions[sectorIdx]; }
	const std::list<UnitDeathAnimation *>& getDeathAnimations(int sectorIdx) const
		{ return sectorDeathAnimations[sectorIdx]; }

private:
	bool enabled;
	std::vector<std::list<BulletExplosion *>> sectorExplosions;
	std::vector<std::list<UnitDeathAnimation *>> sectorDeathAnimations;
};

#endif  // !YOG_SERVER_ONLY
