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
