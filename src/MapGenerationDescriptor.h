/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charrière
    for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

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

#ifndef __MAP_GENERATION_DESCRIPTOR_H
#define __MAP_GENERATION_DESCRIPTOR_H

#include "Order.h"
#include "Map.h"

class MapGenerationDescriptor:public Order
{
public:
	MapGenerationDescriptor();
	virtual ~MapGenerationDescriptor(void);
	
	Uint8 getOrderType() {return ORDER_MAP_GENERATION_DEFINITION; }
	char *getData();
	bool setData(const char *data, int dataLength);
	int getDataLength() {return DATA_SIZE; }
	
	void save(SDL_RWops *stream);
	bool load(SDL_RWops *stream);
	Sint32 checkSum();
	void saveSyncronization(void);
	void loadSyncronization(void);
	void synchronizeNow(void);

public:
	Map::TerrainType terrainType;
	enum Methode
	{
		eUNIFORM,
		eRANDOM,
		eISLANDS
	};
	Methode methode;
	
	Sint32 wDec, hDec;
	
	Sint32 waterRatio, sandRatio, grassRatio;
	Sint32 smooth;
	Sint32 islandsSize, beach;
	Sint32 ressource[NB_RESSOURCES];

	Sint32 nbTeams, nbWorkers;
	
	Uint32 randa, randb, randc;
public:
	// Thoses may not be in data
	Sint32 bootX[32];
	Sint32 bootY[32];
public:
	enum {DATA_SIZE=76};
protected:
	//! Serialized form of MapGenerationDescriptor
	char data[DATA_SIZE];
};


#endif 
 
