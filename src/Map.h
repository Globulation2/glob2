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

#include "Header.h"
#include "Unit.h"
#include "Building.h"
#include "Team.h"
#include "Header.h"
#include "Ressource.h"
#include <list>
#include "Sector.h"
#include "Gradient.h"

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

	Uint32 forbidden; // This is a mask, one bit by team, 0=forbidden case, 1=allowed case.
};

enum TerrainType
{
	WATER=0,
	SAND=1,
	GRASS=2,
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
	bool load(SDL_RWops *stream, SessionGame *sessionGame, Game *game=NULL);
	//! Save a map
	void save(SDL_RWops *stream);
	
	// add & remove teams, used by the map editor and the random map generator
	// Have to be called *after* session.numberOfTeam has been changed.
	void addTeam(void);
	void removeTeam(void);

	//! Grow ressources on map
	void growRessources(void);
	//! Do a step associated woth map (grow ressources and process bullets)
	void step(Sint32 stepCounter);
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
#ifdef DBG_ALL_MAP_DISCOVERED
		return true;
#else
		return ((*(fogOfWar+w*(y&hMask)+(x&wMask)))&visionMask)!=0;
#endif
	}

	inline Uint16 getTerrain(int x, int y)
	{
		return (*(cases+w*(y&hMask)+(x&wMask))).terrain;
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
	void setForbiddenArea(int x, int y, int r, Uint32 me);
	void clearForbiddenArea(int x, int y, int r, Uint32 me);
	void clearForbiddenArea(Uint32 me);

	inline bool isWater(int x, int y)
	{
		int t=getTerrain(x, y)-256;
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
		return getRessource(x, y).id != NORESID;
	}

	bool isRemovableRessource(int x, int y)
	{
		Ressource r=getRessource(x, y);
		if (r.id==NORESID)
			return false;
		Uint8 t=r.field.type;
		return (t<BASIC_COUNT && t!=STONE);
	}

	bool isRessource(int x, int y, int ressourceType)
	{
		return getRessource(x, y).field.type == ressourceType;
	}

	bool isRessource(int x, int y, int *ressourceType)
	{
		int rt=getRessource(x, y).field.type;
		if (rt==0xFF)
			return false;
		*ressourceType=rt;
		return true;
	}

	//! Decrement ressource at position (x,y). Return true on success, false otherwise.
	bool decRessource(int x, int y);
	//! Decrement ressource at position (x,y) if ressource type = ressourceType. Return true on success, false otherwise.
	bool decRessource(int x, int y, int ressourceType);
	bool incRessource(int x, int y, int ressourceType);
	
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
	bool doesUnitTouchRemovableRessource(Unit *unit, int *dx, int *dy);
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
	void setUMatPos(int x, int y, TerrainType t, int size);

	//! With size==0, it will remove no ressource. (Unaligned coordinates)
	void setNoRessource(int x, int y, int size);
	//! With size==0, it will add ressource only on one case. (Aligned coordinates)
	void setRessource(int x, int y, int type, int size);
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
	
	bool ressourceAviable(int teamNumber, int ressourceType, bool canSwim, int x, int y);
	bool ressourceAviable(int teamNumber, int ressourceType, bool canSwim, int x, int y, int *dist);
	bool ressourceAviable(int teamNumber, int ressourceType, bool canSwim, int x, int y, Sint32 *targetX, Sint32 *targetY, int *dist);
	
	/*Uint8 distToRessource(int teamNumber, Uint8 ressourceType, bool canSwim, int x, int y)
	{
		Uint8 *gradient=ressourcesGradient[teamNumber][ressourceType][canSwim];
		assert(gradient);
		return 254-*(gradient+(x&wMask)+(y&hMask)*w);
	}*/
	Uint8 getGradient(int teamNumber, Uint8 ressourceType, bool canSwim, int x, int y)
	{
		Uint8 *gradient=ressourcesGradient[teamNumber][ressourceType][canSwim];
		assert(gradient);
		return *(gradient+(x&wMask)+(y&hMask)*w);
	}
	
	void updateGlobalGradient(Uint8 *gradient);
	void updateGradient(int teamNumber, Uint8 ressourceType, bool canSwim, bool init);
	bool directionFromMinigrad(Uint8 miniGrad[25], int *dx, int *dy);
	bool directionByMinigrad(Uint32 teamMask, bool canSwim, int x, int y, int *dx, int *dy, Uint8 *gradient, bool *gradientUsable);
	bool directionByMinigrad(Uint32 teamMask, bool canSwim, int x, int y, int bx, int by, int *dx, int *dy, Uint8 localGradient[1024], bool *gradientUsable);
	bool pathfindRessource(int teamNumber, Uint8 ressourceType, bool canSwim, int x, int y, int *dx, int *dy, bool *stopWork);
	
	void updateLocalGradient(Building *building, bool canSwim); //The 32*32 gradient
	void updateGlobalGradient(Building *building, bool canSwim); //The full-sized gradient
	void updateLocalRessources(Building *building, bool canSwim); //A special gradient for clearing flags
	void expandLocalGradient(Uint8 *gradient);
	
	bool buildingAviable(Building *building, bool canSwim, int x, int y, int *dist);
	bool pathfindBuilding(Building *building, bool canSwim, int x, int y, int *dx, int *dy);
	bool pathfindLocalRessource(Building *building, bool canSwim, int x, int y, int *dx, int *dy); // Used for all ressources mixed in clearing flags.
	
	void dirtyLocalGradient(int x, int y, int wl, int hl, int teamNumber);
	
protected:
	// computationals pathfinding statistics:
	int ressourceAviableCount[16][MAX_RESSOURCES];
	int ressourceAviableCountFast[16][MAX_RESSOURCES];
	int ressourceAviableCountFar[16][MAX_RESSOURCES];
	int ressourceAviableCountSuccess[16][MAX_RESSOURCES];
	int ressourceAviableCountFailure[16][MAX_RESSOURCES];
	
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
	int pathToBuildingCountCloseSuccessAround;
	int pathToBuildingCountCloseSuccessUpdated;
	int pathToBuildingCountCloseSuccessUpdatedAround;
	int pathToBuildingCountCloseFailureLocked;
	int pathToBuildingCountCloseFailureEnd;
	
	int pathToBuildingCountIsFar;
	int pathToBuildingCountFar;
	int pathToBuildingCountFarOldSuccess;
	int pathToBuildingCountFarOldFailureLocked;
	int pathToBuildingCountFarOldFailureBad;
	int pathToBuildingCountFarOldFailureUnusable;
	int pathToBuildingCountFarUpdateSuccess;
	int pathToBuildingCountFarUpdateSuccessAround;
	int pathToBuildingCountFarUpdateFailureLocked;
	int pathToBuildingCountFarUpdateFailureVirtual;
	int pathToBuildingCountFarUpdateFailureBad;
	
	int localBuildingGradientUpdate;
	int localBuildingGradientUpdateLocked;
	int globalBuildingGradientUpdate;
	int globalBuildingGradientUpdateLocked;
	
	int buildingAviableCountTot;
	
	int buildingAviableCountClose;
	int buildingAviableCountCloseSuccess;
	int buildingAviableCountCloseSuccessAround;
	int buildingAviableCountCloseSuccessUpdate;
	int buildingAviableCountCloseSuccessUpdateAround;
	int buildingAviableCountCloseFailureLocked;
	int buildingAviableCountCloseFailureEnd;
	
	int buildingAviableCountIsFar;
	int buildingAviableCountFar;
	int buildingAviableCountFarNew;
	int buildingAviableCountFarNewSuccess;
	int buildingAviableCountFarNewSuccessClosely;
	int buildingAviableCountFarNewFailureLocked;
	int buildingAviableCountFarNewFailureVirtual;
	int buildingAviableCountFarNewFailureEnd;
	int buildingAviableCountFarOld;
	int buildingAviableCountFarOldSuccess;
	int buildingAviableCountFarOldSuccessAround;
	int buildingAviableCountFarOldFailureLocked;
	int buildingAviableCountFarOldFailureEnd;

protected:
	// private functions, used for edition

	void regenerateMap(int x, int y, int w, int h);
	
	Uint16 lookup(Uint8 tl, Uint8 tr, Uint8 bl, Uint8 br);

    // here we handle terrain
	// mapDiscovered
	bool arraysBuilt; // if true, the next pointers(arrays) have to be valid and filled.
	Uint32 *mapDiscovered;
	Uint32 *fogOfWar, *fogOfWarA, *fogOfWarB;
	Case *cases;
	//[int team][int ressourceNumber][bool unitCanSwim][int mapX][int mapY]
	//255=ressource, 0=obstacle, the higher it is, the closest it is from the ressouce.
	Uint8 *ressourcesGradient[32][MAX_NB_RESSOURCES][2];
	//Used for scheduling computation time. (if==0) has to be fully recomputed, (if>0) number of depth already computed.
	int gradientUpdatedDepth[32][MAX_NB_RESSOURCES][2];
	Uint8 *undermap;
	Sint32 w, h; //in cases
	int size;
	
	Sint32 wMask, hMask;
	Sint32 wDec, hDec;
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
 
