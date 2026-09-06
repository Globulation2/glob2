// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Stubs for symbols referenced by Map.o that the MapQuery test doesn't actually
// exercise. The Sector class is the worst offender: pulling in the real
// Sector.cpp drags in Bullet, GameEvent, Team::pushGameEvent, Building::kill,
// Unit::getRealArmor, and globalContainer — most of the game. These stubs
// satisfy the linker without any of that.
//
// None of these stubs are called at runtime by MapQueryTest. setSize() (which
// would construct a real Sector[]) and setGame() (which would call setGame on
// each Sector) are deliberately bypassed by the GrassMap test fixture.

#include <GAGSys.h>
#include <Stream.h>
#include "Sector.h"
#include "Map.h"
#include "render/GameAnimations.h"

Sector::Sector(Game *) {}
Sector::~Sector(void) {}
void Sector::setGame(Game *) {}
void Sector::free(void) {}
#ifndef YOG_SERVER_ONLY
void Sector::step(void) {}
#endif
void Sector::save(GAGCore::OutputStream *) {}
bool Sector::load(GAGCore::InputStream *, Game *, Sint32) { return false; }

#ifndef YOG_SERVER_ONLY
UnitDeathAnimation::UnitDeathAnimation(int x_, int y_, Team *t)
	: x(x_), y(y_), ticksLeft(0), team(t) {}

// Stub for Map::setGame's animations->resize() call. MapQueryTest never invokes
// setGame (the GrassMap fixture bypasses it), but Map.o's symbol is still linked.
void GameAnimations::resize(int) {}
#endif

// Reached only through Map::ensureBuildingField, which the weighted-field
// test never calls (it seeds fields directly).
void Map::updateGlobalGradient(Building *, bool) {}

// Map::warpDistSquare lives in MapMisc.cpp, which pulls in globalContainer
// through dumpGradient. Same arithmetic, torus-wrapped per axis.
namespace
{
	Sint32 wrapDist1d(int a, int b, int n)
	{
		Sint32 d = (a - b) % n;
		if (d < 0) d = -d;
		return d < n - d ? d : n - d;
	}
}
Sint32 Map::warpDistSquare(int px, int py, int qx, int qy)
{
	Sint32 dx = wrapDist1d(px, qx, w);
	Sint32 dy = wrapDist1d(py, qy, h);
	return dx * dx + dy * dy;
}
