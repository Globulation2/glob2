/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charriere
    for any question or comment contact us at nct@ysagoon.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#ifndef __MAP_H
#define __MAP_H

#include "GAG.h"
#include "Unit.h"
#include "Building.h"
#include "Team.h"
#include "Header.h"
#include <list>

#define NOUID (Sint16)0x8000
class Map;
class Game;
class MapGenerationDescriptor;

// a 1x1 piece of map
struct Case
{
	Uint16 terrain;
	Sint16 unit; //this includes units, buildings and flags. (called UID in Builging.h)
	             //Units are positives, 0xFFFF is an empty case.

	Uint32 getInteger(void)
	{
		return ((terrain<<16) | (Uint16)unit);
	}
	void setInteger(Uint32 i)
	{
		terrain=(i>>16);
		unit=(i & 0xFFFF);
	}
};

// a 16x16 piece of Map
class Sector
{
public:
	Sector() {}
	Sector(Game *);
	virtual ~Sector(void);
	// !This call is needed to use the Sector!
	void setGame(Game *game);

	void free(void);
	
	std::list<Bullet *> bullets;

	void save(SDL_RWops *stream);
	bool load(SDL_RWops *stream, Game *game);
	
	void step(void);
private:
	Map *map;
	Game *game;
};

class BaseMap: public Order
{
public:
	BaseMap();
	virtual ~BaseMap(void) { }

	enum { MAP_NAME_MAX_SIZE=32 };

protected:
	char mapName[MAP_NAME_MAX_SIZE];
	char mapFileName[MAP_NAME_MAX_SIZE+4];//This is not saved in file
	char gameFileName[MAP_NAME_MAX_SIZE+4];//This is not saved in file

public:
	//! Safely copy s to mapName[] and remove the extention if needed.
	void setMapName(const char *s);
	const char *getMapName() const;
	const char *getMapFileName() const;
	const char *getGameFileName() const;
protected:
	//! serialized form of BaseMap
	char data[MAP_NAME_MAX_SIZE];

public:
	bool load(SDL_RWops *stream);
	void save(SDL_RWops *stream);
	
	Uint8 getOrderType();
	char *getData();
	bool setData(const char *data, int dataLength);
	int getDataLength();
	virtual Sint32 checkSum();
};

//! Map, handle all physical localisations
/*!
	When not specified, all size are given in 32x32 pixel cell, which is the basic
	game measurement unit.
	All functions are wrap-safe, excepted the one specified otherwise.
*/
class Map:public BaseMap
{
public:
	//! Type of terrain
	enum TerrainType
	{
		WATER=0,
		SAND=1,
		GRASS=2
	};

public:
	//! Map constructor
	Map();
	//! Map destructor
	virtual ~Map(void);

	//! Set the base map (name and initial infos)
	void setBaseMap(const BaseMap *initial);
	//! Reset map size to width = 2^wDec and height=2^hDec, and fill background with terrainType
	void setSize(int wDec, int hDec, TerrainType terrainType=WATER);
	// !This call is needed to use the Map!
	void setGame(Game *game);
	//! Save a map
	void save(SDL_RWops *stream);
	//! Load a map from a stream and relink with associated game
	bool load(SDL_RWops *stream, Game *game=NULL);

	//! Grow ressources on map
	void growRessources(void);
	//! Do a step associated woth map (grow ressources and process bullets)
	void step(void);
	//! Switch the Fog of War buffer
	void switchFogOfWar(void);

	//! Return map width
	int getW(void) { return w; }
	//! Return map height
	int getH(void) { return h; }
	//! Return map width mask
	int getMaskW(void) { return wMask; }
	//! Return map height mask
	int getMaskH(void) { return hMask; }
	//! Return the number of sectors on x, which corresponds to the sector map width
	int getSectorW(void) { return wSector; }
	//! Return the number of sectors on y, which corresponds to the sector map height
	int getSectorH(void) { return hSector; }

	//! Set map to discovered state at position (x,y) for team p
	void setMapDiscovered(int x, int y, int p)
	{
		(*(mapDiscovered+w*(y&hMask)+(x&wMask)))|=(1<<p);
		(*(fogOfWarA+w*(y&hMask)+(x&wMask)))|=(1<<p);
		(*(fogOfWarB+w*(y&hMask)+(x&wMask)))|=(1<<p);
	}
	//! Set map to undiscovered state at position (x,y) for the shared vision mask of team ps
	void unsetMapDiscovered(int x, int y, int p)
	{
		(*(mapDiscovered+w*(y&hMask)+(x&wMask)))&=~(1<<p);
		(*(fogOfWarA+w*(y&hMask)+(x&wMask)))&=~(1<<p);
		(*(fogOfWarB+w*(y&hMask)+(x&wMask)))&=~(1<<p);
	}
	//! Returs true if map is discovered at position (x,y) for the shared vision mask of team ps
	bool isMapDiscovered(int x, int y, int visionMask)
	{
#ifdef DBG_ALL_MAP_DISCOVERED
		return true;
#else
		return ((*(mapDiscovered+w*(y&hMask)+(x&wMask)))&visionMask)!=0;
#endif
	}
	/*void setFOW(int x, int y, int p) { (*(fogOfWar+w*(y&hMask)+(x&wMask)))|=(1<<p); }
	void unsetFOW(int x, int y, int p) { (*(fogOfWar+w*(y&hMask)+(x&wMask)))&=~(1<<p); }*/
	//! Return true if FOW (Fog of War) is not set at position (x,y) for team p (the function name is illogic, should be isFOWfree )
	bool isFOW(int x, int y, int visionMask)
	{
#ifdef DBG_ALL_MAP_DISCOVERED
		return true;
#else
		return ((*(fogOfWar+w*(y&hMask)+(x&wMask)))&visionMask)!=0;
#endif
	}
	//! Set map to discovered state at rect (x,y) - (x+w, y+h) for team p
	void setMapDiscovered(int x, int y, int w, int h, int p) { for (int dx=x; dx<x+w; dx++) for (int dy=y; dy<y+h; dy++) setMapDiscovered(dx, dy, p); }
	//! Set map to undiscovered state at rect (x,y) - (x+w, y+h) for team p
	void unsetMapDiscovered(int x, int y, int w, int h, int p) { for (int dx=x; dx<x+w; dx++) for (int dy=y; dy<y+h; dy++) unsetMapDiscovered(dx, dy, p); }
	//! Set all map to undiscovered state
	void unsetMapDiscovered(void) { memset(mapDiscovered, 0, w*h*sizeof(Uint32)); }
	// NOTE : unused now
	//void isMapDescovered(int x, int y, int w, int h, int p) { for (int dx=x; dx<x+w; dx++) for (int dy=y; dy<y+h; dy++) if (isMapDiscovered(dx, dy, p)) return true; return false; }

	//! Return the terrain type at position (x,y).
	Uint16 getTerrain(int x, int y) { return (*(cases+w*(y&hMask)+(x&wMask))).terrain; }
	//! Set the terrain type at position (x,y).
	void setTerrain(int x, int y, Uint16 t) { (*(cases+w*(y&hMask)+(x&wMask))).terrain=t; }
	bool isWaterOrAlga(int x, int y);
	bool isWater(int x, int y);
	bool isGrass(int x, int y);
	bool isSand(int x, int y);
	bool isGrowableRessource(int x, int y);
	bool isRessource(int x, int y);
	bool isRessource(int x, int y, RessourceType  ressourceType);
	bool isRessource(int x, int y, RessourceType *ressourceType);
	
	//! Decrement ressource at position (x,y). Return true on success, false otherwise.
	bool decRessource(int x, int y);
	//! Decrement ressource at position (x,y) if ressource type = ressourceType. Return true on success, false otherwise.
	bool decRessource(int x, int y, RessourceType ressourceType);
	//! Return true if unit can go to position (x,y)
	bool isFreeForUnit(int x, int y, bool canFly);
	//! Return true if unit has contact with otherUID. If true, put contact direction in dx, dy
	bool doesUnitTouchUID(Unit *unit, Sint16 otherUID, int *dx, int *dy);
	//! Return true if (x,y) has contact with otherUID.
	bool doesPosTouchUID(int x, int y, Sint16 otherUID);
	//! Return true if (x,y) has contact with otherUID. If true, put contact direction in dx, dy
	bool doesPosTouchUID(int x, int y, Sint16 otherUID, int *dx, int *dy);
	//! Return true if unit has contact with ressource of any ressourceType. If true, put contact direction in dx, dy
	bool doesUnitTouchRessource(Unit *unit, int *dx, int *dy);
	//! Return true if unit has contact with ressource of type ressourceType. If true, put contact direction in dx, dy
	bool doesUnitTouchRessource(Unit *unit, RessourceType ressourceType, int *dx, int *dy);
	//! Return true if (x,y) has contact with ressource of type ressourceType. If true, put contact direction in dx, dy
	bool doesPosTouchRessource(int x, int y, RessourceType ressourceType, int *dx, int *dy);
	//! Return true if unit has contact with enemy. If true, put contact direction in dx, dy
	bool doesUnitTouchEnemy(Unit *unit, int *dx, int *dy);

	//! Return unit or building UID at (x,y). Return NOUID if none
	Sint16 getUnit(int x, int y) { return (*(cases+w*(y&hMask)+(x&wMask))).unit; }
	//! Return sector at (x,y).
	Sector *getSector(int x, int y) { return &(sectors[wSector*((y&hMask)>>4)+((x&wMask)>>4)]); }
	//! Return a sector in the sector array. It is not clean because too high level
	Sector *getSector(int i) { assert(i>=0); assert(i<wSector*hSector); return sectors+i; }

	//! Set unit or building at (x,y) to UID u
	void setUnit(int x, int y, Sint16 u) { (*(cases+w*(y&hMask)+(x&wMask))).unit=u; }
	//! Set building at rect (x,y) - (x+w,y+h) to UID u
	void setBuilding(int x, int y, int w, int h, Sint16 u);
	//! Set undermap terrain type at (x,y) (undermap positions)
	void setUMTerrain(int x, int y, TerrainType t) { *(undermap+w*(y&hMask)+(x&wMask))=(Uint8)t; }
	//! Return undermap terrain type at (x,y)
	TerrainType getUMTerrain(int x, int y) { return (TerrainType)(*(undermap+w*(y&hMask)+(x&wMask))); }
	//! Set undermap terrain type at (x,y) (undermap positions) on an area
	void setUMatPos(int x, int y, TerrainType t, int size);
	//! Set ressourcse at (x,y) on an area
	void setResAtPos(int x, int y, int type, int size);
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
	//! Return the nearest ressource from (x,y) for type ressourceType. The position is returned in (dx,dy)
	bool nearestRessource(int x, int y, RessourceType  ressourceType, int *dx, int *dy);
	bool nearestRessource(int x, int y, RessourceType *ressourceType, int *dx, int *dy);
	bool nearestRessourceInCircle(int x, int y, int fx, int fy, int fsr, int *dx, int *dy);

protected:
	// private functions, used for edition

	void regenerateMap(int x, int y, int w, int h);

	Uint16 lookup(Uint8 tl, Uint8 tr, Uint8 bl, Uint8 br);

    // here we handle terrain
	Uint32 *mapDiscovered;
	Uint32 *fogOfWar, *fogOfWarA, *fogOfWarB;
	Case *cases;
	Uint8 *undermap;
	Sint32 w, h; //in cases
	Sint32 wMask, hMask;
	Sint32 wDec, hDec;
	Sector *sectors;
	Sint32 wSector, hSector;

public:
	Sint32 checkSum();
	int warpDistSquare(int px, int py, int qx, int qy);

protected:
	Sint32 stepCounter;
	int sizeOfFogOfWar;

public:
	void makeHomogenMap(Map::TerrainType terrainType);
	void controlSand(void);
	void makeRandomMap(MapGenerationDescriptor &descriptor);
	void makeIslandsMap(MapGenerationDescriptor &descriptor);
	void addRessources(MapGenerationDescriptor &descriptor);
};

#endif
 
