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

//! No global unit identifier. This value means there is no unit. Used at Case::groundUnit or Case::airUnit.
#define NOGUID 0xFFFF

//! No global building identifier. This value means there is no building. Used at Case::building.
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
struct Case
{
	Uint16 terrain = 0; // default, not really meaningful.
	Uint16 building = NOGBID;

	Ressource ressource;

	Uint16 groundUnit = NOGUID;
	Uint16 airUnit = NOGUID;

	Uint32 forbidden = 0; // This is a mask, one bit by team, 1=forbidden, 0=allowed
	///The difference between forbidden zone and hidden forbidden zone is that hidden forbidden zone
	///is put there by the game engine and is not draw to the screen.
	Uint32 guardArea = 0; // This is a mask, one bit by team, 1=guard area, 0=normal
	Uint32 clearArea = 0; // This is a mask, one bit by team, 1=clear area, 0=normal

	Uint16 scriptAreas = 0; // This is also a mask. A single bit represents an area #n, on or off for the square
	Uint8 canRessourcesGrow = 1; // This is a boolean, it represents whether ressources are allowed to grow into this location.
	
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
	
	// add & remove teams, used by the map editor and the random map generator
	// Have to be called *after* session.numberOfTeam has been changed.
	void addTeam(void);
	void removeTeam(void);

	//! Grow ressources on map
	void growRessources(void);
#ifndef YOG_SERVER_ONLY
	//! Do a step associated with map (grow ressources and process bullets)
	void syncStep(Uint32 stepCounter);
#endif  // !YOG_SERVER_ONLY
	//! Switch the Fog of War bufferRessourceType
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

	///Returns a normalized version of the x cordinate, taking into account that x coordinates wrap around
	int normalizeX(int x) const
	{
		return x & wMask;
	}
	
	///Returns a normalized version of the y cordinate, taking into account that y coordinates wrap around
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
		return cases[coordToIndex(x, y)].forbidden&teamMask;
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
		return cases[coordToIndex(x, y)].guardArea&teamMask;
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
		return cases[coordToIndex(x, y)].clearArea&teamMask;
	}
	
	// These rebuild the render-only displayed*View caches from the authoritative
	// cases[] bits, and are only meaningful for the locally-displayed team (the one
	// whose areas are drawn on screen). They do not touch checkSum() state.
	//! Rebuild displayedForbiddenView from cases[].forbidden for the given team.
	void computeDisplayedForbidden(int teamNumber);
	//! Rebuild displayedGuardAreaView from cases[].guardArea for the given team.
	void computeDisplayedGuardArea(int teamNumber);
	//! Rebuild displayedClearAreaView from cases[].clearArea for the given team.
	void computeDisplayedClearArea(int teamNumber);

	//! Sentinel for "no displayed team yet" — used before GameGUI::adjustLocalTeam has run.
	static constexpr Sint32 NO_DISPLAYED_TEAM = -1;
	//! Register the team whose view is currently displayed. Used only to decide whether
	//! to refresh the displayedForbiddenView / displayedGuardAreaView / displayedClearAreaView
	//! caches. This identity is per-client display state — not in checkSum() — and must be
	//! kept in sync with GameGUI's localTeamNo. Never branch sim/network state on it.
	void setDisplayedTeam(Sint32 teamNo) { displayedTeam = teamNo; }
	Sint32 getDisplayedTeam() const { return displayedTeam; }
	
	//! Return the case at a given position
	inline Case &getCase(int x, int y)
	{
		return cases[coordToIndex(x, y)];
	}

	//! Return the const case at a given position
	inline const Case &getCase(int x, int y) const
	{
		return cases[coordToIndex(x, y)];
	}

	//! Return the terrain for a given coordinate
	inline Uint16 getTerrain(int x, int y) const
	{
		return cases[coordToIndex(x, y)].terrain;
	}
	
	//! Return the terrain for a given position in case array
	inline Uint16 getTerrain(size_t pos) const
	{
		return cases[pos].terrain;
	}

	//! Return the typeof terrain. If type is unregistred, returns unknown (-1).
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

	const Ressource& getRessource(int x, int y) const
	{
		return cases[coordToIndex(x, y)].ressource;
	}

	const Ressource& getRessource(size_t pos) const
	{
		return cases[pos].ressource;
	}

	Ressource& getRessource(int x, int y)
	{
		return cases[coordToIndex(x, y)].ressource;
	}
	
	Ressource& getRessource(size_t pos)
	{
		return cases[pos].ressource;
	}
	
	//Returns the combined forbidden and hidden foribidden masks
	Uint32 getForbidden(int x, int y) const
	{
		return cases[coordToIndex(x, y)].forbidden;
	}
	
	Uint8 getExplored(int x, int y, int team) const
	{
		return exploredArea[team][coordToIndex(x, y)];
	}
	
	Uint8 getGuardAreasGradient(int x, int y, bool canSwim, int team) const
	{
		return guardAreasGradient[team][canSwim][coordToIndex(x, y)];
	}
	
	void setTerrain(int x, int y, Uint16 terrain)
	{
		cases[coordToIndex(x, y)].terrain = terrain;
	}
	
	void setForbidden(int x, int y, Uint32 forbidden)
	{
		cases[coordToIndex(x, y)].forbidden = forbidden;
	}
	
	void addForbidden(int x, int y, Uint32 teamNum)
	{
		cases[coordToIndex(x, y)].forbidden |=  Team::teamNumberToMask(teamNum);
	}

	void removeForbidden(int x, int y, Uint32 teamNum)
	{
		Case& c=cases[coordToIndex(x, y)];
		c.forbidden ^= c.forbidden &  Team::teamNumberToMask(teamNum);
	}
	
	void addClearArea(int x, int y, Uint32 teamNum)
	{
		cases[coordToIndex(x, y)].clearArea |=  Team::teamNumberToMask(teamNum);
	}
	
	void addGuardArea(int x, int y, Uint32 teamNum)
	{
		cases[coordToIndex(x, y)].guardArea |=  Team::teamNumberToMask(teamNum);
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

	bool isRessource(int x, int y) const
	{
		return getCase(x, y).ressource.type != NO_RES_TYPE;
	}

	bool isRessourceTakeable(int x, int y, int ressourceType) const
	{
		const Ressource &ressource = getCase(x, y).ressource;
		return (ressource.type == ressourceType && ressource.amount > 0);
	}

	bool isRessourceTakeable(int x, int y, bool ressourceTypes[BASIC_COUNT]) const
	{
		const Ressource &ressource = getCase(x, y).ressource;
		return (ressource.type != NO_RES_TYPE
			&& ressource.amount > 0
			&& ressource.type < BASIC_COUNT
			&& ressourceTypes[ressource.type]);
	}

	bool isRessource(int x, int y, int *ressourceType) const
	{
		const Ressource &ressource = getCase(x, y).ressource;
		if (ressource.type == NO_RES_TYPE)
			return false;
		*ressourceType = ressource.type;
		return true;
	}

	bool canRessourcesGrow(int x, int y) const
	{
		return getCase(x, y).canRessourcesGrow;
	}

	//! Decrement ressource at position (x,y). Return true on success, false otherwise.
	void decRessource(int x, int y);
	//! Decrement ressource at position (x,y) if ressource type = ressourceType. Return true on success, false otherwise.
	void decRessource(int x, int y, int ressourceType);
	bool incRessource(int x, int y, int ressourceType, int variety);

private:
	//! Per-tile predicate driver shared by isFree*/isHardSpace*.
	//! Each flag toggles whether one occupancy/terrain test contributes to rejection.
	struct TileChecks {
		bool noRessource    : 1; //!< reject if a ressource sits on the tile
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
	// The "hardSpace" keywork means "Free" but you don't count Ground-Units as obstacles.
	bool isHardSpaceForGroundUnit(int x, int y, bool canSwim, Uint32 me) const;
	bool isHardSpaceForBuilding(int x, int y) const;
	bool isHardSpaceForBuilding(int x, int y, int w, int h) const;
	bool isHardSpaceForBuilding(int x, int y, int w, int h, Uint16 gid) const;
	
	//! Return contact direction (dx, dy) if unit touches building gbid; nullopt otherwise.
	std::optional<Offset> doesUnitTouchBuilding(Unit *unit, Uint16 gbid) const;
	//! Return contact direction (dx, dy) if (x, y) touches building gbid; nullopt otherwise.
	std::optional<Offset> doesPosTouchBuilding(int x, int y, Uint16 gbid) const;

	//! Return contact direction (dx, dy) if unit touches a ressource of any type; nullopt otherwise.
	std::optional<Offset> doesUnitTouchRessource(Unit *unit) const;
	//! Return contact direction (dx, dy) if unit touches a ressource of the given type; nullopt otherwise.
	std::optional<Offset> doesUnitTouchRessource(Unit *unit, int ressourceType) const;
	//! Return contact direction (dx, dy) if (x, y) touches a ressource of the given type; nullopt otherwise.
	std::optional<Offset> doesPosTouchRessource(int x, int y, int ressourceType) const;
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
	Uint16 getGroundUnit(int x, int y) const { return cases[coordToIndex(x, y)].groundUnit; }
	Uint16 getAirUnit(int x, int y) const { return cases[coordToIndex(x, y)].airUnit; }
	Uint16 getBuilding(int x, int y) const { return cases[coordToIndex(x, y)].building; }
	
	void setGroundUnit(int x, int y, Uint16 guid) { cases[coordToIndex(x, y)].groundUnit = guid; }
	void setAirUnit(int x, int y, Uint16 guid) { cases[coordToIndex(x, y)].airUnit = guid; }
	void setBuilding(int x, int y, int w, int h, Uint16 gbid)
	{
		for (int yi=y; yi<y+h; yi++)
			for (int xi=x; xi<x+w; xi++)
				cases[coordToIndex(xi, yi)].building = gbid;
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

	//! With l==0, it will remove no ressource. (Unaligned coordinates)
	void setNoRessource(int x, int y, int l);
	//! With l==0, it will add ressource only on one case. (Aligned coordinates)
	void setRessource(int x, int y, int type, int l);
	bool isRessourceAllowed(int x, int y, int type);
	

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
	void mapCaseToDisplayable(int mx, int my, int *px, int *py, int viewportX, int viewportY) const;
	//! Transform coordinate from map (mx,my) to screen (px,py). Use this one to display a pathline to the screen.
	void mapCaseToDisplayableVector(int mx, int my, int *px, int *py, int viewportX, int viewportY, int screenW, int screenH) const;
	//! Transform coordinate from screen (mx,my) to map (px,py) for standard grid aligned object (buildings, ressources, units)
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
	
	bool ressourceAvailable(int teamNumber, int ressourceType, bool canSwim, int x, int y) const;
	bool ressourceAvailable(int teamNumber, int ressourceType, bool canSwim, int x, int y, int *dist) const;
	bool ressourceAvailableUpdate(int teamNumber, int ressourceType, bool canSwim, int x, int y, Sint32 *targetX, Sint32 *targetY, int *dist);
	
	//! Starting from position (x, y) using gradient, returns the gradient destination in (targetX, targetY)
	bool getGlobalGradientDestination(Uint8 *gradient, int x, int y, Sint32 *targetX, Sint32 *targetY) const;
	
	Uint8 getGradient(int teamNumber, Uint8 ressourceType, bool canSwim, int x, int y) const
	{
		const Uint8 *gradient = ressourcesGradient[teamNumber][ressourceType][canSwim];
		assert(gradient);
		return gradient[coordToIndex(x, y)];
	}
	
	Uint8 getClearingGradient(int teamNumber, bool canSwim, int x, int y) const
	{
		const Uint8 *gradient = clearAreasGradient[teamNumber][canSwim];
		assert(gradient);
		return gradient[coordToIndex(x, y)];
	}
	
	// Chamfer distance transform on a pre-seeded gradient buffer. Caller
	// fills the buffer (0 = obstacle, 1 = free, any cell >= 3 = source);
	// chamfer sweeps it forward and backward until stable. Defined in
	// MapGradientGlobal.cpp.
	void updateGlobalGradient(Uint8 *gradient);
	void updateRessourcesGradient(int teamNumber, Uint8 ressourceType, bool canSwim);
	bool directionFromMinigrad(Uint8 miniGrad[25], int *dx, int *dy, const bool strict) const;
	bool directionByMinigrad(Uint32 teamMask, bool canSwim, int x, int y, int *dx, int *dy, const Uint8 *gradient, bool strict) const;
	bool directionByMinigrad(Uint32 teamMask, bool canSwim, int x, int y, int bx, int by, int *dx, int *dy, Uint8 localGradient[1024], bool strict) const;
	bool pathfindRessource(int teamNumber, Uint8 ressourceType, bool canSwim, int x, int y, int *dx, int *dy, bool *stopWork);
#ifndef YOG_SERVER_ONLY
	void pathfindRandom(Unit *unit);
#endif  // !YOG_SERVER_ONLY

	void updateLocalGradient(Building *building, bool canSwim); //The 32*32 gradient
	void updateGlobalGradient(Building *building, bool canSwim); //The full-sized gradient
	//!A special gradient for clearing flags. Returns false if there is nothing to clear.
	bool updateLocalRessources(Building *building, bool canSwim);
	
	//! Probe a full-map gradient at (x, y) and its 8 neighbors; sets *dist = GRADIENT_AT_GOAL - g if reachable.
	bool probeGlobalGradient(const Uint8 *gradient, int x, int y, int *dist) const;
	bool buildingAvailable(Building *building, bool canSwim, int x, int y, int *dist);
	//!requests the next step (dx, dy) to take to get to the building from (x,y) provided the unit canSwim.
	bool pathfindBuilding(Building *building, bool canSwim, int x, int y, int *dx, int *dy);
	bool pathfindLocalRessource(Building *building, bool canSwim, int x, int y, int *dx, int *dy); // Used for all ressources mixed in clearing flags.
	
	//! Make local gradient dirty in the area. Wrap-safe on x,y
	void dirtyLocalGradient(int x, int y, int wl, int hl, int teamNumber);
	bool pathfindForbidden(const Uint8 *optionGradient, int teamNumber, bool canSwim, int x, int y, int *dx, int *dy);
	enum class AreaKind { Guard, Clear };
	//! Find the best direction toward a guard or clear area; return true if one has been found.
	bool pathfindArea(AreaKind kind, int teamNumber, bool canSwim, int x, int y, int *dx, int *dy);
	//! Update the forbidden gradient, 
	void updateForbiddenGradient(int teamNumber, bool canSwim);
	void updateForbiddenGradient(int teamNumber);
	void updateForbiddenGradient();
	//! Update the guard area gradient
	void updateGuardAreasGradient(int teamNumber, bool canSwim);
	void updateGuardAreasGradient(int teamNumber);
	void updateGuardAreasGradient();
	//! Update the clear area gradient
	void updateClearAreasGradient(int teamNumber, bool canSwim);
	void updateClearAreasGradient(int teamNumber);
	void updateClearAreasGradient();
	
	///Implements A* algorithm for point to point pathfinding. Does not cache path, designed to be fast
	bool pathfindPointToPoint(int x, int y, int targetX, int targetY, int *dx, int *dy, bool canSwim, Uint32 teamMask, int maximumLength);
	
	void initExploredArea(int teamNumber);
	void makeDiscoveredAreasExplored(int teamNumber);
	void updateExploredArea(int teamNumber);
	
public:
	Game *game;
public:
	std::vector<Case> cases;
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
	//! guard / clear). These mirror the per-team bits in cases[].{forbidden,guardArea,
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
	
public:
	// Used to go to ressources
	//[int team][int ressourceNumber][bool unitCanSwim]
	//255=resource, 0=obstacle, the higher it is, the closer it is to the resource.
	Uint8 *ressourcesGradient[Team::MAX_COUNT][MAX_NB_RESSOURCES][2];
	
	// Used to go out of forbidden areas
	//[int team][bool unitCanSwim]
	Uint8 *forbiddenGradient[Team::MAX_COUNT][2];
	
	// Used to attract idle warriors into guard areas
	//[int team][bool unitCanSwim]
	Uint8 *guardAreasGradient[Team::MAX_COUNT][2];
	
	// Used to attract idle workers into clearing
	// areas that aren't clear
	Uint8 *clearAreasGradient[Team::MAX_COUNT][2];
	
	// Used to guide explorers
	//[int team]
	// 0=unexplored, 255=just explored
	Uint8 *exploredArea[Team::MAX_COUNT];
	
	/// This shows how many "claims" there are on a particular ressource square
	/// This is so that not all 150 free units go after one piece of wood
	/// Each square is the gid of the claiming unit
	Uint16 *clearingAreaClaims[Team::MAX_COUNT];
	
	/// These are integers that tell whether an immobile unit is standing on the
	/// square, and if so, what team number it is. In terms of the engine, these
	/// are treated like forbidden areas
	Uint8 *immobileUnits;
	
protected:
	//Used for scheduling computation time.
	bool gradientUpdated[Team::MAX_COUNT][MAX_NB_RESSOURCES][2];
	//Used for scheduling computation time on the guard area gradients
	bool guardGradientUpdated[Team::MAX_COUNT][2];
	//Used for scheduling computation time on the clear area gradients
	bool clearGradientUpdated[Team::MAX_COUNT][2];
	
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
			if(points[lhs].totalCost > points[rhs].totalCost)
				return true;
			return false;
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
	Sint32 warpDistSum(int px, int py, int qx, int qy); //!< The combined distance on x and r y axis, between (px, py) and (qx, qy), warp-safe.
	bool isInLocalGradient(int ux, int uy, int bx, int by); //!< Return true if the unit @(ux, uy) is close enough of building @(bx, by).
	void dumpGradient(Uint8 *gradient, const std::string filename = "gradient.dump.pgm");

public:
	void makeHomogenMap(TerrainType terrainType);
	void controlSand(void);
	void smoothRessources(int times);
	bool makeRandomMap(MapGenerationDescriptor &descriptor);
	bool oldMakeRandomMap(MapGenerationDescriptor &descriptor);
	void oldAddRessourcesRandomMap(MapGenerationDescriptor &descriptor);
	bool oldMakeIslandsMap(MapGenerationDescriptor &descriptor);
	void oldAddRessourcesIslandsMap(MapGenerationDescriptor &descriptor);

};

