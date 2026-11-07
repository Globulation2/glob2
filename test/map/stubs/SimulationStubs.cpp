// SPDX-License-Identifier: GPL-3.0-or-later
// Simulation symbols that libglob2_map.a still references (the map -> sim welds that
// `python3 tools/link_graph.py --within map closure map/Map.o --external` lists).
// glob2-map-tests never attaches a Game, Team, Building or Unit to a map, so each of
// these is unreachable: they abort, and a test that reaches one belongs in
// glob2-sim-tests. Keep this list equal to the linker's need; delete an entry when the
// production reference goes away.

#include "AreaEffects.h"
#include "Building.h"
#include "Game.h"
#include "render/GameAnimations.h"
#include "Race.h"
#include "Sector.h"
#include "TeamStat.h"
#include "Unit.h"
#include "engine/sim/snapshot/WorldSnapshot.h"

#include <cstdlib>

// Sector holds bullets (sim). Map owns an array of them once a Game sizes it.
Sector::~Sector() { std::abort(); }
void Sector::setGame(Game*) { std::abort(); }
void Sector::step() { std::abort(); }

// Map hooks that run only when map.game is set.
void Game::invalidateObservation() { std::abort(); }
const SimulationSnapshot::Handle& Game::observation() const { std::abort(); }
bool Game::preparingBuildingGradient(const Building&, int) const { std::abort(); }
void GameAnimations::step() { std::abort(); }
void GameAnimations::resize(int) { std::abort(); }
void BuildingAreaEffects::Runtime::reset() { std::abort(); }
GameplayMeasurements& TeamStats::editMeasurements() { std::abort(); }

// Building and unit routing and claim queries, reached only through live entities.
BuildingRoute Building::resolveRoute(BuildingRoute) const { std::abort(); }
void Building::retireSharedGradient(int) { std::abort(); }
MovementAccess Unit::movementAccess() const { std::abort(); }
void Unit::directionFromDxDy() { std::abort(); }
bool Unit::activeMaterialClaimValid() { std::abort(); }
void Unit::releaseActiveMaterialClaim() { std::abort(); }

// The resource gradient probe captures a live Game.
namespace SimulationSnapshot
{
std::shared_ptr<const std::vector<BuildingKindView>> captureCatalog(const Game&) { std::abort(); }
Handle capture(const Game&, std::shared_ptr<const std::vector<BuildingKindView>>, Requirements, const Handle*,
               Storage*, bool) { std::abort(); }
}

// BaseTeam::load constructs a Race only for pre-73 streams, which no map test reads.
Race::Race() {}
Race::~Race() {}
bool Race::load(GAGCore::InputStream*, Sint32) { std::abort(); }
