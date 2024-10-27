/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  Copyright (C) 2006 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef __MAP_H
#define __MAP_H

#include <list>
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
	static const bool verbose = false;
public:
	//! Type of terrain (used for undermap)


public:
	//! Map constructor
	Map();
	//! Map destructor
	virtual ~Map(void);
	//! Reset map and free arrays
	void clear();
	void logAtClear();

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
	//! Return map height mask
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

	/// Returns a normalized version of the x coordinate, taking into account that x coordinates wrap around
	int normalizeX(int x) const
	{
		return x & wMask;
	}
	
	/// Returns a normalized version of the y coordinate, taking into account that y coordinates wrap around
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
	
	//! Return true if the position (x,y) is a forbidden area set by the user
	// or rather, by the team computeLocalForbidden was last called for
	bool isForbiddenLocal(int x, int y) const
	{
		return localForbiddenMap.get(coordToIndex(x, y));
	}
	
	//! Returns true if the position(x, y) is a forbidden area for the given team
	bool isForbidden(int x, int y, Uint32 teamMask) const
	{
		return tiles[coordToIndex(x, y)].forbidden&teamMask;
	}

	//! Return true if the position (x,y) is a guard area set by the user
	// or rather, by the team computeLocalGuardArea was last called for
	bool isGuardAreaLocal(int x, int y) const
	{
		return localGuardAreaMap.get(coordToIndex(x, y));
	}
	
	//! Returns true if the position(x, y) is a guard area for the given team
	bool isGuardArea(int x, int y, Uint32 teamMask) const
	{
		return tiles[coordToIndex(x, y)].guardArea&teamMask;
	}

	//! Return true if the position (x,y) is a clear area set by the user
	// or rather, by the team computeLocalClearArea was last called for
	bool isClearAreaLocal(int x, int y) const
	{
		return localClearAreaMap.get(coordToIndex(x, y));
	}

	//! Returns true if the position(x, y) is a clear area for the given team
	bool isClearArea(int x, int y, Uint32 teamMask) const
	{
		return tiles[coordToIndex(x, y)].clearArea&teamMask;
	}
	
	// note - these are only meant to be called for the LOCAL team
	// (the one whose stuff is displayed on the screen)
	//! Compute localForbiddenMap from tiles array
	void computeLocalForbidden(int localTeamNo);
	//! Compute localGuardAreaMap from tiles array
	void computeLocalGuardArea(int localTeamNo);
	//! Compute localClearAreaMap from tiles array
	void computeLocalClearArea(int localTeamNo);
	
	//! Return the tile at a given position
	inline Tile &getTile(int x, int y)
	{
		return tiles[coordToIndex(x, y)];
	}

	//! Return the const case at a given position
	inline const Tile &getTile(int x, int y) const
	{
		return tiles[coordToIndex(x, y)];
	}

	//! Return the terrain for a given coordinate
	inline Uint16 getTerrain(int x, int y) const
	{
		return tiles[coordToIndex(x, y)].terrain;
	}
	
	//! Return the terrain for a given position in case array
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
			return -1;
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
	
	Uint8 getGuardAreasGradient(int x, int y, bool canSwim, int team) const
	{
		return guardAreasGradient[team][canSwim][coordToIndex(x, y)];
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
	
	//! Return true if unit has contact with building gbid. If true, put contact direction in dx, dy
	bool doesUnitTouchBuilding(Unit *unit, Uint16 gbid, int *dx, int *dy) const;
	//! Return true if (x,y) has contact with building gbid.
	bool doesPosTouchBuilding(int x, int y, Uint16 gbid) const;
	//! Return true if (x,y) has contact with building gbid. If true, put contact direction in dx, dy
	bool doesPosTouchBuilding(int x, int y, Uint16 gbid, int *dx, int *dy) const;
	
	//! Return true if unit has contact with resource of any resourceType. If true, put contact direction in dx, dy
	bool doesUnitTouchResource(Unit *unit, int *dx, int *dy) const;
	//! Return true if unit has contact with resource of type resourceType. If true, put contact direction in dx, dy
	bool doesUnitTouchResource(Unit *unit, int resourceType, int *dx, int *dy) const;
	//! Return true if (x,y) has contact with resource of type resourceType. If true, put contact direction in dx, dy
	bool doesPosTouchResource(int x, int y, int resourceType, int *dx, int *dy) const;
	//! Return true if unit has contact with enemy. If true, put contact direction in dx, dy
	bool doesUnitTouchEnemy(Unit *unit, int *dx, int *dy) const;

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
	
	//! Return sector at (x,y).
	Sector *getSector(int x, int y) { return &(sectors[wSector*((y&hMask)>>4)+((x&wMask)>>4)]); }
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
	//! With l==0, it will add resource only on one case. (Aligned coordinates)
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
	
	bool resourceAvailable(int teamNumber, int resourceType, bool canSwim, int x, int y) const;
	bool resourceAvailable(int teamNumber, int resourceType, bool canSwim, int x, int y, int *dist) const;
	bool resourceAvailableUpdate(int teamNumber, int resourceType, bool canSwim, int x, int y, Sint32 *targetX, Sint32 *targetY, int *dist);
	
	//! Starting from position (x, y) using gradient, returns the gradient destination in (targetX, targetY)
	bool getGlobalGradientDestination(Uint8 *gradient, int x, int y, Sint32 *targetX, Sint32 *targetY) const;
	
	Uint8 getGradient(int teamNumber, Uint8 resourceType, bool canSwim, int x, int y) const
	{
		const Uint8 *gradient = resourcesGradient[teamNumber][resourceType][canSwim];
		assert(gradient);
		return gradient[coordToIndex(x, y)];
	}
	
	Uint8 getClearingGradient(int teamNumber, bool canSwim, int x, int y) const
	{
		const Uint8 *gradient = clearAreasGradient[teamNumber][canSwim];
		assert(gradient);
		return gradient[coordToIndex(x, y)];
	}
	
	void updateGlobalGradientSlow(Uint8 *gradient);
	template<typename Tint> void updateGlobalGradientSlow(Uint8 *gradient);
	
	template<typename Tint> void updateGlobalGradientVersionSimple(
		Uint8 *gradient, Tint *listedAddr, size_t listCountWrite, GradientType gradientType);
	template<typename Tint> void updateGlobalGradientVersionSimon(Uint8 *gradient, Tint *listedAddr, size_t listCountWrite);
	template<typename Tint> void updateGlobalGradientVersionKai(Uint8 *gradient, Tint *listedAddr, size_t listCountWrite);
	template<typename Tint> void updateGlobalGradient(
		Uint8 *gradient, Tint *listedAddr, size_t listCountWrite, GradientType gradientType, bool canSwim);
	//void updateGlobalGradientSmall(Uint8 *gradient);
	//void updateGlobalGradientBig(Uint8 *gradient);
	//void updateGlobalGradient(Uint8 *gradient);
	void updateResourcesGradient(int teamNumber, Uint8 resourceType, bool canSwim);
	template<typename Tint> void updateResourcesGradient(int teamNumber, Uint8 resourceType, bool canSwim);
	bool directionFromMiniGrad(Uint8 miniGrad[25], int *dx, int *dy, const bool strict, bool verbose) const;
	bool directionByMiniGrad(Uint32 teamMask, bool canSwim, int x, int y, int *dx, int *dy, const Uint8 *gradient, bool strict, bool verbose) const;
	bool directionByMiniGrad(Uint32 teamMask, bool canSwim, int x, int y, int bx, int by, int *dx, int *dy, Uint8 localGradient[1024], bool strict, bool verbose) const;
	bool pathfindResource(int teamNumber, Uint8 resourceType, bool canSwim, int x, int y, int *dx, int *dy, bool *stopWork, bool verbose);
#ifndef YOG_SERVER_ONLY
	void pathfindRandom(Unit *unit, bool verbose);
#endif  // !YOG_SERVER_ONLY

	void updateLocalGradient(Building *building, bool canSwim); //The 32*32 gradient
	void updateGlobalGradient(Building *building, bool canSwim); //The full-sized gradient
	template<typename Tint> void updateGlobalGradient(Building *building, bool canSwim);
	//!A special gradient for clearing flags. Returns false if there is nothing to clear.
	bool updateLocalResources(Building *building, bool canSwim); 
	void expandLocalGradient(Uint8 *gradient);
	
	bool buildingAvailable(Building *building, bool canSwim, int x, int y, int *dist);
	//!requests the next step (dx, dy) to take to get to the building from (x,y) provided the unit canSwim.
	bool pathfindBuilding(Building *building, bool canSwim, int x, int y, int *dx, int *dy, bool verbose);
	bool pathfindLocalResource(Building *building, bool canSwim, int x, int y, int *dx, int *dy); // Used for all resources mixed in clearing flags.
	
	//! Make local gradient dirty in the area. Wrap-safe on x,y
	void dirtyLocalGradient(int x, int y, int wl, int hl, int teamNumber);
	bool pathfindForbidden(const Uint8 *optionGradient, int teamNumber, bool canSwim, int x, int y, int *dx, int *dy, bool verbose);
	//! Find the best direction toward guard area, return true if one has been found, false otherwise
	bool pathfindGuardArea(int teamNumber, bool canSwim, int x, int y, int *dx, int *dy);
	//! Find the best direction toward clearing area, return true if one has been found, false otherwise
	bool pathfindClearArea(int teamNumber, bool canSwim, int x, int y, int *dx, int *dy);
	//! Update the forbidden gradient, 
	void updateForbiddenGradient(int teamNumber, bool canSwim);
	template<typename Tint> void updateForbiddenGradient(int teamNumber, bool canSwim);
	void updateForbiddenGradient(int teamNumber);
	void updateForbiddenGradient();
	//! Update the guard area gradient
	void updateGuardAreasGradient(int teamNumber, bool canSwim);
	template<typename Tint> void updateGuardAreasGradient(int teamNumber, bool canSwim);
	void updateGuardAreasGradient(int teamNumber);
	void updateGuardAreasGradient();
	//! Update the clear area gradient
	void updateClearAreasGradient(int teamNumber, bool canSwim);
	template<typename Tint> void updateClearAreasGradient(int teamNumber, bool canSwim);
	void updateClearAreasGradient(int teamNumber);
	void updateClearAreasGradient();
	
	///Implements A* algorithm for point to point pathfinding. Does not cache path, designed to be fast
	bool pathfindPointToPoint(int x, int y, int targetX, int targetY, int *dx, int *dy, bool canSwim, Uint32 teamMask, int maximumLength);
	
	void initExploredArea(int teamNumber);
	void makeDiscoveredAreasExplored(int teamNumber);
	void updateExploredArea(int teamNumber);
	
protected:
	// computed pathfinding statistics:
	int resourceAvailableCount[16][MAX_RESOURCES];
	int resourceAvailableCountSuccess[16][MAX_RESOURCES];
	int resourceAvailableCountFailure[16][MAX_RESOURCES];
	
	int pathToResourceCountTot;
	int pathToResourceCountSuccess;
	int pathToResourceCountFailure;
	
	int localResourcesUpdateCount;
	
	int pathfindLocalResourceCount;
	int pathfindLocalResourceCountWait;
	int pathfindLocalResourceCountSuccessBase;
	int pathfindLocalResourceCountSuccessLocked;
	int pathfindLocalResourceCountSuccessUpdate;
	int pathfindLocalResourceCountSuccessUpdateLocked;
	int pathfindLocalResourceCountFailureUnusable;
	int pathfindLocalResourceCountFailureNone;
	int pathfindLocalResourceCountFailureBad;
	
	int pathToBuildingCountTot;
	
	int pathToBuildingCountClose;
	int pathToBuildingCountCloseSuccessStand;
	int pathToBuildingCountCloseSuccessBase;
	int pathToBuildingCountCloseSuccessUpdated;
	int pathToBuildingCountCloseFailureLocked;
	int pathToBuildingCountCloseFailureEnd;
	
	int pathToBuildingCountIsFar;
	int pathToBuildingCountFar;
	int pathToBuildingCountFarIsNew;
	int pathToBuildingCountFarOldSuccess;
	int pathToBuildingCountFarOldFailureLocked;
	int pathToBuildingCountFarOldFailureBad;
	int pathToBuildingCountFarOldFailureRepeat;
	int pathToBuildingCountFarOldFailureUnusable;
	int pathToBuildingCountFarUpdateSuccess;
	int pathToBuildingCountFarUpdateFailureLocked;
	int pathToBuildingCountFarUpdateFailureVirtual;
	int pathToBuildingCountFarUpdateFailureBad;
	
	int localBuildingGradientUpdate;
	int localBuildingGradientUpdateLocked;
	int globalBuildingGradientUpdate;
	int globalBuildingGradientUpdateLocked;
	
	int buildingAvailableCountTot;
	
	int buildingAvailableCountClose;
	int buildingAvailableCountCloseSuccessFast;
	int buildingAvailableCountCloseSuccessAround;
	int buildingAvailableCountCloseSuccessUpdate;
	int buildingAvailableCountCloseSuccessUpdateAround;
	int buildingAvailableCountCloseFailureLocked;
	int buildingAvailableCountCloseFailureEnd;
	
	int buildingAvailableCountIsFar;
	int buildingAvailableCountFar;
	int buildingAvailableCountFarNew;
	int buildingAvailableCountFarNewSuccessFast;
	int buildingAvailableCountFarNewSuccessClosely;
	int buildingAvailableCountFarNewFailureLocked;
	int buildingAvailableCountFarNewFailureVirtual;
	int buildingAvailableCountFarNewFailureEnd;
	int buildingAvailableCountFarOld;
	int buildingAvailableCountFarOldSuccessFast;
	int buildingAvailableCountFarOldSuccessAround;
	int buildingAvailableCountFarOldFailureLocked;
	int buildingAvailableCountFarOldFailureEnd;
	
	int pathfindForbiddenCount;
	int pathfindForbiddenCountSuccess;
	int pathfindForbiddenCountFailure;
	
	//#define check_disorderable_gradient_error_probability
	#ifdef check_disorderable_gradient_error_probability
	// stats to check the probability of an error in the updateGlobalGradientVersionDisorderable gradient computation
	int *listCountSizeStats[GT_SIZE];
	int listCountSizeStatsOver[GT_SIZE];
	#endif

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
	//! true = forbidden
	Utilities::BitArray localForbiddenMap;
	//! true = guard area
	Utilities::BitArray localGuardAreaMap;
	//! true = clear area
	Utilities::BitArray localClearAreaMap;
	
	///This is the maximum fertility of any point on the map
	Uint16 fertilityMaximum;
	
public:
	// Used to go to resources
	//[int team][int resourceNumber][bool unitCanSwim]
	//255=resource, 0=obstacle, the higher it is, the closer it is to the resource.
	Uint8 *resourcesGradient[Team::MAX_COUNT][MAX_NB_RESOURCES][2];
	
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
	bool gradientUpdated[Team::MAX_COUNT][MAX_NB_RESOURCES][2];
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
		AStarAlgorithmPoint() : x(-1), y(-1), dx(-1), dy(-1), moveCost(static_cast<Uint16>(-1)), totalCost(static_cast<Uint16>(-1)), isClosed(false) { }
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
	void makeHomogeneMap(TerrainType terrainType);
	void controlSand(void);
	void smoothResources(int times);
	bool makeRandomMap(MapGenerationDescriptor &descriptor);
	bool oldMakeRandomMap(MapGenerationDescriptor &descriptor);
	void oldAddResourcesRandomMap(MapGenerationDescriptor &descriptor);
	bool oldMakeIslandsMap(MapGenerationDescriptor &descriptor);
	void oldAddResourcesIslandsMap(MapGenerationDescriptor &descriptor);

protected:
	FILE *logFile;
	Uint32 incResourceLog[16];
};

#endif
