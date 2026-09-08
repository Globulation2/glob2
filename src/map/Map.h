// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2006 Bradley Arsenault

#pragma once

#include <list>
#include <optional>
#include <assert.h>

#include "Building.h"
#include "Ressource.h"
#include "Sector.h"
#include "Team.h"
#include "TerrainType.h"
#include "BitArray.h"

class Unit;

//! No global unit identifier. This value means there is no unit. Used at Tile::groundUnit or Tile::airUnit.
#define NOGUID 0xFFFF

//! No global building identifier. This value means there is no building. Used at Tile::building.
#define NOGBID 0xFFFF

class Map;
class Game;
class MapGenerationDescriptor;
class SessionGame;
class MapHeader;

//! 2D grid offset returned by Map's 3x3-neighborhood "doesTouch" queries.
//! dx and dy are each in {-1, 0, +1}.
struct Offset
{
	int dx;
	int dy;
};

// a 1x1 piece of map
struct Tile
{
	Uint16 terrain = 0; // default, not really meaningful.
	Uint16 building = NOGBID;

	Resource resource;

	Uint16 groundUnit = NOGUID;
	Uint16 airUnit = NOGUID;

	Uint32 forbidden = 0; // This is a mask, one bit by team, 1=forbidden, 0=allowed
	///The difference between forbidden zone and hidden forbidden zone is that hidden forbidden zone
	///is put there by the game engine and is not draw to the screen.
	Uint32 guardArea = 0; // This is a mask, one bit by team, 1=guard area, 0=normal
	Uint32 clearArea = 0; // This is a mask, one bit by team, 1=clear area, 0=normal

	Uint16 scriptAreas = 0; // This is also a mask. A single bit represents an area #n, on or off for the square
	Uint8 canResourcesGrow = 1; // This is a boolean, it represents whether resources are allowed to grow into this location.
	
	Uint16 fertility = 0; // This is a value that represents the fertility of this square, the chance that wheat will grow on it
};

/// Types of areas
enum AreaType
{
	ClearingArea = 0,
	ForbiddenArea,
	GuardArea
};


/*! Map, handle all physical localisations
	All size are given in 32x32 pixel cell, which is the basic game measurement unit.
	All functions are wrap-safe, excepted the one specified otherwise.
*/
class Map
{
public:
	//! Type of terrain (used for undermap)

	// === Tile geometry (cross-slice) ===
	//! Bit-shift converting a tile index to its top-left pixel coordinate
	//! (i.e. log2 of TILE_PX). Used by mapCaseToPixelCase and friends in
	//! MapView.cpp / TypeSteps.cpp.
	static constexpr int TILE_PIXEL_SHIFT = 5;
	//! Side length of one map tile in screen pixels (1 << TILE_PIXEL_SHIFT).
	static constexpr int TILE_PX = 32;
	//! Half-tile in pixels — used when centring sprites / bullets on a tile.
	static constexpr int HALF_TILE_PX = 16;

	//! Sentinel returned by Map::getTerrainType when the underlying terrain
	//! sprite ID does not fall in any of the registered terrain ranges
	//! (GRASS / SAND / WATER). Callers test for `< 0` / `== TERRAIN_TYPE_UNKNOWN`.
	static constexpr int TERRAIN_TYPE_UNKNOWN = -1;

	//! "Infinity" / "unvisited" sentinel for the A* algorithm's Uint16 cost fields
	//! (moveCost, totalCost). All real costs fit within 0..0xFFFE so 0xFFFF is safe.
	static constexpr Uint16 ASTAR_COST_INFINITY = static_cast<Uint16>(-1);

public:
	//! Map constructor
	Map();
	//! Map destructor
	virtual ~Map(void);
	//! Reset map and free arrays
	void clear();

	//! Reset map size to width = 2^wDec and height=2^hDec, and fill background with terrainType
	void setSize(int wDec, int hDec, TerrainType terrainType=WATER);
	// !This call is needed to use the Map!
	void setGame(Game *game);
	//! Load a map from a stream and relink with associated game
	bool load(GAGCore::InputStream *stream, MapHeader& header, Game *game=NULL);
	//! Save a map
	void save(GAGCore::OutputStream *stream);
	//! Write the per-team explored area. Saved games only; save() decides.
	void saveExploredArea(GAGCore::OutputStream *stream, int numberOfTeams);
	//! Read the section written by saveExploredArea into freshly allocated
	//! exploredArea arrays. With keep=false the data is consumed and dropped,
	//! for loads that have no game to attach it to.
	void loadExploredArea(GAGCore::InputStream *stream, int numberOfTeams, bool keep);
	
	// add & remove teams, used by the map editor and the random map generator
	// Have to be called *after* session.numberOfTeam has been changed.
	void addTeam(void);
	void removeTeam(void);

	//! Grow resources on map
	void growResources(void);
#ifndef YOG_SERVER_ONLY
	//! Do a step associated with map (grow resources and process bullets)
	void syncStep(Uint32 stepCounter);
#endif  // !YOG_SERVER_ONLY
	//! Switch the Fog of War bufferResourceType
	void switchFogOfWar(void);

	//! Return map width
	int getW(void) const { return w; }
	//! Return map height
	int getH(void) const { return h; }
	//! Return map width mask
	int getMaskW(void) const { return wMask; }
	//! Return map height maskint
	int getMaskH(void) const { return hMask; }
	//! Return map width shift 
	int getShiftW(void) const { return wDec; }
	//! Return map height shift
	int getShiftH(void) const { return hDec; }
	//! Return the number of sectors on x, which corresponds to the sector map width
	int getSectorW(void) const { return wSector; }
	//! Return the number of sectors on y, which corresponds to the sector map height
	int getSectorH(void) const { return hSector; }

	/// Return an index into map arrays for a given position, wrap-safe
	inline size_t coordToIndex(int x, int y) const {
		return ((y & hMask) << wDec) + (x & wMask);
	}

	///Returns a normalized version of the x coordinate, taking into account that x coordinates wrap around
	int normalizeX(int x) const
	{
		return x & wMask;
	}
	
	///Returns a normalized version of the y coordinate, taking into account that y coordinates wrap around
	int normalizeY(int y) const
	{
		return y & hMask;
	}

	//! Set map to discovered state at position (x, y) for all teams in sharedVision (mask).
	void setMapDiscovered(int x, int y, Uint32 sharedVision);

	//! Set map to discovered state at rect (x, y, w, h) for all teams in sharedVision (mask).
	void setMapDiscovered(int x, int y, int w, int h,  Uint32 sharedVision);

	//! Make the building at (x, y) visible for all teams in sharedVision (mask).
	void setMapBuildingsDiscovered(int x, int y, Uint32 sharedVision, Team *teams[Team::MAX_COUNT]);

	//! Make the building at rect (x, y, w, h) visible for all teams in sharedVision (mask).
	void setMapBuildingsDiscovered(int x, int y, int w, int h, Uint32 sharedVision, Team *teams[Team::MAX_COUNT]);
	
	//! Make the map at rect (x, y, w, h) explored by unit, i.e. to 255
	void setMapExploredByUnit(int x, int y, int w, int h, int team);
	
	//! Make the map at rect (x, y, w, h) explored by building, i.e. to minimum 2
	void setMapExploredByBuilding(int x, int y, int w, int h, int team);

	//! Set all map for all teams to undiscovered state
	void unsetMapDiscovered(void);

	//! Returns true if map is discovered at position (x,y) for a given vision mask.
	//! This mask represents which team's part of map we are allowed to see.
	bool isMapDiscovered(int x, int y, Uint32 visionMask) const
	{
		return ((mapDiscovered[coordToIndex(x, y)]) & visionMask) != 0;
	}
	
	//! Returns true if map is discovered at position (x1..x2,y1..y2) for a given vision mask.
	//! This mask represents which team's part of map we are allowed to see.
	bool isMapPartiallyDiscovered(int x1, int y1, int x2, int y2, Uint32 visionMask) const;

	//! Sets all map for all teams to discovered state
	void setMapDiscovered(void);

	//! Returns true if map is currently discovered at position (x,y) for a given vision mask.
	//! This mask represents which team's units and buildings we are allowed to see.
	bool isFOWDiscovered(int x, int y, int visionMask) const
	{
		return ((fogOfWar[coordToIndex(x, y)]) & visionMask) != 0;
	}
	
	//! Return true if (x,y) is forbidden in the locally-displayed team's overlay cache
	//! (i.e. the team computeDisplayedForbidden was last refreshed for). Render-only.
	bool isForbiddenInDisplayedView(int x, int y) const
	{
		return displayedForbiddenView.get(coordToIndex(x, y));
	}
	
	//! Returns true if the position(x, y) is a forbidden area for the given team
	bool isForbidden(int x, int y, Uint32 teamMask) const
	{
		return tiles[coordToIndex(x, y)].forbidden&teamMask;
	}

	//! Return true if (x,y) is a guard area in the locally-displayed team's overlay cache
	//! (i.e. the team computeDisplayedGuardArea was last refreshed for). Render-only.
	bool isGuardAreaInDisplayedView(int x, int y) const
	{
		return displayedGuardAreaView.get(coordToIndex(x, y));
	}
	
	//! Returns true if the position(x, y) is a guard area for the given team
	bool isGuardArea(int x, int y, Uint32 teamMask) const
	{
		return tiles[coordToIndex(x, y)].guardArea&teamMask;
	}

	//! Return true if (x,y) is a clear area in the locally-displayed team's overlay cache
	//! (i.e. the team computeDisplayedClearArea was last refreshed for). Render-only.
	bool isClearAreaInDisplayedView(int x, int y) const
	{
		return displayedClearAreaView.get(coordToIndex(x, y));
	}

	//! Returns true if the position(x, y) is a clear area for the given team
	bool isClearArea(int x, int y, Uint32 teamMask) const
	{
		return tiles[coordToIndex(x, y)].clearArea&teamMask;
	}
	
	// These rebuild the render-only displayed*View caches from the authoritative
	// tiles[] bits, and are only meaningful for the locally-displayed team (the one
	// whose areas are drawn on screen). They do not touch checkSum() state.
	//! Rebuild displayedForbiddenView from tiles[].forbidden for the given team.
	void computeDisplayedForbidden(int teamNumber);
	//! Rebuild displayedGuardAreaView from tiles[].guardArea for the given team.
	void computeDisplayedGuardArea(int teamNumber);
	//! Rebuild displayedClearAreaView from tiles[].clearArea for the given team.
	void computeDisplayedClearArea(int teamNumber);

	//! Sentinel for "no displayed team yet" — used before GameGUI::adjustLocalTeam has run.
	static constexpr Sint32 NO_DISPLAYED_TEAM = -1;
	//! Register the team whose view is currently displayed. Used only to decide whether
	//! to refresh the displayedForbiddenView / displayedGuardAreaView / displayedClearAreaView
	//! caches. This identity is per-client display state — not in checkSum() — and must be
	//! kept in sync with GameGUI's localTeamNo. Never branch sim/network state on it.
	void setDisplayedTeam(Sint32 teamNo) { displayedTeam = teamNo; }
	Sint32 getDisplayedTeam() const { return displayedTeam; }
	
	//! Return the tile at a given position
	inline Tile &getTile(int x, int y)
	{
		return tiles[coordToIndex(x, y)];
	}

	//! Return the const tile at a given position
	inline const Tile &getTile(int x, int y) const
	{
		return tiles[coordToIndex(x, y)];
	}

	//! Return the terrain for a given coordinate
	inline Uint16 getTerrain(int x, int y) const
	{
		return tiles[coordToIndex(x, y)].terrain;
	}
	
	//! Return the terrain for a given position in tile array
	inline Uint16 getTerrain(size_t pos) const
	{
		return tiles[pos].terrain;
	}

	//! Return the typeof terrain. If type is unregistered, returns unknown (-1).
	int getTerrainType(int x, int y) const
	{
		unsigned t = getTerrain(x, y);
		if (t<16)
			return GRASS;
		else if ((t>=128) && (t<128+16))
			return SAND;
		else if ((t>=256) && (t<256+16))
			return WATER;
		else
			return TERRAIN_TYPE_UNKNOWN;
	}

	const Resource& getResource(int x, int y) const
	{
		return tiles[coordToIndex(x, y)].resource;
	}

	const Resource& getResource(size_t pos) const
	{
		return tiles[pos].resource;
	}

	Resource& getResource(int x, int y)
	{
		return tiles[coordToIndex(x, y)].resource;
	}
	
	Resource& getResource(size_t pos)
	{
		return tiles[pos].resource;
	}
	
	//Returns the combined forbidden and hidden forbidden masks
	Uint32 getForbidden(int x, int y) const
	{
		return tiles[coordToIndex(x, y)].forbidden;
	}
	
	Uint8 getExplored(int x, int y, int team) const
	{
		return exploredArea[team][coordToIndex(x, y)];
	}
	
	void setTerrain(int x, int y, Uint16 terrain)
	{
		tiles[coordToIndex(x, y)].terrain = terrain;
	}
	
	void setForbidden(int x, int y, Uint32 forbidden)
	{
		tiles[coordToIndex(x, y)].forbidden = forbidden;
	}
	
	void addForbidden(int x, int y, Uint32 teamNum)
	{
		tiles[coordToIndex(x, y)].forbidden |=  Team::teamNumberToMask(teamNum);
	}

	void removeForbidden(int x, int y, Uint32 teamNum)
	{
		Tile& c=tiles[coordToIndex(x, y)];
		c.forbidden ^= c.forbidden &  Team::teamNumberToMask(teamNum);
	}
	
	void addClearArea(int x, int y, Uint32 teamNum)
	{
		tiles[coordToIndex(x, y)].clearArea |=  Team::teamNumberToMask(teamNum);
	}
	
	void addGuardArea(int x, int y, Uint32 teamNum)
	{
		tiles[coordToIndex(x, y)].guardArea |=  Team::teamNumberToMask(teamNum);
	}

	
	bool isWater(int x, int y) const
	{
		int t = getTerrain(x, y)-256;
		return ((t>=0) && (t<16));
	}
	
	bool isWater(unsigned pos) const
	{
		int t = getTerrain(pos)-256;
		return ((t>=0) && (t<16));
	}

	bool isGrass(int x, int y) const
	{
		return (getTerrain(x, y)<16);
	}
	
	bool isGrass(unsigned pos) const
	{
		return (getTerrain(pos)<16);
	}
	
	bool isSand(int x, int y) const
	{
		int t=getTerrain(x, y);
		return ((t>=128)&&(t<128+16));
	}
	
	bool hasSand(int x, int y) const
	{
		int t=getTerrain(x, y);
		return ((t>=16)&&(t<=255));
	}

	bool isResource(int x, int y) const
	{
		return getTile(x, y).resource.type != NO_RES_TYPE;
	}

	bool isResourceTakeable(int x, int y, int resourceType) const
	{
		const Resource &resource = getTile(x, y).resource;
		return (resource.type == resourceType && resource.amount > 0);
	}

	bool isResourceTakeable(int x, int y, bool resourceTypes[BASIC_COUNT]) const
	{
		const Resource &resource = getTile(x, y).resource;
		return (resource.type != NO_RES_TYPE
			&& resource.amount > 0
			&& resource.type < BASIC_COUNT
			&& resourceTypes[resource.type]);
	}

	bool isResource(int x, int y, int *resourceType) const
	{
		const Resource &resource = getTile(x, y).resource;
		if (resource.type == NO_RES_TYPE)
			return false;
		*resourceType = resource.type;
		return true;
	}

	bool canResourcesGrow(int x, int y) const
	{
		return getTile(x, y).canResourcesGrow;
	}

	//! Decrement resource at position (x,y). Return true on success, false otherwise.
	void decResource(int x, int y);
	//! Decrement resource at position (x,y) if resource type = resourceType. Return true on success, false otherwise.
	void decResource(int x, int y, int resourceType);
	bool incResource(int x, int y, int resourceType, int variety);

private:
	//! Per-tile predicate driver shared by isFree*/isHardSpace*.
	//! Each flag toggles whether one occupancy/terrain test contributes to rejection.
	struct TileChecks {
		bool noResource    : 1; //!< reject if a resource sits on the tile
		bool noUnit         : 1; //!< reject if a ground unit sits on the tile
		bool waterBlocks    : 1; //!< reject water tiles unless canSwim is true
		bool requireGrass   : 1; //!< reject any tile whose terrain isn't grass
		bool checkForbidden : 1; //!< reject if the tile's forbidden mask intersects teamMask
	};
	//! Returns true iff (x,y) passes every enabled check. A building whose gid
	//! equals ignoreGid is treated as not present (used by the gid-tolerant
	//! isFreeForBuilding/isHardSpaceForBuilding overloads); pass NOGBID to make
	//! every occupant building reject.
	bool checkTile(int x, int y, TileChecks c, bool canSwim,
	               Uint32 teamMask, Uint16 ignoreGid) const;

public:
	//! Return true if unit can go to position (x,y)
	bool isFreeForGroundUnit(int x, int y, bool canSwim, Uint32 teamMask) const;
	bool isFreeForGroundUnitNoForbidden(int x, int y, bool canSwim) const;
	bool isFreeForAirUnit(int x, int y) const { return (getAirUnit(x+w, y+h)==NOGUID); }
	bool isFreeForBuilding(int x, int y) const;
	bool isFreeForBuilding(int x, int y, int w, int h) const;
	bool isFreeForBuilding(int x, int y, int w, int h, Uint16 gid) const;
	// The "hardSpace" keyword means "Free" but you don't count Ground-Units as obstacles.
	bool isHardSpaceForGroundUnit(int x, int y, bool canSwim, Uint32 me) const;
	bool isHardSpaceForBuilding(int x, int y) const;
	bool isHardSpaceForBuilding(int x, int y, int w, int h) const;
	bool isHardSpaceForBuilding(int x, int y, int w, int h, Uint16 gid) const;
	
	//! Return contact direction (dx, dy) if unit touches building gbid; nullopt otherwise.
	std::optional<Offset> doesUnitTouchBuilding(Unit *unit, Uint16 gbid) const;
	//! Return contact direction (dx, dy) if (x, y) touches building gbid; nullopt otherwise.
	std::optional<Offset> doesPosTouchBuilding(int x, int y, Uint16 gbid) const;

	//! Return contact direction (dx, dy) if unit touches a resource of any type; nullopt otherwise.
	std::optional<Offset> doesUnitTouchResource(Unit *unit) const;
	//! Return contact direction (dx, dy) if unit touches a resource of the given type; nullopt otherwise.
	std::optional<Offset> doesUnitTouchResource(Unit *unit, int resourceType) const;
	//! Return contact direction (dx, dy) if (x, y) touches a resource of the given type; nullopt otherwise.
	std::optional<Offset> doesPosTouchResource(int x, int y, int resourceType) const;
	//! Return contact direction (dx, dy) if unit touches an enemy; nullopt otherwise.
	std::optional<Offset> doesUnitTouchEnemy(Unit *unit) const;

	//! Sets this particular clearing area location as claimed
	void setClearingAreaClaimed(int x, int y, int teamNumber, int gid);
	//! Sets this particular clearing area location as unclaimed
	void setClearingAreaUnclaimed(int x, int y, int teamNumber);
	//! Returns the gid if this clearing area is claimed, NOGUID otherwise
	int isClearingAreaClaimed(int x, int y, int teamNumber) const;

	//! Marks a particular square as containing an immobile unit
	void markImmobileUnit(int x, int y, int teamNumber);
	//! Clears a particular square of having an immobile unit
	void clearImmobileUnit(int x, int y);
	//! Returns true if theres an immobile unit on the square
	bool isImmobileUnit(int x, int y) const;
	//! Returns the team number of the immobile unit on the given square, 255 for none
	Uint8 getImmobileUnit(int x, int y) const;

	//! Return GID
	Uint16 getGroundUnit(int x, int y) const { return tiles[coordToIndex(x, y)].groundUnit; }
	Uint16 getAirUnit(int x, int y) const { return tiles[coordToIndex(x, y)].airUnit; }
	Uint16 getBuilding(int x, int y) const { return tiles[coordToIndex(x, y)].building; }
	
	void setGroundUnit(int x, int y, Uint16 guid) { tiles[coordToIndex(x, y)].groundUnit = guid; }
	void setAirUnit(int x, int y, Uint16 guid) { tiles[coordToIndex(x, y)].airUnit = guid; }
	void setBuilding(int x, int y, int w, int h, Uint16 gbid)
	{
		for (int yi=y; yi<y+h; yi++)
			for (int xi=x; xi<x+w; xi++)
				tiles[coordToIndex(xi, yi)].building = gbid;
	}
	
	//! Return the sector index of the sector containing tile (x,y). The
	//! formula is: y is wrapped to the map height, divided by SECTOR_TILES
	//! to get the sector row, then multiplied by sector-grid width and
	//! offset by the wrapped/divided x. Used by Map::getSector and by
	//! GameAnimations to bucket render effects per sector.
	int getSectorIndex(int x, int y) const { return wSector*((y&hMask)>>Sector::SECTOR_SHIFT)+((x&wMask)>>Sector::SECTOR_SHIFT); }
	//! Return sector at (x,y).
	Sector *getSector(int x, int y) { return &(sectors[getSectorIndex(x, y)]); }
	//! Return a sector in the sector array. It is not clean because too high level
	Sector *getSector(int i) { assert(i>=0); assert(i<sizeSector); return sectors+i; }

	//! Set undermap terrain type at (x,y) (undermap positions)
	void setUMTerrain(int x, int y, TerrainType t) { undermap[coordToIndex(x, y)] = (Uint8)t; }
	//! Return undermap terrain type at (x,y)
	TerrainType getUMTerrain(int x, int y) const { return (TerrainType)undermap[coordToIndex(x, y)]; }
	//! Set undermap terrain type at (x,y) (undermap positions) on an area
	void setUMatPos(int x, int y, TerrainType t, int l);

	//! With l==0, it will remove no resource. (Unaligned coordinates)
	void setNoResource(int x, int y, int l);
	//! Removes every resource in the w by h area at (x, y) whose terrain no longer allows it,
	//! used after the terrain under it changed
	void removeUnallowedResources(int x, int y, int w, int h);
	//! With l==0, it will add resource only on one tile. (Aligned coordinates)
	void setResource(int x, int y, int type, int l);
	bool isResourceAllowed(int x, int y, int type);
	

	///The following is for script areas, which are named areas for map scripts set in the editor
	///@{
	///Returns whether area #n is set for a particular point. n can be from 0 to 8
	bool isPointSet(int n, int x, int y) const;
	///Sets a particular point on area #n
	void setPoint(int n, int x, int y);
	///Unsets a particular point on area #n
	void unsetPoint(int n, int x, int y);
	///Returns the name of area #n
	std::string getAreaName(int n) const;
	///Sets the name of area #n
	void setAreaName(int n, std::string name);
	///A vector holding the area names
	std::vector<std::string> areaNames;
	///@}
	
	//! Transform coordinate from map scale (mx,my) to pixel scale (px,py)
	void mapCaseToPixelCase(int mx, int my, int *px, int *py) const { *px=(mx<<5); *py=(my<<5); }
	//! Transform coordinate from map (mx,my) to screen (px,py). Use this one to display a building or an unit to the screen.
	// Presentation bounds only; never serialized or included in simulation checksums.
	int displayViewportW=0, displayViewportH=0;
	void mapCaseToDisplayable(int mx, int my, int *px, int *py, int viewportX, int viewportY) const;
	//! Transform coordinate from map (mx,my) to screen (px,py). Use this one to display a path line to the screen.
	void mapCaseToDisplayableVector(int mx, int my, int *px, int *py, int viewportX, int viewportY, int screenW, int screenH) const;
	//! Transform coordinate from screen (mx,my) to map (px,py) for standard grid aligned object (buildings, resources, units)
	void displayToMapCaseAligned(int mx, int my, int *px, int *py, int viewportX, int viewportY) const;
	//! Transform coordinate from screen (mx,my) to map (px,py) for standard grid unaligned object (terrain)
	void displayToMapCaseUnaligned(int mx, int my, int *px, int *py, int viewportX, int viewportY) const;
	//! Transform coordinate from screen (mx,my) to building (px,py)
	void cursorToBuildingPos(int mx, int my, int buildingWidth, int buildingHeight, int *px, int *py, int viewportX, int viewportY) const;
	//! Transform coordinate from building (px,py) to screen (mx,my)
	void buildingPosToCursor(int px, int py, int buildingWidth, int buildingHeight, int *mx, int *my, int viewportX, int viewportY) const;
	
	enum GradientType
	{
		GT_UNDEFINED = 0,
		GT_RESOURCE = 1,
		GT_BUILDING = 2,
		GT_FORBIDDEN = 3,
		GT_GUARD_AREA = 4,
		GT_CLEAR_AREA=5,
		GT_SIZE = 6
	};
	
	//! Swim class of a unit with these walk and swim speeds (0 = cannot swim).
	static int swimClass(int walkSpeed, int swimSpeed);
	//! Swim class used where no unit is at hand: water costs the same as land.
	static constexpr int SWIM_CLASS_EVEN = 3;
	//! Cheapest possible step for a class, the A* heuristic unit.
	static int minStepCost(int swimClass);
	//! Highest cost a gradient can hold (see MapInternal.h).
	static constexpr int GRADIENT_COST_LIMIT = 0xFFFF - 1 - 1 - 42;
	//! Cost of stepping (dx, dy) into the cell at targetIndex, in gradient units.
	int stepCost(int dx, int dy, size_t targetIndex, int swimClass) const;
	
	// Gradients are built per team and swim class the first time a unit of that
	// class asks for one, so classes nobody uses cost nothing.
	Uint16 *getResourceGradient(int teamNumber, int resourceType, int swimClass);
	Uint16 *getForbiddenGradient(int teamNumber, int swimClass);
	Uint16 *getGuardAreasGradient(int teamNumber, int swimClass);
	Uint16 *getClearAreasGradient(int teamNumber, int swimClass);
	
	bool resourceAvailable(int teamNumber, int resourceType, int swimClass, int x, int y);
	bool resourceAvailable(int teamNumber, int resourceType, int swimClass, int x, int y, int *dist);
	bool resourceAvailableUpdate(int teamNumber, int resourceType, int swimClass, int x, int y, Sint32 *targetX, Sint32 *targetY, int *dist);
	
	//! Follow the gradient uphill from (x, y). Returns whether a goal cell was reached; the
	//! last position is in (targetX, targetY). Works on the Uint16 pathfinding gradients and
	//! on the AIs' Uint8 maps alike: the goal is the maximum of the element type.
	template<typename T>
	bool getGlobalGradientDestination(const T *gradient, int x, int y, Sint32 *targetX, Sint32 *targetY) const;
	//! Whether (x, y) is a local maximum of gradient: no neighbour holds a strictly higher
	//! value. True at any tile getGlobalGradientDestination's ascent could end on, including
	//! gradients like a round-trip field whose seeded goal is a finite cost, not the type's max.
	template<typename T>
	bool isGradientPeak(const T *gradient, int x, int y) const;

	Uint16 getGradient(int teamNumber, Uint8 resourceType, int swimClass, int x, int y)
	{
		return getResourceGradient(teamNumber, resourceType, swimClass)[coordToIndex(x, y)];
	}
	
	// Chamfer distance transform on a pre-seeded Uint8 buffer. Caller fills the
	// buffer (0 = obstacle, 1 = free, any cell >= 3 = source); chamfer sweeps it
	// forward and backward until stable. Only the AIs' own helper maps use it;
	// the pathfinding gradients are built by propagateGradient. Defined in
	// MapGradientGlobal.cpp.
	void updateGlobalGradient(Uint8 *gradient);
	//! Dijkstra from every seeded cell of a pathfinding gradient (see MapInternal.h).
	//! Seeds may carry any cost up to GRADIENT_COST_LIMIT (0 for GRADIENT_AT_GOAL; e.g. a
	//! resource tile seeded with its distance to a building); do not pass a completed
	//! field. With maxCost, cells that would cost more stay unreachable. Uses shared
	//! scratch storage: calls across all Maps must be serial and non-reentrant.
	//! swimClass must be in [0, SWIM_CLASS_COUNT).
	void propagateGradient(Uint16 *gradient, int swimClass, int maxCost = GRADIENT_COST_LIMIT);
	//! Step toward the neighbour with the highest value minus step cost. strict requires
	//! real progress; otherwise a random sidestep to an equal cell is accepted when blocked.
	bool directionByGradient(Uint32 teamMask, int swimClass, int x, int y, const Uint16 *gradient, int *dx, int *dy, bool strict) const;
	void updateResourcesGradient(int teamNumber, Uint8 resourceType, int swimClass);
	//! Direction toward a resource of resourceType. With a target building the round-trip
	//! gradient is descended, so the unit heads for the resource that is nearest for
	//! fetching and carrying it there; without one, for the resource nearest to itself.
	bool pathfindResource(int teamNumber, Uint8 resourceType, int swimClass, int x, int y, int *dx, int *dy, bool *stopWork, Building *target);
#ifndef YOG_SERVER_ONLY
	void pathfindRandom(Unit *unit);
#endif  // !YOG_SERVER_ONLY

	//! Rebuild the building's full-map gradient for a swim class.
	void updateGlobalGradient(Building *building, int swimClass);
	//! Rebuild the building's round-trip gradient for a resource type and swim class:
	//! every tile of that resource is seeded with its distance to the building, so a
	//! cell's value is the cheapest fetch-and-carry trip from there.
	void updateRoundTripGradient(Building *building, int resourceType, int swimClass);
	//! The building's round-trip gradient, built or refreshed on demand. NULL when the
	//! building cannot be reached.
	const Uint16 *roundTripGradient(Building *building, int resourceType, int swimClass);
	//! Tiles of the cheapest trip from (x, y) to a resource of resourceType and on to the
	//! building, read from a round-trip gradient a fetcher's walk has already built. False
	//! when there is none or no such trip; the caller then scores by the plain distances.
	bool roundTripDistance(Building *building, int resourceType, int swimClass, int x, int y, int *dist);
	//! The building's gradient for a swim class, built or refreshed as needed; NULL if the building is unreachable.
	const Uint16 *buildingGradient(Building *building, int swimClass);
	bool buildingAvailable(Building *building, int swimClass, int x, int y, int *dist);
	//!requests the next step (dx, dy) to take to get to the building from (x,y)
	bool pathfindBuilding(Building *building, int swimClass, int x, int y, int *dx, int *dy);
	
	//! Mark the gradients of this team's buildings in the area for a rebuild. Wrap-safe on x,y
	void dirtyBuildingGradients(int x, int y, int wl, int hl, int teamNumber);
	bool pathfindForbidden(const Uint16 *optionGradient, int teamNumber, int swimClass, int x, int y, int *dx, int *dy);
	enum class AreaKind { Guard, Clear };
	//! Find the best direction toward a guard or clear area; return true if one has been found.
	bool pathfindArea(AreaKind kind, int teamNumber, int swimClass, int x, int y, int *dx, int *dy);
	//! Update the forbidden gradient, 
	void updateForbiddenGradient(int teamNumber, int swimClass);
	void updateForbiddenGradient(int teamNumber);
	void updateForbiddenGradient();
	//! Update the guard area gradient
	void updateGuardAreasGradient(int teamNumber, int swimClass);
	void updateGuardAreasGradient(int teamNumber);
	void updateGuardAreasGradient();
	//! Update the clear area gradient
	void updateClearAreasGradient(int teamNumber, int swimClass);
	void updateClearAreasGradient(int teamNumber);
	void updateClearAreasGradient();
	
	///Implements A* algorithm for point to point pathfinding. Does not cache path, designed to be fast
	bool pathfindPointToPoint(int x, int y, int targetX, int targetY, int *dx, int *dy, int swimClass, Uint32 teamMask, int maximumLength);
	
	void initExploredArea(int teamNumber);
	void makeDiscoveredAreasExplored(int teamNumber);
	void updateExploredArea(int teamNumber);
	
public:
	Game *game;
public:
	std::vector<Tile> tiles;
	Sint32 w, h;
	Sint32 wMask, hMask;
	Sint32 wDec, hDec;
	
protected:
	// private functions, used for edition

	void regenerateMap(int x, int y, int w, int h);
	
	Uint16 lookup(Uint8 tl, Uint8 tr, Uint8 bl, Uint8 br) const;

public:
    // here we handle terrain
	// mapDiscovered
	bool arraysBuilt; // if true, the next pointers(arrays) have to be valid and filled.
	std::vector<Uint32> mapDiscovered;
	std::vector<Uint32> fogOfWarA;
	std::vector<Uint32> fogOfWarB;
	Uint32* fogOfWar = nullptr; // if valid, either points to &fogOfWarA[0] or &fogOfWarB[0]
	//! Render-only overlay caches for the locally-displayed team's areas (forbidden /
	//! guard / clear). These mirror the per-team bits in tiles[].{forbidden,guardArea,
	//! clearArea} but only for displayedTeam, so the renderer can query one tile cheaply.
	//! They are NOT in checkSum() and must never be read from a sim path — doing so would
	//! desync, because displayedTeam differs per client. true = bit set.
	Utilities::BitArray displayedForbiddenView;
	Utilities::BitArray displayedGuardAreaView;
	Utilities::BitArray displayedClearAreaView;
	//! Team whose view is locally displayed. Mirror of GameGUI::localTeamNo, used only to
	//! decide whether to refresh the displayed*View caches above. Not in checkSum.
	Sint32 displayedTeam = NO_DISPLAYED_TEAM;
	
	///This is the maximum fertility of any point on the map
	Uint16 fertilityMaximum;
	
protected:
	// Pathfinding gradients, see MapInternal.h for the cell values. Indexed
	// [team][swim class]; NULL until a unit of that class asks for one.
	// Map owns the buffers and frees them on clear. Resource/guard/clear fields
	// refresh round-robin in syncStep; forbidden fields refresh through map edits.
	// Used to go to resources
	//[int team][int resourceNumber][int swimClass]
	Uint16 *resourcesGradient[Team::MAX_COUNT][MAX_NB_RESOURCES][SWIM_CLASS_COUNT];
	
	// Used to go out of forbidden areas
	Uint16 *forbiddenGradient[Team::MAX_COUNT][SWIM_CLASS_COUNT];
	
	// Used to attract idle warriors into guard areas
	Uint16 *guardAreasGradient[Team::MAX_COUNT][SWIM_CLASS_COUNT];
	
	// Used to attract idle workers into clearing
	// areas that aren't clear
	Uint16 *clearAreasGradient[Team::MAX_COUNT][SWIM_CLASS_COUNT];
	
public:
	// Used to guide explorers
	//[int team]
	// 0=unexplored, 255=just explored
	Uint8 *exploredArea[Team::MAX_COUNT];
	
	/// This shows how many "claims" there are on a particular resource square
	/// This is so that not all 150 free units go after one piece of wood
	/// Each square is the gid of the claiming unit
	Uint16 *clearingAreaClaims[Team::MAX_COUNT];
	
	/// These are integers that tell whether an immobile unit is standing on the
	/// square, and if so, what team number it is. In terms of the engine, these
	/// are treated like forbidden areas
	Uint8 *immobileUnits;
	
protected:
	//Used for scheduling computation time.
	bool gradientUpdated[Team::MAX_COUNT][MAX_NB_RESOURCES][SWIM_CLASS_COUNT];
	//Used for scheduling computation time on the guard area gradients
	bool guardGradientUpdated[Team::MAX_COUNT][SWIM_CLASS_COUNT];
	//Used for scheduling computation time on the clear area gradients
	bool clearGradientUpdated[Team::MAX_COUNT][SWIM_CLASS_COUNT];
	
	Uint8 *undermap;
	Uint8 **listedAddr;
	size_t size;

	Sector *sectors;
	Sint32 wSector, hSector;
	int sizeSector;
	
	
	///This is a single point in the array used for A* algorithm
	struct AStarAlgorithmPoint
	{
		AStarAlgorithmPoint() : x(-1), y(-1), dx(-1), dy(-1), moveCost(ASTAR_COST_INFINITY), totalCost(ASTAR_COST_INFINITY), isClosed(false) { }
		AStarAlgorithmPoint(Sint16 x, Sint16 y, Sint16 dx, Sint16 dy, Uint16 moveCost, Uint16 totalCost, bool isClosed) : x(x), y(y), dx(dx), dy(dy), moveCost(moveCost), totalCost(totalCost), isClosed(isClosed) {}
		//Pos x
		Sint16 x;
		//Pos y
		Sint16 y;
		//The direction from the starting point that leads to this path
		Sint16 dx;
		//The direction from the starting point that leads to this path
		Sint16 dy;
		//Cost to get to square x
		Uint16 moveCost;
		//Cost to get to square x + estimate to get to the end
		Uint16 totalCost;
		//Whether this cell has been examined
		bool isClosed;
	};
	
	///This is a function-object that compares two points based on their total score in the A* algorithm
	struct AStarComparator
	{
		AStarComparator(const AStarAlgorithmPoint* points) : points(points) {}
		bool operator()(int lhs, int rhs)
		{
			if(points[lhs].totalCost != points[rhs].totalCost)
				return points[lhs].totalCost > points[rhs].totalCost;
			// Total order on equal keys so the pop sequence does not depend on
			// how the standard library arranges equal heap elements.
			return lhs > rhs;
		}
		const AStarAlgorithmPoint* points;
	};
	
	//This array is kept and re-used for every point-to-point pathfind call
	AStarAlgorithmPoint* aStarPoints;
	std::vector<int> aStarExaminedPoints;

public:
	Uint32 checkSum(bool heavy);
	Sint32 warpDist1d(int p, int q, int l);///distance of coordinates p and q on a loop of length l
	Sint32 warpDistSquare(int px, int py, int qx, int qy); //!< The distance^2 between (px, py) and (qx, qy), warp-safe.
	Sint32 warpDistMax(int px, int py, int qx, int qy); //!< The max distance on x or y axis, between (px, py) and (qx, qy), warp-safe.
	void dumpGradient(Uint8 *gradient, const std::string filename = "gradient.dump.pgm");

public:
	void makeHomogenMap(TerrainType terrainType);
	void controlSand(void);
	void smoothResources(int times);
	bool makeRandomMap(MapGenerationDescriptor &descriptor);
	bool oldMakeRandomMap(MapGenerationDescriptor &descriptor);
	bool oldMakeIslandsMap(MapGenerationDescriptor &descriptor);

};

