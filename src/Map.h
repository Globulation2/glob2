/*
 * Globulation 2 map support
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#ifndef __MAP_H
#define __MAP_H

#include "GAG.h"
#include "Unit.h"
#include "Building.h"
#include "Team.h"
#include <list>

#define NOUID (Sint16)0x8000
class Map;
class Game;

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
	
	void free(void);
	
	std::list<Bullet *> bullets;

	void save(SDL_RWops *stream);
	void load(SDL_RWops *stream, Game *game);
	
	void step(void);
	
	Map *map;
	Game *game;
};

class BaseMap: public Order
{
public:
	BaseMap();
	virtual ~BaseMap(void) { }

	char mapName[32];
protected:
	char data[32];

public:
	bool load(SDL_RWops *stream);
	void save(SDL_RWops *stream);
	
	Uint8 getOrderType();
	char *getData();
	bool setData(const char *data, int dataLength);
	int getDataLength();
	virtual Sint32 checkSum();
};

// map container
class Map:public BaseMap
{
public:
	enum TerrainType
	{
		WATER=0,
		SAND=1,
		GRASS=2
	};
	
public:
	Map();
	virtual ~Map(void);

	void setBaseMap(const BaseMap *initial);
	void setSize(int wDec, int hDec, Game *game);
	void save(SDL_RWops *stream);
	bool load(SDL_RWops *stream, Game *game);
	
	void growRessources(void);
	void step(void);
	void switchFogOfWar(void);

	int getW(void) { return w; }
	int getH(void) { return h; }
	int getMaskW(void) { return wMask; }
	int getMaskH(void) { return hMask; }

	void setMapDiscovered(int x, int y, int p)
	{
		(*(mapDiscovered+w*(y&hMask)+(x&wMask)))|=(1<<p);
		(*(fogOfWarA+w*(y&hMask)+(x&wMask)))|=(1<<p);
		(*(fogOfWarB+w*(y&hMask)+(x&wMask)))|=(1<<p);
	}
	void unsetMapDiscovered(int x, int y, int p)
	{
		(*(mapDiscovered+w*(y&hMask)+(x&wMask)))&=~(1<<p);
		(*(fogOfWarA+w*(y&hMask)+(x&wMask)))&=~(1<<p);
		(*(fogOfWarB+w*(y&hMask)+(x&wMask)))&=~(1<<p);
	}
	bool isMapDiscovered(int x, int y, int p)
	{
		return ((*(mapDiscovered+w*(y&hMask)+(x&wMask)))&(1<<p))!=0;
	}
	/*void setFOW(int x, int y, int p) { (*(fogOfWar+w*(y&hMask)+(x&wMask)))|=(1<<p); }
	void unsetFOW(int x, int y, int p) { (*(fogOfWar+w*(y&hMask)+(x&wMask)))&=~(1<<p); }*/
	bool isFOW(int x, int y, int p)
	{
		return ((*(fogOfWar+w*(y&hMask)+(x&wMask)))&(1<<p))!=0;
	}
	void setMapDiscovered(int x, int y, int w, int h, int p) { for (int dx=x; dx<x+w; dx++) for (int dy=y; dy<y+h; dy++) setMapDiscovered(dx, dy, p); }
	void unsetMapDiscovered(int x, int y, int w, int h, int p) { for (int dx=x; dx<x+w; dx++) for (int dy=y; dy<y+h; dy++) unsetMapDiscovered(dx, dy, p); }
	// NOTE : unused now
	//void isMapDescovered(int x, int y, int w, int h, int p) { for (int dx=x; dx<x+w; dx++) for (int dy=y; dy<y+h; dy++) if (isMapDiscovered(dx, dy, p)) return true; return false; }

	Uint16 getTerrain(int x, int y) { return (*(cases+w*(y&hMask)+(x&wMask))).terrain; }
	void setTerrain(int x, int y, Uint16 t) { (*(cases+w*(y&hMask)+(x&wMask))).terrain=t; }
	bool isWater(int x, int y);
	bool isGrass(int x, int y);
	bool isGrowableRessource(int x, int y);
	bool isRessource(int x, int y);
	bool isRessource(int x, int y, RessourceType ressourceType);
	bool decRessource(int x, int y);
	bool decRessource(int x, int y, RessourceType ressourceType);
	bool isFreeForUnit(int x, int y, bool canFly);
	bool doesUnitTouchUID(Unit *unit, Sint16 otherUID, int *dx, int *dy);
	bool doesPosTouchUID(int x, int y, Sint16 otherUID);
	bool doesPosTouchUID(int x, int y, Sint16 otherUID, int *dx, int *dy);
	bool doesUnitTouchRessource(Unit *unit, RessourceType ressourceType, int *dx, int *dy);
	bool doesPosTouchRessource(int x, int y, RessourceType ressourceType, int *dx, int *dy);
	bool doesUnitTouchEnemy(Unit *unit, int *dx, int *dy);

	Sint16 getUnit(int x, int y) { return (*(cases+w*(y&hMask)+(x&wMask))).unit; }
	Sector *getSector(int x, int y) { return &(sectors[wSector*((y&hMask)>>4)+((x&wMask)>>4)]); }
	void setUnit(int x, int y, Sint16 u) { (*(cases+w*(y&hMask)+(x&wMask))).unit=u; }
	void setBuilding(int x, int y, int w, int h, Sint16 u);

	void setUMTerrain(int x, int y, TerrainType t) { *(undermap+w*(y&hMask)+(x&wMask))=(Uint8)t; }
	TerrainType getUMTerrain(int x, int y) { return (TerrainType)(*(undermap+w*(y&hMask)+(x&wMask))); }

	void setUMatPos(int x, int y, TerrainType t, int size);
	void setResAtPos(int x, int y, int type, int size);
	
	static void mapCaseToPixelCase(int mx, int my, int *px, int *py) { *px=(mx<<5); *py=(my<<5); }
	void mapCaseToDisplayable(int mx, int my, int *px, int *py, int viewportX, int viewportY);
	void displayToMapCaseAligned(int mx, int my, int *px, int *py, int viewportX, int viewportY);
	void displayToMapCaseUnaligned(int mx, int my, int *px, int *py, int viewportX, int viewportY);
	void cursorToBuildingPos(int mx, int my, int buildingWidth, int buildingHeight, int *px, int *py, int viewportX, int viewportY);
	void buildingPosToCursor(int px, int py, int buildingWidth, int buildingHeight, int *mx, int *my, int viewportX, int viewportY);

	bool nearestRessource(int x, int y, RessourceType ressourceType, int *dx, int *dy);
	
//private:
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

private:
	Sint32 stepCounter;
	int sizeOfFogOfWar;
};

#endif
 
