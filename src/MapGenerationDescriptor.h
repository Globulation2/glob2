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
	
	Sint32 checkSum();

public:
	Map::TerrainType terrainType;
	enum Methode
	{
		eUNIFORM=0,
		eRANDOM=1
	};
	Methode methode;
	
	int waterRatio, sandRatio, grassRatio;
	int smooth;
protected:
	//! Serialized form of MapGenerationDescriptor
	enum {DATA_SIZE=1};
	char data[DATA_SIZE];
};


#endif 
 
