// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Stubs for symbols referenced by Map.o that the unit tests never exercise. The Sector
// class is the worst offender: pulling in the real Sector.cpp drags in Bullet,
// GameEvent, Team::pushGameEvent, Building::kill, Unit::getRealArmor and
// globalContainer, most of the game. Map::setSize() (which would construct a real
// Sector[]) and setGame() are bypassed by the GrassMap fixture in UnitFixtures.h.

#include <GAGSys.h>
#include <Stream.h>
#include "Sector.h"
#include "render/GameAnimations.h"

Sector::Sector(Game *) {}
Sector::~Sector(void) {}
void Sector::setGame(Game *) {}
void Sector::free(void) {}
void Sector::step(void) {}
void Sector::save(GAGCore::OutputStream *) {}
bool Sector::load(GAGCore::InputStream *, Game *, Sint32) { return false; }

UnitDeathAnimation::UnitDeathAnimation(int x_, int y_, Team *t)
	: x(x_), y(y_), ticksLeft(0), team(t) {}

// Map::setGame calls animations->resize(); never reached, but Map.o links the symbol.
void GameAnimations::resize(int) {}

// Lightweight Map fixtures never allocate forbidden fields. Fail loudly if a
// future test reaches this engine-only refresh; those tests belong in engine.
#include "Map.h"
#include <cstdlib>
void Map::updateForbiddenGradient(int, int) { std::abort(); }

// Supplier discovery is exercised by the real-engine catalog fixtures. Unit map
// fixtures have no teams/buildings and must not accidentally test a fake balance.
#include "Building.h"
Sint32 Building::availableMaterial(int) const { std::abort(); }

unsigned Map::materialSupplyModesSlot(const Building*, int) const { std::abort(); }
bool Map::stockSupplierEligibleSlot(const Building*, const Building*, int, unsigned) const { std::abort(); }

// Unit Map fixtures have no Game tick or deferred preparation. Preserve real
// completed-job draining for terrain import, and fail if an engine reservation
// reaches this test double; that lifecycle is covered by engine harnesses.
#include "gradient/GradientRuntime.h"
void Map::preparePendingGradient() {
    if (gradientRuntime->preparation.job) std::abort();
}
void Map::finishGradientPipeline() {
    preparePendingGradient();
    gradientRuntime->pipeline.finish();
}
// Unit fixtures never schedule building fields; clearing only drops storage.
void Map::resetBuildingGradientPipeline() noexcept {
    gradientRuntime->stagedBuildings.clear();
    gradientRuntime->buildingRequests.clear();
    gradientRuntime->buildings.reset();
}


// Growth scheduling belongs to engine fixtures. Lightweight maps never submit
// jobs; reject accidental use rather than silently simulating a fake pipeline.
void Map::finishResourceGrowth() {
    if (gradientRuntime->growth.count() || gradientRuntime->growth.needsPreparation()) std::abort();
}
void ResourceGrowth::Pipeline::reset() noexcept {
    if (!pending.empty() || reservation) std::abort();
    spare.clear();
    executor = nullptr;
    proposalReserve = 0;
    metrics = {};
    delay = 8;
}

#include "AreaEffects.h"
// Standalone map tests never attach a Game; satisfy Map::clear's optional
// simulation-owned sidecar reset without linking the complete game.
void BuildingAreaEffects::Runtime::reset() { std::abort(); }
