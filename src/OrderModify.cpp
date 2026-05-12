// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <stdlib.h>

#include "Game.h"
#include "Marshaling.h"
#include "Order.h"
#include "Brush.h"

// OrderModify' code

OrderModify::OrderModify()
:Order()
{
}

// OrderModifyBuildings' code

// Deserialize wire format: Uint16 gid || Uint16 numberRequested (4 bytes total).
// On malformed input (wrong length), fields remain zero-initialized — caller should
// drop invalid orders. The Rust port should use a Result-returning factory instead.
OrderModifyBuilding::OrderModifyBuilding(const Uint8 *data, int dataLength, Uint32 versionMinor)
:OrderModify()
{
	gid = 0;
	numberRequested = 0;
	if (!setData(data, dataLength, versionMinor))
	{
		// Malformed packet — fields stay zeroed.
	}
}

OrderModifyBuilding::OrderModifyBuilding(Uint16 gid, Uint16 numberRequested)
{
	assert(gid<BUILDING_GID_MAX);
	this->gid=gid;
	this->numberRequested=numberRequested;
}

Uint8 *OrderModifyBuilding::getData(void)
{
	assert(sizeof(data) == getDataLength());
	addUint16(data, gid, 0);
	addUint16(data, numberRequested, 2);
	return data;
}

bool OrderModifyBuilding::setData(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	if (dataLength!=4)
		return false;
	gid=getUint16(data, 0);
	numberRequested=getUint16(data, 2);
	return true;
}

// OrderModifyExchange' code

// Deserialize wire format: Uint16 gid || Uint32 receiveRessourceMask || Uint32 sendRessourceMask (10 bytes).
// On malformed input, fields remain zero-initialized.
OrderModifyExchange::OrderModifyExchange(const Uint8 *data, int dataLength, Uint32 versionMinor)
:OrderModify()
{
	gid = 0;
	receiveRessourceMask = 0;
	sendRessourceMask = 0;
	if (!setData(data, dataLength, versionMinor))
	{
		// Malformed packet — fields stay zeroed.
	}
}

OrderModifyExchange::OrderModifyExchange(Uint16 gid, Uint32 receiveRessourceMask, Uint32 sendRessourceMask)
{
	this->gid=gid;
	this->receiveRessourceMask=receiveRessourceMask;
	this->sendRessourceMask=sendRessourceMask;
}

Uint8 *OrderModifyExchange::getData(void)
{
	assert(sizeof(data) == getDataLength());
	addUint16(data, gid, 0);
	addUint32(data, receiveRessourceMask, 2);
	addUint32(data, sendRessourceMask, 6);
	return data;
}

bool OrderModifyExchange::setData(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	if (dataLength!=10)
		return false;
	gid=getUint16(data, 0);
	receiveRessourceMask=getUint32(data, 2);
	sendRessourceMask=getUint32(data, 6);
	return true;
}

// OrderModifySwarm's code

// Deserialize wire format: Uint16 gid || Sint32 ratio[NB_UNIT_TYPE].
// On malformed input, fields remain zero-initialized.
OrderModifySwarm::OrderModifySwarm(const Uint8 *data, int dataLength, Uint32 versionMinor)
:OrderModify()
{
	gid = 0;
	for (int i = 0; i < NB_UNIT_TYPE; i++)
		ratio[i] = 0;
	if (!setData(data, dataLength, versionMinor))
	{
		// Malformed packet — fields stay zeroed.
	}
}

OrderModifySwarm::OrderModifySwarm(Uint16 gid, Sint32 ratio[NB_UNIT_TYPE])
{
	this->gid = gid;
	memcpy(this->ratio, ratio, 4*NB_UNIT_TYPE);
}

Uint8 *OrderModifySwarm::getData(void)
{
	assert(sizeof(data) == getDataLength());
	addUint16(data, gid, 0);
	for (int i=0; i<NB_UNIT_TYPE; i++)
		addSint32(data, ratio[i], 2+4*i);
	return data;
}

bool OrderModifySwarm::setData(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	if (dataLength != getDataLength())
		return false;
	gid = getUint16(data, 0);
	for (int i=0; i<NB_UNIT_TYPE; i++)
		ratio[i] = getSint32(data, 2+4*i);
	return true;
}

// OrderModifyFlag' code

// Deserialize wire format: Uint16 gid || Sint32 range (6 bytes).
// On malformed input, fields remain zero-initialized.
OrderModifyFlag::OrderModifyFlag(const Uint8 *data, int dataLength, Uint32 versionMinor)
:OrderModify()
{
	gid = 0;
	range = 0;
	if (!setData(data, dataLength, versionMinor))
	{
		// Malformed packet — fields stay zeroed.
	}
}

OrderModifyFlag::OrderModifyFlag(Uint16 gid, Sint32 range)
{
	this->gid=gid;
	this->range=range;
}

Uint8 *OrderModifyFlag::getData(void)
{
	assert(sizeof(data) == getDataLength());
	addUint16(data, gid, 0);
	addSint32(data, range, 2);
	return data;
}

bool OrderModifyFlag::setData(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	if (dataLength!=6)
		return false;
	gid=getUint16(data, 0);
	range=getSint32(data,2);
	return true;
}

// OrderModifyClearingFlags' code

// Deserialize wire format: Uint16 gid || Uint8 clearingRessources[BASIC_COUNT].
// On malformed input, fields remain zero-initialized.
OrderModifyClearingFlag::OrderModifyClearingFlag(const Uint8 *data, int dataLength, Uint32 versionMinor)
:OrderModify()
{
	this->data = NULL;
	gid = 0;
	for (int i = 0; i < BASIC_COUNT; i++)
		clearingRessources[i] = false;
	if (!setData(data, dataLength, versionMinor))
	{
		// Malformed packet — fields stay zeroed.
	}
}

OrderModifyClearingFlag::OrderModifyClearingFlag(Uint16 gid, bool clearingRessources[BASIC_COUNT])
{
	this->data=NULL;
	this->gid=gid;
	memcpy(this->clearingRessources, clearingRessources, sizeof(bool)*BASIC_COUNT);
}

OrderModifyClearingFlag::~OrderModifyClearingFlag(void)
{
	if (data)
		free(data);
}

Uint8 *OrderModifyClearingFlag::getData(void)
{
	if (data==NULL)
		data=(Uint8 *)malloc(2+BASIC_COUNT);
	addUint16(data, gid, 0);
	for (int i=0; i<BASIC_COUNT; i++)
		addUint8(data, (Uint8)clearingRessources[i], 2+i);
	return data;
}

bool OrderModifyClearingFlag::setData(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	if (dataLength!=getDataLength())
		return false;
	this->gid=getUint16(data, 0);
	for (int i=0; i<BASIC_COUNT; i++)
		clearingRessources[i]=(bool)getUint8(data, 2+i);

	return true;
}

// OrderModifyMinLevelToFlag's code

// Deserialize wire format: Uint16 gid || Uint16 minLevelToFlag (4 bytes).
// On malformed input, fields remain zero-initialized.
OrderModifyMinLevelToFlag::OrderModifyMinLevelToFlag(const Uint8 *data, int dataLength, Uint32 versionMinor)
:OrderModify()
{
	gid = 0;
	minLevelToFlag = 0;
	if (!setData(data, dataLength, versionMinor))
	{
		// Malformed packet — fields stay zeroed.
	}
}

OrderModifyMinLevelToFlag::OrderModifyMinLevelToFlag(Uint16 gid, Uint16 minLevelToFlag)
{
	this->gid=gid;
	this->minLevelToFlag=minLevelToFlag;
}

OrderModifyMinLevelToFlag::~OrderModifyMinLevelToFlag(void)
{
}

Uint8 *OrderModifyMinLevelToFlag::getData(void)
{
	assert(sizeof(data) == getDataLength());
	addUint16(data, gid, 0);
	addUint16(data, minLevelToFlag, 2);
	return data;
}

bool OrderModifyMinLevelToFlag::setData(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	if (dataLength!=getDataLength())
		return false;
	this->gid=getUint16(data, 0);
	this->minLevelToFlag=getUint16(data, 2);
	return true;
}

// OrderMoveFlags' code

// Deserialize wire format: Uint16 gid || Sint32 x || Sint32 y || Uint8 drop (11 bytes).
// On malformed input, fields remain zero-initialized.
OrderMoveFlag::OrderMoveFlag(const Uint8 *data, int dataLength, Uint32 versionMinor)
:OrderModify()
{
	gid = 0;
	x = 0;
	y = 0;
	drop = false;
	if (!setData(data, dataLength, versionMinor))
	{
		// Malformed packet — fields stay zeroed.
	}
}

OrderMoveFlag::OrderMoveFlag(Uint16 gid, Sint32 x, Sint32 y, bool drop)
{
	this->gid=gid;
	this->x=x;
	this->y=y;
	this->drop=drop;
}

Uint8 *OrderMoveFlag::getData(void)
{
	assert(sizeof(data) == getDataLength());
	addUint16(data, gid, 0);
	addSint32(data, x, 2);
	addSint32(data, y, 6);
	addUint8(data, (Uint8)drop, 10);
	return data;
}

bool OrderMoveFlag::setData(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	if (dataLength!=11)
		return false;
	gid=getUint16(data, 0);
	x=getSint32(data, 2);
	y=getSint32(data, 6);
	drop=(bool)getUint8(data, 10);
	return true;
}

// OrderAlterateArea's code

// Deserialize wire format: Uint8 teamNumber || Uint8 type || Sint16 centerX || Sint16 centerY ||
// Sint16 minX || Sint16 minY || Uint16 maxX || Uint16 maxY || BitArray mask.
// On malformed input, fields remain zero-initialized.
OrderAlterateArea::OrderAlterateArea(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	_data = NULL;
	teamNumber = 0;
	type = 0;
	centerX = 0;
	centerY = 0;
	minX = 0;
	minY = 0;
	maxX = 0;
	maxY = 0;
	if (!setData(data, dataLength, versionMinor))
	{
		// Malformed packet — fields stay zeroed.
	}
}

#ifndef YOG_SERVER_ONLY
OrderAlterateArea::OrderAlterateArea(Uint8 teamNumber, Uint8 type, BrushAccumulator *acc, const Map* map)
{
	assert(acc);
	_data = NULL;

	BrushAccumulator::AreaDimensions dim;
	acc->getBitmap(&mask, &dim, map);
	this->teamNumber = teamNumber;
	this->type = type;
	centerX = dim.centerX;
	centerY = dim.centerY;
	minX = dim.minX;
	minY = dim.minY;
	maxX = dim.maxX;
	maxY = dim.maxY;
	assert(maxX-minX <= ORDER_AREA_BRUSH_MAX_SIDE);
	assert(maxY-minY <= ORDER_AREA_BRUSH_MAX_SIDE);
}
#endif

OrderAlterateArea::~OrderAlterateArea(void)
{
	if (_data)
		free(_data);
}

Uint8 *OrderAlterateArea::getData(void)
{
	if (_data)
		free (_data);
	this->_data = (Uint8 *)malloc(getDataLength());

	addUint8(_data, teamNumber, 0);
	addUint8(_data, type, 1);
	addSint16(_data, centerX, 2);
	addSint16(_data, centerY, 4);
	addSint16(_data, minX, 6);
	addSint16(_data, minY, 8);
	addUint16(_data, maxX, 10);
	addUint16(_data, maxY, 12);
	mask.serialize(_data+14);

	return _data;
}

bool OrderAlterateArea::setData(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	if (dataLength < 14)
	{
		printf("OrderAlterateArea::setData(dataLength=%d) failure\n", dataLength);
		for (int i=0; i<dataLength; i++)
			printf("data[%d]=%d\n", i, data[i]);
		return false;
	}

	teamNumber = getUint8(data, 0);
	type = getUint8(data, 1);
	centerX = getSint16(data, 2);
	centerY = getSint16(data, 4);
	minX = getSint16(data, 6);
	minY = getSint16(data, 8);
	maxX = getUint16(data, 10);
	maxY = getUint16(data, 12);
	assert(maxX-minX <= ORDER_AREA_BRUSH_MAX_SIDE);
	assert(maxY-minY <= ORDER_AREA_BRUSH_MAX_SIDE);
	mask.deserialize(data+14, (maxX-minX)*(maxY-minY));

	return true;
}

int OrderAlterateArea::getDataLength(void)
{
	int length=14+mask.getByteLength();
	assert(length>=14);
	return length;
}
