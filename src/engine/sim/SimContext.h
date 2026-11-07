// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <SDL3/SDL_stdinc.h>

class BuildingsTypes;
class GameAnimations;
class GameHeader;
class Map;
class MapHeader;
class Player;
class SimServices;
class Team;
class UnitCargoStore;
namespace BuildingAreaEffects { class Runtime; }

//! The parts of a Game that team, building and unit code reads every tick,
//! as plain pointers into the owning Game. Game fills one when it creates a
//! Team (Game::simContext()) and Team keeps a copy, so entity code reaches
//! this state through `owner->ctx.x` with the same loads as `owner->game->x`
//! without including Game.h. Each pointer targets a Game member whose address
//! is fixed for the Game's lifetime. Never serialized or checksummed.
//!
//! This header names types only, so any layer may include it; it carries a
//! rank override in test/header_layers.json for Team.h. Rare calls into the
//! game go through `services` (sim/SimServices.h).
struct SimContext
{
	const GameHeader *gameHeader = nullptr;
	const MapHeader *mapHeader = nullptr;
	Map *map = nullptr;
	BuildingsTypes *buildingsTypes = nullptr;
	UnitCargoStore *unitCargo = nullptr;
	BuildingAreaEffects::Runtime *areaEffects = nullptr;
	//! Game::animations (render-side bullet and death effects).
	GameAnimations *animations = nullptr;
	const Uint32 *stepCounter = nullptr;
	//! Game::teams and Game::players, Team::MAX_COUNT entries each.
	Team **teams = nullptr;
	Player **players = nullptr;
	SimServices *services = nullptr;

	Uint32 tick() const { return *stepCounter; }
};
