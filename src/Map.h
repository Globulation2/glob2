/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
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

//! No global unit identifier. This value means there is no unit. Used at Case::groundUnit or Case::airUnit.
#define NOGUID 0xFFFF

//! No global building identifier. This value means there is no building. Used at Case::building.
#define NOGBID 0xFFFF

class Map;
class Game;
class MapGenerationDescriptor;
class SessionGame;

// a 1x1 piece of map
struct Case
{
	Uint16 terrain;
	Uint16 building;

	Ressource ressource;

	Uint16 groundUnit;
	Uint16 airUnit;

	Uint32 forbidden; // This is a mask, one bit by team, 1=forbidden, 0=allowed
	Uint32 guardArea; // This is a mask, one bit by team, 1=guard area, 0=normal
	Uint32 clearArea; // This is a mask, one bit by team, 1=clear area, 0=normal
};


/*! Map, handle all physical localisations
	All size are given in 32x32 pixel cell, which is the basic game measurement unit.
	All functions are wrap-safe, excepted the one specified otherwise.
*/
class Map
{
public:
	//! Type of terrain (used for undermap)


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
	bool load(GAGCore::InputStream *stream, SessionGame *sessionGame, Game *game=NULL);
	//! Save a map
	void save(GAGCore::OutputStream *stream);
	
	// add & remove teams, used by the map editor and the random map generator
	// Have to be called *after* session.numberOfTeam has been changed.
	void addTeam(void);
	void removeTeam(void);

	//! Grow ressources on map
	void growRessources(void);
	//! Do a step associated woth map (grow ressources and process bullets)
	void syncStep(Uint32 stepCounter);
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

	//! Set map to discovered state at position (x, y) for all teams in sharedVision (mask).
	void setMapDiscovered(int x, int y, Uint32 sharedVision)
	{
		(*(mapDiscovered+w*(y&hMask)+(x&wMask)))|=sharedVision;
		(*(fogOfWarA+w*(y&hMask)+(x&wMask)))|=sharedVision;
		(*(fogOfWarB+w*(y&hMask)+(x&wMask)))|=sharedVision;
	}

	//! Set map to discovered state at rect (x, y, w, h) for all teams in sharedVision (mask).
	void setMapDiscovered(int x, int y, int w, int h,  Uint32 sharedVision)
	{
		for (int dx=x; dx<x+w; dx++)
			for (int dy=y; dy<y+h; dy++)
				setMapDiscovered(dx, dy, sharedVision);
	}

	//! Make the building at (x, y) visible for all teams in sharedVision (mask).
	void setMapBuildingsDiscovered(int x, int y, Uint32 sharedVision, Team *teams[32])
	{
		Uint16 bgid=(cases+w*(y&hMask)+(x&wMask))->building;
		if (bgid!=NOGBID)
		{
			int id = Building::GIDtoID(bgid);
			int team = Building::GIDtoTeam(bgid);
			assert(id>=0);
			assert(id<1024);
			assert(team>=0);
			assert(team<32);
			teams[team]->myBuildings[id]->seenByMask|=sharedVision;
		}
	}

	//! Make the building at rect (x, y, w, h) visible for all teams in sharedVision (mask).
	void setMapBuildingsDiscovered(int x, int y, int w, int h, Uint32 sharedVision, Team *teams[32])
	{
		for (int dx=x; dx<x+w; dx++)
			for (int dy=y; dy<y+h; dy++)
				setMapBuildingsDiscovered(dx, dy, sharedVision, teams);
	}

	//! Set all map for all teams to undiscovered state
	void unsetMapDiscovered(void)
	{
		memset(mapDiscovered, 0, w*h*sizeof(Uint32));
	}

	//! Returs true if map is discovered at position (x,y) for a given vision mask.
	//! This mask represents which team's part of map we are allowed to see.
	bool isMapDiscovered(int x, int y, Uint32 visionMask)
	{
		return ((*(mapDiscovered+w*(y&hMask)+(x&wMask)))&visionMask)!=0;
	}

	//! Returs true if map is currently discovered at position (x,y) for a given vision mask.
	//! This mask represents which team's units and buildings we are allowed to see.
	bool isFOWDiscovered(int x, int y, int visionMask)
	{
		return ((*(fogOfWar+w*(y&hMask)+(x&wMask)))&visionMask)!=0;
	}
	
	//! Return true if the position (x,y) is a forbidden area set by the user
	bool isForbiddenLocal(int x, int y)
	{
		return localForbiddenMap.get(w*(y&hMask)+(x&wMask));
	}
	
	//! Return true if the position (x,y) is a guard area set by the user
	bool isGuardAreaLocal(int x, int y)
	{
		return localGuardAreaMap.get(w*(y&hMask)+(x&wMask));
	}
	
	//! Return true if the position (x,y) is a clear area set by the user
	bool isClearAreaLocal(int x, int y)
	{
		return localClearAreaMap.get(w*(y&hMask)+(x&wMask));
	}
	
	//! Compute localForbiddenMap from cases array
	void computeLocalForbidden(int localTeamNo);
	//! Compute localGuardAreaMap from cases array
	void computeLocalGuardArea(int localTeamNo);
	//! Compute localClearAreaMap from cases array
	void computeLocalClearArea(int localTeamNo);

	//! Return the terrain for a given coordinate
	inline Uint16 getTerrain(int x, int y)
	{
		return (*(cases+w*(y&hMask)+(x&wMask))).terrain;
	}
	
	//! Return the terrain for a gievn position in case array
	inline Uint16 getTerrain(unsigned pos)
	{
		return (cases+pos)->terrain;
	}

	//! Return the typeof terrain. If type is unregistred, returns unknown (-1).
	int getTerrainType(int x, int y)
	{
		unsigned t=getTerrain(x, y);
		if (t<16)
			return GRASS;
		else if ((t>=128)&&(t<128+16))
			return SAND;
		else if ((t>=256) && (t<256+16))
			return WATER;
		else
			return -1;
	}

	Ressource getRessource(int x, int y)
	{
		return (*(cases+w*(y&hMask)+(x&wMask))).ressource;
	}
	
	Ressource getRessource(unsigned pos)
	{
		return (cases+pos)->ressource;
	}
	
	Uint32 getForbidden(int x, int y)
	{
		return (*(cases+w*(y&hMask)+(x&wMask))).forbidden;
	}
	
	void setTerrain(int x, int y, Uint16 terrain)
	{
		(*(cases+w*(y&hMask)+(x&wMask))).terrain=terrain;
	}
	
	void setForbidden(int x, int y, Uint32 forbidden)
	{
		(*(cases+w*(y&hMask)+(x&wMask))).forbidden=forbidden;
	}
	
	bool isWater(int x, int y)
	{
		int t = getTerrain(x, y)-256;
		return ((t>=0) && (t<16));
	}
	
	bool isWater(unsigned pos)
	{
		int t = getTerrain(pos)-256;
		return ((t>=0) && (t<16));
	}

	bool isGrass(int x, int y)
	{
		return (getTerrain(x, y)<16);
	}

	bool isSand(int x, int y)
	{
		int t=getTerrain(x, y);
		return ((t>=128)&&(t<128+16));
	}

	bool isRessource(int x, int y)
	{
		return (*(cases+w*(y&hMask)+(x&wMask))).ressource.type!=NO_RES_TYPE;
	}

	bool isRessource(int x, int y, int ressourceType)
	{
		Ressource *ressource=&(*(cases+w*(y&hMask)+(x&wMask))).ressource;
		return (ressource->type == ressourceType &&  ressource->amount>0);
	}

	bool isRessource(int x, int y, bool ressourceTypes[BASIC_COUNT])
	{
		Ressource *ressource=&(*(cases+w*(y&hMask)+(x&wMask))).ressource;
		return (ressource->type!=NO_RES_TYPE
			&& ressource->amount>0
			&& ressource->type<BASIC_COUNT
			&& ressourceTypes[ressource->type]);
	}

	bool isRessource(int x, int y, int *ressourceType)
	{
		int rt=getRessource(x, y).type;
		if (rt==0xFF)
			return false;
		*ressourceType=rt;
		return true;
	}

	//! Decrement ressource at position (x,y). Return true on success, false otherwise.
	void decRessource(int x, int y);
	//! Decrement ressource at position (x,y) if ressource type = ressourceType. Return true on success, false otherwise.
	void decRessource(int x, int y, int ressourceType);
	bool incRessource(int x, int y, int ressourceType, int variety);
	
	//! Return true if unit can go to position (x,y)
	bool isFreeForGroundUnit(int x, int y, bool canSwim, Uint32 teamMask);
	bool isFreeForGroundUnitNoForbidden(int x, int y, bool canSwim);
	bool isFreeForAirUnit(int x, int y) {return (getAirUnit(x+w, y+h)==NOGUID); }
	bool isFreeForBuilding(int x, int y);
	bool isFreeForBuilding(int x, int y, int w, int h);
	bool isFreeForBuilding(int x, int y, int w, int h, Uint16 gid);
	// The "hardSpace" keywork means "Free" but you don't count Ground-Units as obstacles.
	bool isHardSpaceForGroundUnit(int x, int y, bool canSwim, Uint32 me);
	bool isHardSpaceForBuilding(int x, int y);
	bool isHardSpaceForBuilding(int x, int y, int w, int h);
	bool isHardSpaceForBuilding(int x, int y, int w, int h, Uint16 gid);
	
	//! Return true if unit has contact with building gbid. If true, put contact direction in dx, dy
	bool doesUnitTouchBuilding(Unit *unit, Uint16 gbid, int *dx, int *dy);
	//! Return true if (x,y) has contact with building gbid.
	bool doesPosTouchBuilding(int x, int y, Uint16 gbid);
	//! Return true if (x,y) has contact with building gbid. If true, put contact direction in dx, dy
	bool doesPosTouchBuilding(int x, int y, Uint16 gbid, int *dx, int *dy);
	
	//! Return true if unit has contact with ressource of any ressourceType. If true, put contact direction in dx, dy
	bool doesUnitTouchRessource(Unit *unit, int *dx, int *dy);
	//! Return true if unit has contact with ressource of type ressourceType. If true, put contact direction in dx, dy
	bool doesUnitTouchRessource(Unit *unit, int ressourceType, int *dx, int *dy);
	//! Return true if (x,y) has contact with ressource of type ressourceType. If true, put contact direction in dx, dy
	bool doesPosTouchRessource(int x, int y, int ressourceType, int *dx, int *dy);
	//! Return true if unit has contact with enemy. If true, put contact direction in dx, dy
	bool doesUnitTouchEnemy(Unit *unit, int *dx, int *dy);

	//! Return GID
	Uint16 getGroundUnit(int x, int y) { return (*(cases+w*(y&hMask)+(x&wMask))).groundUnit; }
	Uint16 getAirUnit(int x, int y) { return (*(cases+w*(y&hMask)+(x&wMask))).airUnit; }
	Uint16 getBuilding(int x, int y) { return (*(cases+w*(y&hMask)+(x&wMask))).building; }
	
	void setGroundUnit(int x, int y, Uint16 guid) { (*(cases+w*(y&hMask)+(x&wMask))).groundUnit=guid; }
	void setAirUnit(int x, int y, Uint16 guid) { (*(cases+w*(y&hMask)+(x&wMask))).airUnit=guid; }
	void setBuilding(int x, int y, int w, int h, Uint16 gbid)
	{
		for (int yi=y; yi<y+h; yi++)
			for (int xi=x; xi<x+w; xi++)
				(*(cases+this->w*(yi&hMask)+(xi&wMask))).building=gbid;
	}
	
	//! Return sector at (x,y).
	Sector *getSector(int x, int y) { return &(sectors[wSector*((y&hMask)>>4)+((x&wMask)>>4)]); }
	//! Return a sector in the sector array. It is not clean because too high level
	Sector *getSector(int i) { assert(i>=0); assert(i<sizeSector); return sectors+i; }

	//! Set undermap terrain type at (x,y) (undermap positions)
	void setUMTerrain(int x, int y, TerrainType t) { *(undermap+w*(y&hMask)+(x&wMask))=(Uint8)t; }
	//! Return undermap terrain type at (x,y)
	TerrainType getUMTerrain(int x, int y) { return (TerrainType)(*(undermap+w*(y&hMask)+(x&wMask))); }
	//! Set undermap terrain type at (x,y) (undermap positions) on an area
	void setUMatPos(int x, int y, TerrainType t, int l);

	//! With l==0, it will remove no ressource. (Unaligned coordinates)
	void setNoRessource(int x, int y, int l);
	//! With l==0, it will add ressource only on one case. (Aligned coordinates)
	void setRessource(int x, int y, int type, int l);
	bool isRessourceAllowed(int x, int y, int type);
	
	//! Transform coordinate from map scale (mx,my) to pixel scale (px,py)
	void mapCaseToPixelCase(int mx, int my, int *px, int *py) { *px=(mx<<5); *py=(my<<5); }
	//! Transform coordinate from map (mx,my) to screen (px,py)
	void mapCaseToDisplayable(int mx, int my, int *px, int *py, int viewportX, int viewportY);
	//! Transform coordinate from screen (mx,my) to map (px,py) for standard grid aligned object (buildings, ressources, units)
	void displayToMapCaseAligned(int mx, int my, int *px, int *py, int viewportX, int viewportY);
	//! Transform coordinate from screen (mx,my) to map (px,py) for standard grid unaligned object (terrain)
	void displayToMapCaseUnaligned(int mx, int my, int *px, int *py, int viewportX, int viewportY);
	//! Transform coordinate from screen (mx,my) to building (px,py)
	void cursorToBuildingPos(int mx, int my, int buildingWidth, int buildingHeight, int *px, int *py, int viewportX, int viewportY);
	//! Transform coordinate from building (px,py) to screen (mx,my)
	void buildingPosToCursor(int px, int py, int buildingWidth, int buildingHeight, int *mx, int *my, int viewportX, int viewportY);
	
	bool ressourceAvailable(int teamNumber, int ressourceType, bool canSwim, int x, int y);
	bool ressourceAvailable(int teamNumber, int ressourceType, bool canSwim, int x, int y, int *dist);
	bool ressourceAvailable(int teamNumber, int ressourceType, bool canSwim, int x, int y, Sint32 *targetX, Sint32 *targetY, int *dist);
	
	//! Starting from position (x, y) using gradient, returns the gradient destination in (targetX, targetY)
	bool getGlobalGradientDestination(Uint8 *gradient, int x, int y, Sint32 *targetX, Sint32 *targetY);
	
	Uint8 getGradient(int teamNumber, Uint8 ressourceType, bool canSwim, int x, int y)
	{
		Uint8 *gradient=ressourcesGradient[teamNumber][ressourceType][canSwim];
		assert(gradient);
		return *(gradient+(x&wMask)+(y&hMask)*w);
	}
	
	void updateGlobalGradientSmall(Uint8 *gradient);
	void updateGlobalGradientBig(Uint8 *gradient);
	void updateGlobalGradient(Uint8 *gradient);
	void updateRessourcesGradient(int teamNumber, Uint8 ressourceType, bool canSwim);
	bool directionFromMinigrad(Uint8 miniGrad[25], int *dx, int *dy, const bool strict, bool verbose);
	bool directionByMinigrad(Uint32 teamMask, bool canSwim, int x, int y, int *dx, int *dy, Uint8 *gradient, bool strict, bool verbose);
	bool directionByMinigrad(Uint32 teamMask, bool canSwim, int x, int y, int bx, int by, int *dx, int *dy, Uint8 localGradient[1024], bool strict, bool verbose);
	bool pathfindRessource(int teamNumber, Uint8 ressourceType, bool canSwim, int x, int y, int *dx, int *dy, bool *stopWork, bool verbose);
	void pathfindRandom(Unit *unit, bool verbose);
	
	void updateLocalGradient(Building *building, bool canSwim); //The 32*32 gradient
	void updateGlobalGradient(Building *building, bool canSwim); //The full-sized gradient
	//!A special gradient for clearing flags. Returns false if there is nothing to clear.
	bool updateLocalRessources(Building *building, bool canSwim); 
	void expandLocalGradient(Uint8 *gradient);
	
	bool buildingAvailable(Building *building, bool canSwim, int x, int y, int *dist);
	bool pathfindBuilding(Building *building, bool canSwim, int x, int y, int *dx, int *dy, bool verbose);
	bool pathfindLocalRessource(Building *building, bool canSwim, int x, int y, int *dx, int *dy); // Used for all ressources mixed in clearing flags.
	
	void dirtyLocalGradient(int x, int y, int wl, int hl, int teamNumber);
	bool pathfindForbidden(Uint8 *optionGradient, int teamNumber, bool canSwim, int x, int y, int *dx, int *dy, bool verbose);
	//! Find the best direction toward gaurd area, return true if one has been found, false otherwise
	bool pathfindGuardArea(int teamNumber, bool canSwim, int x, int y, int *dx, int *dy);
	//! Update the forbidden gradient, 
	void updateForbiddenGradient(int teamNumber, bool canSwim);
	void updateForbiddenGradient(int teamNumber);
	void updateForbiddenGradient();
	//! Update the guard area gradient
	void updateGuardAreasGradient(int teamNumber, bool canSwim);
	void updateGuardAreasGradient(int teamNumber);
	void updateGuardAreasGradient();
	
protected:
	// computationals pathfinding statistics:
	int ressourceAvailableCount[16][MAX_RESSOURCES];
	int ressourceAvailableCountSuccess[16][MAX_RESSOURCES];
	int ressourceAvailableCountFailure[16][MAX_RESSOURCES];
	
	int pathToRessourceCountTot;
	int pathToRessourceCountSuccess;
	int pathToRessourceCountFailure;
	
	int localRessourcesUpdateCount;
	
	int pathfindLocalRessourceCount;
	int pathfindLocalRessourceCountWait;
	int pathfindLocalRessourceCountSuccessBase;
	int pathfindLocalRessourceCountSuccessLocked;
	int pathfindLocalRessourceCountSuccessUpdate;
	int pathfindLocalRessourceCountSuccessUpdateLocked;
	int pathfindLocalRessourceCountFailureUnusable;
	int pathfindLocalRessourceCountFailureNone;
	int pathfindLocalRessourceCountFailureBad;
	
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

public:
	Case *cases;
	Sint32 w, h;
	Sint32 wMask, hMask;
	Sint32 wDec, hDec;
	
protected:
	// private functions, used for edition

	void regenerateMap(int x, int y, int w, int h);
	
	Uint16 lookup(Uint8 tl, Uint8 tr, Uint8 bl, Uint8 br);

public:
    // here we handle terrain
	// mapDiscovered
	bool arraysBuilt; // if true, the next pointers(arrays) have to be valid and filled.
	Uint32 *mapDiscovered;
	Uint32 *fogOfWar, *fogOfWarA, *fogOfWarB;
	//! true = forbidden
	Utilities::BitArray localForbiddenMap;
	//! true = guard area
	Utilities::BitArray localGuardAreaMap;
	//! true = clear area
	Utilities::BitArray localClearAreaMap;
	
public:
	// Used to go to ressources
	//[int team][int ressourceNumber][bool unitCanSwim][int mapX][int mapY]
	//255=ressource, 0=obstacle, the higher it is, the closest it is from the ressouce.
	Uint8 *ressourcesGradient[32][MAX_NB_RESSOURCES][2];
	
	// Used to go out of forbidden areas
	//[int team][bool unitCanSwim][int mapX][int mapY]
	Uint8 *forbiddenGradient[32][2];
	
	// Used to attrack idle warriors into guard areas
	//[int team][bool unitCanSwim][int mapX][int mapY]
	Uint8 *guardAreasGradient[32][2];
	
protected:
	//Used for scheduling computation time.
	bool gradientUpdated[32][MAX_NB_RESSOURCES][2];
	Uint8 *undermap;
	size_t size;
	
	Sector *sectors;
	Sint32 wSector, hSector;
	int sizeSector;
	
	Game *game;

public:
	Sint32 checkSum(bool heavy);
	Sint32 warpDistSquare(int px, int py, int qx, int qy); //The distance between (px, py) and (qx, qy), warp-safe, but not rooted.
	Sint32 warpDistMax(int px, int py, int qx, int qy); //The max distance on x or y axis, between (px, py) and (qx, qy), warp-safe.
	bool isInLocalGradient(int ux, int uy, int bx, int by); //Return true if the unit @(ux, uy) is close enough of building @(bx, by).

public:
	void makeHomogenMap(TerrainType terrainType);
	void controlSand(void);
	void smoothRessources(int times);
	bool makeRandomMap(MapGenerationDescriptor &descriptor);
	void addRessourcesRandomMap(MapGenerationDescriptor &descriptor);
	bool makeIslandsMap(MapGenerationDescriptor &descriptor);
	void addRessourcesIslandsMap(MapGenerationDescriptor &descriptor);

protected:
	FILE *logFile;
};

#endif
 
