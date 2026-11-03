// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once


#include <list>
#include <SDL3/SDL_stdinc.h>
#include <unordered_map>
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

//! A unit a building has just produced or serviced, for the effects in
//! UnitEffects.h. Render-only, like the death animation; the team pointer only
//! colors it.
struct UnitEffectAnimation
{
	int x, y;          //!< tile the unit came out on, where dust and icons stay
	int age;           //!< ticks since it came out, captured as 0 on its first tick
	Team *team;
	Uint16 unitGid;
	Uint16 buildingGid;   //!< the building it came out of
	Sint32 buildingType;  //!< that building's type, whose map icon the effect shows
	Sint8 dx, dy;         //!< direction the unit leaves the building in
	Uint8 kind;           //!< UnitEffect::Kind
	Uint8 level;          //!< a trained unit's new displayed level, otherwise 0
};

//! Per-game render container for bullet explosions, unit death animations and
//! unit spawn effects.
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

	//! Record a unit produced by a swarm. No-op when disabled.
	void onUnitSpawn(const Map& map, int x, int y, Team *team, Uint16 unitGid,
		Uint16 buildingGid, int dx, int dy);

	//! A building healed, fed or trained a unit inside it (UnitEffect::Kind). The
	//! effect waits for the unit to come out. No-op when disabled.
	void onUnitServiced(Uint16 unitGid, Uint8 kind, Uint16 buildingGid, Sint32 buildingType, Uint8 level);
	//! A unit came out of a building at (x, y): plays its pending service effect.
	void onUnitLeftBuilding(const Map& map, Uint16 unitGid, int x, int y, int dx, int dy, Team *team);
	//! A unit died: forget any service effect it was waiting to play.
	void onUnitGone(Uint16 unitGid);

	//! Tick down ticksLeft on every stored animation and remove the finished
	//! ones. Called once per simulation tick from Map::syncStep.
	void step();

	//! Iteration access for the renderer.
	const std::list<BulletExplosion *>& getExplosions(int sectorIdx) const
		{ return sectorExplosions[sectorIdx]; }
	const std::list<UnitDeathAnimation *>& getDeathAnimations(int sectorIdx) const
		{ return sectorDeathAnimations[sectorIdx]; }
	const std::vector<UnitEffectAnimation>& getUnitEffects(int sectorIdx) const
		{ return sectorUnitEffects[sectorIdx]; }

private:
	bool enabled;
	std::vector<std::list<BulletExplosion *>> sectorExplosions;
	std::vector<std::list<UnitDeathAnimation *>> sectorDeathAnimations;
	std::vector<std::vector<UnitEffectAnimation>> sectorUnitEffects;
	//! Services awaiting the unit's exit, by unit gid. `age` counts the wait, so an
	//! entry a unit never collects cannot outlive PendingTicks.
	std::unordered_map<Uint16, UnitEffectAnimation> pendingServices;
	static constexpr int PendingTicks = 600;
};

