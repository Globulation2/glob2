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

#include "Map.h"
#include "Unit.h"
#include "Game.h"
#include "Utilities.h"

BaseMap::BaseMap()
{
	strncpy(mapName,"DEBUG MAP", MAP_NAME_MAX_SIZE);
}

void BaseMap::save(SDL_RWops *stream)
{
	SDL_RWwrite(stream, "GLO2", 4, 1);
	SDL_RWwrite(stream, mapName, 32, 1);
	SDL_RWwrite(stream, "GLO2", 4, 1);
}

bool BaseMap::load(SDL_RWops *stream)
{
	char signature[4];
	SDL_RWread(stream, signature, 4, 1);
	if (memcmp(signature,"GLO2",4)!=0)
		return false;
	
	SDL_RWread(stream, mapName, MAP_NAME_MAX_SIZE, 1);
	
	SDL_RWread(stream, signature, 4, 1);
	if (memcmp(signature,"GLO2",4)!=0)
		return false;
	
	return true;
}

/*const*/void BaseMap::setMapName(/*const*/ char *s)
{
	strncpy(mapName, s, MAP_NAME_MAX_SIZE);
	mapName[MAP_NAME_MAX_SIZE-1]=0;
	char *c=strrchr(mapName, '.');
	if (c)
		*c=0;
	//printf("(set)mapName=(%s), s=(%s).\n", mapName, s);
}

/*const*/char *BaseMap::getMapName()
{
	//printf("(get)mapName=(%s).\n", mapName);
	return mapName;
}

/*const*/char *BaseMap::getMapFileName()
{
	strncpy(mapFileName, mapName, MAP_NAME_MAX_SIZE);
	mapFileName[MAP_NAME_MAX_SIZE-1]=0;
	int l=strlen(mapName);
	strncpy(&mapFileName[l], ".map", 5);
	mapFileName[MAP_NAME_MAX_SIZE+4-1]=0;
	//printf("mapFileName=(%s), mapName=(%s).\n", mapFileName, mapName);
	return mapFileName;
}

Uint8 BaseMap::getOrderType()
{
	return DATA_BASE_MAP;
}

char *BaseMap::getData()
{
	memcpy(data, mapName, MAP_NAME_MAX_SIZE);
	return data;
}


bool BaseMap::setData(const char *data, int dataLength)
{
	if (dataLength!=getDataLength())
		return false;
	
	memcpy(mapName, data, MAP_NAME_MAX_SIZE);
	
	return true;
}

int BaseMap::getDataLength()
{
	return 32;
}

Sint32 BaseMap::checkSum()
{
	Sint32 cs=0;

	{
		for (int i=0; i<(int)strlen(mapName); i++)
		{
			cs^=mapName[i];
			cs=(cs<<31)|(cs>>1);
		}
	}
	return cs;
}

Sector::Sector(Game *game)
{
	this->game=game;
	this->map=&(game->map);
}

Sector::~Sector(void)
{
	free();
}

void Sector::free(void)
{
	{
		for (std::list<Bullet *>::iterator it=bullets.begin();it!=bullets.end();it++)
			delete (*it);
	}
	bullets.clear();
	game=NULL;
	map=NULL;
}

void Sector::save(SDL_RWops *stream)
{
	SDL_WriteBE32(stream, bullets.size());
	// we write the number of bullets here
	{
		for (std::list<Bullet *>::iterator it=bullets.begin();it!=bullets.end();it++)
			(*it)->save(stream);
	}
}

void Sector::load(SDL_RWops *stream, Game *game)
{
	int nbUsed;

	free();
	nbUsed=SDL_ReadBE32(stream);
	{
		for (int i=0; i<nbUsed; i++)
		{
			bullets.push_front(new Bullet(stream));
		}
	}
	this->game=game;
	this->map=&(game->map);
}

void Sector::step(void)
{
	std::list<Bullet*>::iterator ittemp;
	
	for (std::list<Bullet *>::iterator it=bullets.begin();it!=bullets.end();++it)
	{
		if ( (*it)->ticksLeft > 0 )
		{
			(*it)->step();
		}
		else
		{
			int UID=map->getUnit((*it)->targetX, (*it)->targetY);
			if (UID>=0)
			{
				int team=Unit::UIDtoTeam(UID);
				int id=Unit::UIDtoID(UID);

				game->teams[team]->setEvent((*it)->targetX, (*it)->targetY, Team::UNIT_UNDER_ATTACK_EVENT);
				game->teams[team]->myUnits[id]->hp-=(*it)->shootDamage;
			}
			else if (UID!=NOUID)
			{
				int team=Building::UIDtoTeam(UID);
				int id=Building::UIDtoID(UID);

				game->teams[team]->setEvent((*it)->targetX, (*it)->targetY, Team::BUILDING_UNDER_ATTACK_EVENT);
				Building *b=game->teams[team]->myBuildings[id];
				int damage=(*it)->shootDamage-b->type->armor; 
				if (damage>0)
					b->hp-=damage;
				else
					b->hp--;
				if (b->hp<=0)
					b->kill();

				//printf("bullet hitted building %d \n", (int)b);
			}

			ittemp=it;
			it=bullets.erase(ittemp);
		}
	}
}

Map::Map()
:BaseMap()
{
	mapDiscovered=NULL;
	fogOfWarA=NULL;
	fogOfWarB=NULL;
	fogOfWar=NULL;
	sizeOfFogOfWar=0;
	stepCounter=0;
	cases=NULL;
	sectors=NULL;
	undermap=NULL;
	w=0;
	h=0;
	wMask=0;
	hMask=0;
	wDec=0;
	hDec=0;
	wSector=0;
	hSector=0;
}

Map::~Map(void)
{
	if (mapDiscovered)
		delete mapDiscovered;
	if (fogOfWarA)
		delete[] fogOfWarA;
	if (fogOfWarB)
		delete[] fogOfWarB;
	fogOfWar=NULL;
	stepCounter=0;
	sizeOfFogOfWar=0;
	if (cases)
		delete[] cases;
	if (sectors)
		delete[] sectors;
	if (undermap)
		delete[] undermap;
}

bool Map::isWaterOrAlga(int x, int y)
{
	int t=getTerrain(x, y);
	return ((t>=256)&&(t<256+16)) || ((t>=302)&&(t<312));
}

bool Map::isWater(int x, int y)
{
	int t=getTerrain(x, y);
	return ((t>=256) && (t<256+16));
}

bool Map::isGrass(int x, int y)
{
	return (getTerrain(x, y)<16);
}

bool Map::isSand(int x, int y)
{
	int t=getTerrain(x, y);
	return ((t>=128)&&(t<128+16));
}

bool Map::isGrowableRessource(int x, int y)
{
	int d=getTerrain(x, y)-272;
	int r=d/10;
	if ((d<0)||(d>=40))
		return false;
	return (r!=STONE);
}

bool Map::isRessource(int x, int y)
{
	return (getTerrain(x, y)>=272);
}

bool Map::isRessource(int x, int y, RessourceType ressourceType)
{
	// TODO : avoid the division !
	int d=getTerrain(x, y)-272;
	if (d<0)
		return false;
	else
		return ( (d/10) == ressourceType );
}

bool Map::isRessource(int x, int y, RessourceType *ressourceType)
{
	int d=getTerrain(x, y)-272;
	if ((d<0)||(d>=40))
		return false;
	else
	{
		*ressourceType=(RessourceType)(d/10);
		return true;
	}
}

bool Map::decRessource(int x, int y)
{
	int d=getTerrain(x, y)-272;
	if ((d<0)||(d>=40))
		return false;
	int r=d/10;
	int l=d%5;
	if ((r==CORN)||(r==STONE)) // those are the slow-consuming ressources.
	{
		if (l>0)
			setTerrain(x, y, d+271);
		else if (l==0)
			setTerrain(x, y, syncRand()&0xF);
	}
	else if (r==WOOD)
		setTerrain(x, y, syncRand()&0xF);
	else if (r==ALGA)
		setTerrain(x, y, 256+(syncRand()&0xF));
	else
		assert(false);
	return true;
}

bool Map::decRessource(int x, int y, RessourceType ressourceType)
{
	// this is the clean way :
	if (isRessource(x, y, ressourceType))
		return decRessource(x, y);
	else
		return false;
	// this is perhaps faster but I don't think so (more cache used) :
	/*
	int d=getTerrain(x, y)-272;
	if ((d<0)||(d>=40))
		return false;
	int r=d/10;
	int l=d%5;
	if (r==ressourceType)
	{
		if ((r==CORN)||(r==STONE)) // those are the slow-consuming ressources.
		{
			if (l>0)
				setTerrain(x, y, d+271);
			else if (l==0)
				setTerrain(x, y, syncRand()&0xF);
		}
		else if (r==WOOD)
			setTerrain(x, y, syncRand()&0xF);
		else if (r==ALGA)
			setTerrain(x, y, 256+(syncRand()&0xF));
		else
			assert(false);
		return true;
	}
	else
		return false;
	*/
}

bool Map::isFreeForUnit(int x, int y, bool canFly)
{
	return ( ( (!isRessource(x, y)) || canFly) && (getUnit(x, y)==NOUID));
}

bool Map::doesUnitTouchUID(Unit *unit, Sint16 otherUID, int *dx, int *dy)
{
	int x=unit->posX;
	int y=unit->posY;
	int tdx, tdy;
	
	for (tdx=-1; tdx<=1; tdx++)
		for (tdy=-1; tdy<=1; tdy++)
			if (getUnit(x+tdx, y+tdy)==otherUID)
			{
				*dx=tdx;
				*dy=tdy;
				return true;
			}
			
	return false;
}

bool Map::doesPosTouchUID(int x, int y, Sint16 otherUID)
{
	int tdx, tdy;
	for (tdx=-1; tdx<=1; tdx++)
		for (tdy=-1; tdy<=1; tdy++)
			if (getUnit(x+tdx, y+tdy)==otherUID)
				return true;
	return false;
}

bool Map::doesPosTouchUID(int x, int y, Sint16 otherUID, int *dx, int *dy)
{
	int tdx, tdy;
	
	for (tdx=-1; tdx<=1; tdx++)
	{
		for (tdy=-1; tdy<=1; tdy++)
		{
			if (getUnit(x+tdx, y+tdy)==otherUID)
			{
				*dx=tdx;
				*dy=tdy;
				return true;
			}
		}
	}
			
	return false;
}

bool Map::doesUnitTouchRessource(Unit *unit, RessourceType ressourceType, int *dx, int *dy)
{
	int x=unit->posX;
	int y=unit->posY;
	int tdx, tdy;
	
	for (tdx=-1; tdx<=1; tdx++)
		for (tdy=-1; tdy<=1; tdy++)
			if (isRessource(x+tdx, y+tdy,ressourceType))
			{
				*dx=tdx;
				*dy=tdy;
				return true;
			}
			
	return false;
}

bool Map::doesPosTouchRessource(int x, int y, RessourceType ressourceType, int *dx, int *dy)
{
	int tdx, tdy;
	
	for (tdx=-1; tdx<=1; tdx++)
		for (tdy=-1; tdy<=1; tdy++)
			if (isRessource(x+tdx, y+tdy,ressourceType))
			{
				*dx=tdx;
				*dy=tdy;
				return true;
			}
			
	return false;
}

bool Map::doesUnitTouchEnemy(Unit *unit, int *dx, int *dy)
{
	int x=unit->posX;
	int y=unit->posY;
	int tdx, tdy;
	Uint32 enemies;

	enemies=unit->owner->enemies;
	for (tdx=-1; tdx<=1; tdx++)
	{
		for (tdy=-1; tdy<=1; tdy++)
		{
			Sint32 UID=getUnit(x+tdx, y+tdy);
			if (UID>=0)
			{
				int otherTeam=Unit::UIDtoTeam(UID);
				Uint32 otherTeamMask=1<<otherTeam;
				if (enemies&otherTeamMask)
				{
					*dx=tdx;
					*dy=tdy;
					return true;
				}
			}
			else if(UID!=NOUID)
			{
				int otherTeam=Building::UIDtoTeam(UID);
				int otherID=Building::UIDtoID(UID);
				Uint32 otherTeamMask=1<<otherTeam;
				Building *b=unit->owner->game->teams[otherTeam]->myBuildings[otherID];
				if ((!b->type->defaultUnitStayRange) && (enemies&otherTeamMask))
				{
					*dx=tdx;
					*dy=tdy;
					return true;
				}
			}
		}
	}
			
	return false;
}

void Map::setBaseMap(/*const*/ BaseMap *initial)
{
	memcpy(mapName, initial->getMapName(), MAP_NAME_MAX_SIZE);
}

void Map::setSize(int wDec, int hDec, Game *game, TerrainType terrainType)
{
	if (mapDiscovered)
		delete mapDiscovered;
	if (fogOfWarA)
		delete[] fogOfWarA;
	if (fogOfWarB)
		delete[] fogOfWarB;
	fogOfWar=NULL;
	stepCounter=0;
	sizeOfFogOfWar=0;
	if (cases)
		delete[] cases;
	if (sectors)
		delete[] sectors;
	if (undermap)
		delete[] undermap;

	this->wDec=wDec;
	this->hDec=hDec;
	w=1<<wDec;
	h=1<<hDec;
	wMask=w-1;
	hMask=h-1;
	int size=w*h;

	mapDiscovered=new Uint32[size];
	memset(mapDiscovered, 0, size*sizeof(Uint32));

	fogOfWarA=new Uint32[size];
	memset(fogOfWarA, 0, size*sizeof(Uint32));
	fogOfWarB=new Uint32[size];
	memset(fogOfWarB, 0, size*sizeof(Uint32));
	fogOfWar=fogOfWarA;
	stepCounter=0;
	sizeOfFogOfWar=size;

	undermap=new Uint8[size];
	memset(undermap, terrainType, size);

	cases=new Case[size];
	Case initCase;
	initCase.terrain=0;
	initCase.unit=NOUID;
	for (int i=0; i<size; i++)
		cases[i]=initCase;
	regenerateMap(0,0,w,h);

	// now sectors
	wSector=w>>4;
	hSector=h>>4;
	size=wSector*hSector;
	// pas standard!!!
#	ifndef WIN32
		sectors=new Sector[size](game);
#	else
		sectors=new Sector[size];
		{
			for (int i=0; i < size; ++i) 
			{
				sectors[i].~Sector();
				new (&sectors[i])Sector(game);
			}
		}
#	endif
}

bool Map::load(SDL_RWops *stream, Game *game)
{
	if (!BaseMap::load(stream))
		return false;

	char signature[4];
	SDL_RWread(stream, signature, 4, 1);
	if (memcmp(signature,"GLO2",4)!=0)
		return false;
	
	// then map
	// recompute size
	wDec=SDL_ReadBE32(stream);
	hDec=SDL_ReadBE32(stream);
	w=1<<wDec;
	h=1<<hDec;
	wMask=w-1;
	hMask=h-1;
	int size=w*h;

	// regenerate
	if (mapDiscovered)
		delete mapDiscovered;
	if (fogOfWarA)
		delete[] fogOfWarA;
	if (fogOfWarB)
		delete[] fogOfWarB;
	fogOfWar=NULL;
	sizeOfFogOfWar=0;
	if (cases)
		delete[] cases;
	if (undermap)
		delete[] undermap;
	mapDiscovered=new Uint32[size];
	fogOfWarA=new Uint32[size];
	fogOfWarB=new Uint32[size];
	fogOfWar=fogOfWarA;
	sizeOfFogOfWar=size;
	stepCounter=0;
	cases=new Case[size];
	{
		for (int i=0;i<size;++i)
		{
			mapDiscovered[i]=SDL_ReadBE32(stream);
			cases[i].setInteger(SDL_ReadBE32(stream));
		}
	}

	undermap=new Uint8[size];
	SDL_RWread(stream, undermap, size, 1);

	// Only if we load a game, (not a map preview, load all stuff)
	if (game)
	{
		memset(fogOfWarA, 0, sizeOfFogOfWar*sizeof(Uint32));
		memset(fogOfWarB, 0, sizeOfFogOfWar*sizeof(Uint32));

		// now sectors
		wSector=SDL_ReadBE32(stream);
		hSector=SDL_ReadBE32(stream);
		size=wSector*hSector;
		if (sectors)
			delete[] sectors;
		// non standard !!!
#		ifndef WIN32
			sectors=new Sector[size](game);
#		else
			sectors=new Sector[size];
			{
				for (int i = 0; i < size; ++i)
				{
					sectors[i].~Sector();
					new (&sectors[i])Sector(game);
				}
			}
#		endif

		{
			for (int i=0;i<size;++i)
			{
				// TODO : make a bool sector.load to allow errors.
				sectors[i].load(stream, game);
			}
		}

		SDL_RWread(stream, signature, 4, 1);
		if (memcmp(signature,"GLO2",4)!=0)
			return false;
	}
	return true;
}

void Map::save(SDL_RWops *stream)
{
	BaseMap::save(stream);
	
	SDL_RWwrite(stream, "GLO2", 4, 1);
	
	// then map
	SDL_WriteBE32(stream, wDec);
	SDL_WriteBE32(stream, hDec);
	int size=w*h;
	{
		for (int i=0;i<size;i++)
		{
			SDL_WriteBE32(stream, mapDiscovered[i]);
			SDL_WriteBE32(stream, cases[i].getInteger());
		}
	}
	SDL_RWwrite(stream, undermap, size, 1);

	SDL_WriteBE32(stream, wSector);
	SDL_WriteBE32(stream, hSector);
	size=wSector*hSector;
	{
		for (int i=0; i<size; ++i)
		{
			sectors[i].save(stream);
		}
	}
	
	SDL_RWwrite(stream, "GLO2", 4, 1);
}
void Map::growRessources(void)
{
	int dy=syncRand()&0x3;
	for (int y=dy; y<h; y+=4)
	{
		for (int x=(syncRand()&0xF); x<w; x+=(syncRand()&0x1F))
		//for (int x=0; x<w; x++)
		{
			//int y=syncRand()&hMask;

			int d=getTerrain(x, y)-272;
			int r=d/10;
			int l=d%5;
			if ((d>=0)&&(d<40))
			{
				// we look around to see if there is any water :
				// TODO: uses UnderMap.
				static const int waterDist=0xF;
				int dwax=(syncRand()&waterDist)-(syncRand()&waterDist);
				int dway=(syncRand()&waterDist)-(syncRand()&waterDist);
				int wax1=x+dwax;
				int way1=y+dway;
				
				int wax2=x+dway*2;
				int way2=y+dwax*2;
				
				int wax3=x-dwax;
				int way3=y-dway;

				//int wax4=x+dway*2;
				//int way4=y-dwax*2;

				bool expand=false;
				if (r==ALGA)
				{
					expand=isWater(wax1, way1)&&isSand(wax2, way2);
				}
				else if (r==WOOD)
				{
					expand=isWaterOrAlga(wax1, way1)&&(!isSand(wax3, way3));
				}
				else if (r==CORN)
				{
					expand=isWaterOrAlga(wax1, way1)&&(!isSand(wax3, way3));
				}
				
				if (expand)
				{
					//if (l<4)
					if (l<=(int)(syncRand()&3))
						setTerrain(x, y, d+273);
					else 
					{
						// we extand ressource:
						int dx, dy;
						Unit::dxdyfromDirection(syncRand()&7, &dx, &dy);
						int nx=x+dx;
						int ny=y+dy;
						if (getUnit(nx, ny)==NOUID)
						if (((r==WOOD||r==CORN)&&isGrass(nx, ny))||((r==ALGA)&&isWater(nx, ny)))
							setTerrain(nx, ny, 272+(r*10)+((syncRand()&1)*5));
					}
				}
			}
		}
	}
}

void Map::step(void)
{
	growRessources();
	for (int i=0; i<(wSector*hSector); i++)
		sectors[i].step();
	
	stepCounter++;
}

void Map::switchFogOfWar(void)
{
	if (fogOfWar==fogOfWarA)
	{
		fogOfWar=fogOfWarB;
		memset(fogOfWarA, 0, sizeOfFogOfWar*sizeof(Uint32));
	}
	else
	{
		fogOfWar=fogOfWarA;
		memset(fogOfWarB, 0, sizeOfFogOfWar*sizeof(Uint32));
	}
}

void Map::setBuilding(int x, int y, int w, int h, Sint16 u)
{
	for (int bx=x; bx<x+w; bx++)
		for (int by=y; by<y+h; by++)
			setUnit(bx, by, u);
}

void Map::setUMatPos(int x, int y, TerrainType t, int size)
{
	int dx, dy;
	for (dx=x-(size>>1);dx<x+(size>>1)+1;dx++)
	{
		for (dy=y-(size>>1);dy<y+(size>>1)+1;dy++)
		{
			if (t==GRASS)
			{
				if (getUMTerrain(dx,dy-1)==WATER)
					setUMTerrain(dx,dy-1,SAND);
				if (getUMTerrain(dx,dy+1)==WATER)
					setUMTerrain(dx,dy+1,SAND);

				if (getUMTerrain(dx-1,dy)==WATER)
					setUMTerrain(dx-1,dy,SAND);
				if (getUMTerrain(dx+1,dy)==WATER)
					setUMTerrain(dx+1,dy,SAND);

				if (getUMTerrain(dx-1,dy-1)==WATER)
					setUMTerrain(dx-1,dy-1,SAND);
				if (getUMTerrain(dx+1,dy-1)==WATER)
					setUMTerrain(dx+1,dy-1,SAND);

				if (getUMTerrain(dx+1,dy+1)==WATER)
					setUMTerrain(dx+1,dy+1,SAND);
				if (getUMTerrain(dx-1,dy+1)==WATER)
					setUMTerrain(dx-1,dy+1,SAND);
			}
			if (t==WATER)
			{
				if (getUMTerrain(dx,dy-1)==GRASS)
					setUMTerrain(dx,dy-1,SAND);
				if (getUMTerrain(dx,dy+1)==GRASS)
					setUMTerrain(dx,dy+1,SAND);

				if (getUMTerrain(dx-1,dy)==GRASS)
					setUMTerrain(dx-1,dy,SAND);
				if (getUMTerrain(dx+1,dy)==GRASS)
					setUMTerrain(dx+1,dy,SAND);

				if (getUMTerrain(dx-1,dy-1)==GRASS)
					setUMTerrain(dx-1,dy-1,SAND);
				if (getUMTerrain(dx+1,dy-1)==GRASS)
					setUMTerrain(dx+1,dy-1,SAND);

				if (getUMTerrain(dx+1,dy+1)==GRASS)
					setUMTerrain(dx+1,dy+1,SAND);
				if (getUMTerrain(dx-1,dy+1)==GRASS)
					setUMTerrain(dx-1,dy+1,SAND);
			}
			setUMTerrain(dx,dy,t);
		}
	}
	//regenerateMap(x-(size>>1)-3,y-(size>>1)-3,size+5,size+5);
	if (t==SAND)
		regenerateMap(x-(size>>1)-1,y-(size>>1)-1,size+1,size+1);
	else
		regenerateMap(x-(size>>1)-2,y-(size>>1)-2,size+3,size+3);
}

void Map::regenerateMap(int x, int y, int w, int h)
{
	int dx,dy;

	for (dx=x;dx<x+w;dx++)
	{
		for (dy=y;dy<y+h;dy++)
		{
			setTerrain(dx,dy,lookup(getUMTerrain(dx,dy),getUMTerrain(dx+1,dy),getUMTerrain(dx,dy+1),getUMTerrain(dx+1,dy+1)));
		}
	}
}

void Map::setResAtPos(int x, int y, int type, int size)
{
	int dx, dy;
	if (type<3)
	{
		for (dx=x-(size>>1);dx<x+(size>>1)+1;dx++)
		{
			for (dy=y-(size>>1);dy<y+(size>>1)+1;dy++)
			{
			if ( (getUMTerrain(dx,dy)==GRASS) &&
				(getUMTerrain(dx+1,dy)==GRASS) &&
				(getUMTerrain(dx,dy+1)==GRASS) &&
				(getUMTerrain(dx+1,dy+1)==GRASS) )
				setTerrain(dx,dy,272+type*10+rand()%10);
			}
		}
	}
	else
	{
		for (dx=x-(size>>1);dx<x+(size>>1)+1;dx++)
		{
			for (dy=y-(size>>1);dy<y+(size>>1)+1;dy++)
			{
				if ( (getUMTerrain(dx,dy)==WATER) &&
					(getUMTerrain(dx+1,dy)==WATER) &&
					(getUMTerrain(dx,dy+1)==WATER) &&
					(getUMTerrain(dx+1,dy+1)==WATER) )
				setTerrain(dx,dy,302+rand()%10);
			}
		}
	}
}

Uint16 Map::lookup(Uint8 tl, Uint8 tr, Uint8 bl, Uint8 br)
{
	const Uint16 terrainLookupTable[81][2] =
	{
		{ 0, 16 },		// H, H, H, H
		{ 80, 8 },		// H, H, H, S
		{ 0, 16 },		// H, H, H, E
		{ 88, 8 },		// H, H, S, H
		{ 48, 8 },		// H, H, S, S
		{ 0, 16 },		// H, H, S, E
		{ 0, 16 },		// H, H, E, H
		{ 0, 16 },		// H, H, E, S
		{ 0, 16 },		// H, H, E, E
		{ 104, 8 },		// H, S, H, H
		{ 64, 8 },		// H, S, H, S
		{ 0, 16 },		// H, S, H, E
		{ 120, 8 },		// H, S, S, H
		{ 32, 8 },		// H, S, S, S
		{ 0, 16 },		// H, S, S, E
		{ 0, 16 },		// H, S, E, H
		{ 0, 16 },		// H, S, E, S
		{ 0, 16 },		// H, S, E, E
		{ 0, 16 },		// H, E, H, H
		{ 0, 16 },		// H, E, H, S
		{ 0, 16 },		// H, E, H, E
		{ 0, 16 },		// H, E, S, H
		{ 0, 16 },		// H, E, S, S
		{ 0, 16 },		// H, E, S, E
		{ 0, 16 },		// H, E, E, H
		{ 0, 16 },		// H, E, E, S
		{ 0, 16 },		// H, E, E, E

		{ 96, 8 },		// S, H, H, H
		{ 112, 8 },		// S, H, H, S
		{ 0, 16 },		// S, H, H, E
		{ 72, 8 },		// S, H, S, H
		{ 40, 8 },		// S, H, S, S
		{ 0, 16 },		// S, H, S, E
		{ 0, 16 },		// S, H, E, H
		{ 0, 16 },		// S, H, E, S
		{ 0, 16 },		// S, H, E, E
		{ 56, 8 },		// S, S, H, H
		{ 24, 8 },		// S, S, H, S
		{ 0, 16 },		// S, S, H, E
		{ 16, 8 },		// S, S, S, H
		{ 128, 16 },	// S, S, S, S
		{ 208, 8 },		// S, S, S, E
		{ 0, 16 },		// S, S, E, H
		{ 216, 8 },		// S, S, E, S
		{ 176, 8 },		// S, S, E, E
		{ 0, 16 },		// S, E, H, H
		{ 0, 16 },		// S, E, H, S
		{ 0, 16 },		// S, E, H, E
		{ 0, 16 },		// S, E, S, H
		{ 232, 8 },		// S, E, S, S
		{ 192, 8 },		// S, E, S, E
		{ 0, 16 },		// S, E, E, H
		{ 240, 8 },		// S, E, E, S
		{ 160, 8 },		// S, E, E, E

		{ 0, 16 },		// E, H, H, H
		{ 0, 16 },		// E, H, H, S
		{ 0, 16 },		// E, H, H, E
		{ 0, 16 },		// E, H, S, H
		{ 0, 16 },		// E, H, S, S
		{ 0, 16 },		// E, H, S, E
		{ 0, 16 },		// E, H, E, H
		{ 0, 16 },		// E, H, E, S
		{ 0, 16 },		// E, H, E, E
		{ 0, 16 },		// E, S, H, H
		{ 0, 16 },		// E, S, H, S
		{ 0, 16 },		// E, S, H, E
		{ 0, 16 },		// E, S, S, H
		{ 224, 8 },		// E, S, S, S
		{ 248, 8 },		// E, S, S, E
		{ 0, 16 },		// E, S, E, H
		{ 200, 8 },		// E, S, E, S
		{ 168, 8 },		// E, S, E, E
		{ 0, 16 },		// E, E, H, H
		{ 0, 16 },		// E, E, H, S
		{ 0, 16 },		// E, E, H, E
		{ 0, 16 },		// E, E, S, H
		{ 184, 8 },		// E, E, S, S
		{ 152, 8 },		// E, E, S, E
		{ 0, 16 },		// E, E, E, H
		{ 144, 8 },		// E, E, E, S
		{ 256, 16 },	// E, E, E, E
	};

	tl=2-tl;
	tr=2-tr;
	bl=2-bl;
	br=2-br;
	int index=tl*27+tr*9+bl*3+br;

	return terrainLookupTable[index][0]+(rand()%terrainLookupTable[index][1]);
}

void Map::mapCaseToDisplayable(int mx, int my, int *px, int *py, int viewportX, int viewportY)
{
	int x=mx-viewportX;
	int y=my-viewportY;
	
	if (x>(getW()/2))
		x-=getW();
	if (y>(getH()/2))
		y-=getH();
	if ((x)<-(getW()/2))
		x+=getW();
	if ((y)<-(getH()/2))
		y+=getH();
	*px=x<<5;
	*py=y<<5;
}

void Map::displayToMapCaseAligned(int mx, int my, int *px, int *py, int viewportX, int viewportY)
{
	*px=((mx>>5)+viewportX+getW())&getMaskW();
	*py=((my>>5)+viewportY+getH())&getMaskH();
}

void Map::displayToMapCaseUnaligned(int mx, int my, int *px, int *py, int viewportX, int viewportY)
{
	*px=(((mx+16)>>5)+viewportX+getW())&getMaskW();
	*py=(((my+16)>>5)+viewportY+getH())&getMaskH();
}

void Map::cursorToBuildingPos(int mx, int my, int buildingWidth, int buildingHeight, int *px, int *py, int viewportX, int viewportY)
{
	// we check for room
	int tempX, tempY;
	if (buildingWidth&0x1)
		tempX=((mx)>>5)+viewportX;
	else
		tempX=((mx+16)>>5)+viewportX;
			
	if (buildingHeight&0x1)
		tempY=((my)>>5)+viewportY;
	else
		tempY=((my+16)>>5)+viewportY;
		
	*px=(tempX+getW())&getMaskW();
	*py=(tempY+getH())&getMaskH();
}

void Map::buildingPosToCursor(int px, int py, int buildingWidth, int buildingHeight, int *mx, int *my, int viewportX, int viewportY)
{
	mapCaseToDisplayable(px, py, mx, my, viewportX, viewportY);
	*mx+=buildingWidth*16;
	*my+=buildingHeight*16;
}

bool Map::nearestRessource(int x, int y, RessourceType ressourceType, int *dx, int *dy)
{
	{
		for (int i=1; i<32; i++)
		{
			for (int j=-i; j<i; j++)
			{
				if (isRessource(x+i, y+j, ressourceType))
				{
					*dx=(x+i)&getMaskW();
					*dy=(y+j)&getMaskH();
					return true;
				}
				if (isRessource(x-i, y+j, ressourceType))
				{
					*dx=(x-i)&getMaskW();
					*dy=(y+j)&getMaskH();
					return true;
				}
				if (isRessource(x+j, y+i, ressourceType))
				{
					*dx=(x+j)&getMaskW();
					*dy=(y+i)&getMaskH();
					return true;
				}
				if (isRessource(x+j, y-i, ressourceType))
				{
					*dx=(x+j)&getMaskW();
					*dy=(y-i)&getMaskH();
					return true;
				}
			}
		}
	}
    return false;
}

bool Map::nearestRessource(int x, int y, RessourceType *ressourceType, int *dx, int *dy)
{
	{
		for (int i=1; i<32; i++)
		{
			for (int j=-i; j<i; j++)
			{
				if (isRessource(x+i, y+j, ressourceType))
				{
					*dx=(x+i)&getMaskW();
					*dy=(y+j)&getMaskH();
					return true;
				}
				if (isRessource(x-i, y+j, ressourceType))
				{
					*dx=(x-i)&getMaskW();
					*dy=(y+j)&getMaskH();
					return true;
				}
				if (isRessource(x+j, y+i, ressourceType))
				{
					*dx=(x+j)&getMaskW();
					*dy=(y+i)&getMaskH();
					return true;
				}
				if (isRessource(x+j, y-i, ressourceType))
				{
					*dx=(x+j)&getMaskW();
					*dy=(y-i)&getMaskH();
					return true;
				}
			}
		}
	}
    return false;
}

Sint32 Map::checkSum()
{
	return BaseMap::checkSum();
}

int Map::warpDistSquare(int px, int py, int qx, int qy)
{
	int dx=abs(px-qx);
	int dy=abs(py-qy);
	dx&=wMask;
	dy&=hMask;
	if (dx>(w>>1))
		dx=w-dx;
	if (dy>(h>>1))
		dy=h-dy;
	
	return ((dx*dx)+(dy*dy));
}
/*
void Map::saveThumbnail(SDL_RWops *stream)
{
	bool isWater;
	bool isSand;
	bool isGrass;
	bool isWood;
	bool isCorn;
	bool isStone;
	bool isSeaweed;
	int dx, dy;
	float dMx, dMy;
	float minidx, minidy;
	Uint8 tempdata[128*128];

	dMx=(float)w/128.0f;
	dMy=(float)h/128.0f;
	for (dy=0; dy<128; dy++)
	{
		for (dx=0; dx<128;dx++)
		{
			isWater=isSand=isGrass=isWood=isCorn=isStone=isSeaweed=false;
			for (minidx=(dMx*dx); minidx<=(dMx*(dx+1)); minidx++)
			{
				for (minidy=(dMy*dy); minidy<=(dMy*(dy+1)); minidy++)
				{
					int mdx=(int)minidx;
					int mdy=(int)minidy;
					if (this->isWater(mdx, mdy))
						isWater=true;
					else if (this->isGrass(mdx, mdy))
						isGrass=true;
					else if (isRessource(mdx, mdy, WOOD))
						isWood=true;
					else if (isRessource(mdx, mdy, CORN))
						isCorn=true;
					else if (isRessource(mdx, mdy, STONE))
						isStone=true;
					else if (isRessource(mdx, mdy, ALGA))
						isSeaweed=true;
					else
						isSand=true;
				}
			}
			tempdata[dy*128+dx]=isWater+(isSand<<1)+(isGrass<<2)+(isWood<<3)+(isCorn<<4)+(isStone<<5)+(isSeaweed<<6);
		}
	}
	SDL_RWwrite(stream, tempdata, 128*128, 1);
}
*/
