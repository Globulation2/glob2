// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#pragma once

#include <GAGSys.h>
#include <vector>

class Game;

/// Returns true if two ranges laid out on a wrapping (toroidal) map axis of
/// length `modulus` share at least one cell. A range is the `len` consecutive
/// cells starting at `start`, each taken modulo `modulus`; `start` may be
/// negative or exceed `modulus`. Lengths are in map tiles. An empty range
/// (len <= 0) overlaps nothing. A range at least `modulus` long covers the
/// whole axis and so overlaps every non-empty range.
inline bool wrappedRangesOverlap(int aStart, int aLen, int bStart, int bLen, int modulus)
{
	if (aLen <= 0 || bLen <= 0)
		return false;
	// Offset from a's start to b's start, normalised into [0, modulus).
	int d = (bStart - aStart) % modulus;
	if (d < 0)
		d += modulus;
	// They meet if b's first cell falls inside a, or a's first cell falls
	// inside b (i.e. b started before a and is long enough to reach it).
	return d < aLen || (modulus - d) < bLen;
}

/// One pending ghost: the building variant the player asked for, and the map
/// tile of its top-left corner.
struct GhostBuilding
{
	/// Index into globalContainer->buildingsTypes, resolved once by
	/// BuildingsTypes::getPlaceableTypeNum at the moment the placement was
	/// ordered. These IDs are positions in a const table, so they stay valid
	/// for the life of the process.
	Sint32 typeNum;
	/// Top-left corner of the footprint, in map tiles. Not wrapped — callers
	/// wrap on use.
	int x;
	int y;
};

///GameGUIGhostBuildingManager causes 'ghosts' of buildings to be drawn on the map in
///the time inbetween when the user clicks the button to construct a building, and when
///the building is actually constructed. This time is 0 for local games, but for online
///games it can be as high as 2 seconds with bad connections.
///
///This is a local display cache only: it is never serialized, never sent over the
///network, and takes no part in the simulation or its checksums.
class GameGUIGhostBuildingManager
{
public:
	///Constructs the manager
	GameGUIGhostBuildingManager(Game& game);

	///Adds the building to be drawn, and the x and y positions on the map.
	///typeNum must already be resolved by BuildingsTypes::getPlaceableTypeNum.
	///Storing the resolved variant rather than its name keeps every later query
	///off the name-lookup path.
	void addBuilding(Sint32 typeNum, int x, int y);

	///Returns true if there is a ghost building covering the given square
	bool isGhostBuilding(int x, int y, int w, int h);

	///Removes every ghost whose top-left corner is exactly (x, y). Ghosts are
	///keyed on position alone — the type is not compared — because the only
	///caller reconciles against an incoming OrderCreate, which identifies the
	///placement by its coordinates.
	void removeBuilding(int x, int y);

	///Draws to the map
	void drawAll(int viewportX, int viewportY, int localTeamNo);
private:
	Game& game;
	std::vector<GhostBuilding> buildings;
};
