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

OrderModifyBuilding::OrderModifyBuilding(Uint16 gid, Uint16 numberRequested)
{
	assert(gid<BUILDING_GID_MAX);
	this->gid=gid;
	this->numberRequested=numberRequested;
}

std::shared_ptr<OrderModifyBuilding> OrderModifyBuilding::deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	auto order = std::make_shared<OrderModifyBuilding>();
	if (!order->setData(data, dataLength, versionMinor))
		return nullptr;
	return order;
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
	if (dataLength!=getDataLength())
		return false;
	gid=getUint16(data, 0);
	numberRequested=getUint16(data, 2);
	return true;
}

// OrderModifyExchange' code

OrderModifyExchange::OrderModifyExchange(Uint16 gid, Uint32 receiveRessourceMask, Uint32 sendRessourceMask)
{
	this->gid=gid;
	this->receiveRessourceMask=receiveRessourceMask;
	this->sendRessourceMask=sendRessourceMask;
}

std::shared_ptr<OrderModifyExchange> OrderModifyExchange::deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	auto order = std::make_shared<OrderModifyExchange>();
	if (!order->setData(data, dataLength, versionMinor))
		return nullptr;
	return order;
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
	if (dataLength!=getDataLength())
		return false;
	gid=getUint16(data, 0);
	receiveRessourceMask=getUint32(data, 2);
	sendRessourceMask=getUint32(data, 6);
	return true;
}

// OrderModifySwarm's code

OrderModifySwarm::OrderModifySwarm(Uint16 gid, Sint32 ratio[NB_UNIT_TYPE])
{
	this->gid = gid;
	memcpy(this->ratio, ratio, 4*NB_UNIT_TYPE);
}

std::shared_ptr<OrderModifySwarm> OrderModifySwarm::deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	auto order = std::make_shared<OrderModifySwarm>();
	if (!order->setData(data, dataLength, versionMinor))
		return nullptr;
	return order;
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

OrderModifyFlag::OrderModifyFlag(Uint16 gid, Sint32 range)
{
	this->gid=gid;
	this->range=range;
}

std::shared_ptr<OrderModifyFlag> OrderModifyFlag::deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	auto order = std::make_shared<OrderModifyFlag>();
	if (!order->setData(data, dataLength, versionMinor))
		return nullptr;
	return order;
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
	if (dataLength!=getDataLength())
		return false;
	gid=getUint16(data, 0);
	range=getSint32(data,2);
	return true;
}

// OrderModifyClearingFlags' code

OrderModifyClearingFlag::OrderModifyClearingFlag(Uint16 gid, bool clearingRessources[BASIC_COUNT])
{
	this->gid=gid;
	memcpy(this->clearingRessources, clearingRessources, sizeof(bool)*BASIC_COUNT);
}

std::shared_ptr<OrderModifyClearingFlag> OrderModifyClearingFlag::deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	auto order = std::make_shared<OrderModifyClearingFlag>();
	if (!order->setData(data, dataLength, versionMinor))
		return nullptr;
	return order;
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

OrderModifyMinLevelToFlag::OrderModifyMinLevelToFlag(Uint16 gid, Uint16 minLevelToFlag)
{
	this->gid=gid;
	this->minLevelToFlag=minLevelToFlag;
}

std::shared_ptr<OrderModifyMinLevelToFlag> OrderModifyMinLevelToFlag::deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	auto order = std::make_shared<OrderModifyMinLevelToFlag>();
	if (!order->setData(data, dataLength, versionMinor))
		return nullptr;
	return order;
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

OrderMoveFlag::OrderMoveFlag(Uint16 gid, Sint32 x, Sint32 y, bool drop)
{
	this->gid=gid;
	this->x=x;
	this->y=y;
	this->drop=drop;
}

std::shared_ptr<OrderMoveFlag> OrderMoveFlag::deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	auto order = std::make_shared<OrderMoveFlag>();
	if (!order->setData(data, dataLength, versionMinor))
		return nullptr;
	return order;
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
	if (dataLength!=getDataLength())
		return false;
	gid=getUint16(data, 0);
	x=getSint32(data, 2);
	y=getSint32(data, 6);
	drop=(bool)getUint8(data, 10);
	return true;
}

// OrderAlterateArea's code

#ifndef YOG_SERVER_ONLY
OrderAlterateArea::OrderAlterateArea(Uint8 teamNumber, Uint8 type, BrushAccumulator *acc, const Map* map)
{
	assert(acc);

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
	mask.serialize(_data+ALTERATE_AREA_HEADER_BYTES);

	return _data;
}

std::optional<size_t> OrderAlterateArea::expectedBitmapBytes(Sint16 minX, Sint16 minY,
                                                              Sint16 maxX, Sint16 maxY)
{
	// Promote to int so the subtraction can't overflow Sint16. The brush-side
	// cap below keeps the eventual size_t multiplication safely under 2^20.
	const int sideX = static_cast<int>(maxX) - static_cast<int>(minX);
	const int sideY = static_cast<int>(maxY) - static_cast<int>(minY);
	if (sideX < 0 || sideY < 0)
		return std::nullopt;
	if (sideX > ORDER_AREA_BRUSH_MAX_SIDE || sideY > ORDER_AREA_BRUSH_MAX_SIDE)
		return std::nullopt;
	const size_t bits = static_cast<size_t>(sideX) * static_cast<size_t>(sideY);
	return (bits + 7) / 8;
}

bool OrderAlterateArea::setData(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	if (dataLength < ALTERATE_AREA_HEADER_BYTES)
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

	// BH-195: reject malformed packets before BitArray::deserialize would read
	// past the end of `data`. The header-declared dimensions are wire bytes
	// and cannot be trusted; release builds previously stripped the asserts
	// that were the only guard here.
	const auto expectedPayload = expectedBitmapBytes(minX, minY, maxX, maxY);
	if (!expectedPayload)
		return false;
	if (static_cast<size_t>(dataLength) != ALTERATE_AREA_HEADER_BYTES + *expectedPayload)
		return false;

	mask.deserialize(data + ALTERATE_AREA_HEADER_BYTES,
		static_cast<size_t>(maxX - minX) * static_cast<size_t>(maxY - minY));

	return true;
}

int OrderAlterateArea::getDataLength(void)
{
	int length=ALTERATE_AREA_HEADER_BYTES+mask.getByteLength();
	assert(length>=ALTERATE_AREA_HEADER_BYTES);
	return length;
}

// OrderAlterate* concrete subclass factories.

std::shared_ptr<OrderAlterateForbidden> OrderAlterateForbidden::deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	auto order = std::make_shared<OrderAlterateForbidden>();
	if (!order->setData(data, dataLength, versionMinor))
		return nullptr;
	return order;
}

std::shared_ptr<OrderAlterateGuardArea> OrderAlterateGuardArea::deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	auto order = std::make_shared<OrderAlterateGuardArea>();
	if (!order->setData(data, dataLength, versionMinor))
		return nullptr;
	return order;
}

std::shared_ptr<OrderAlterateClearArea> OrderAlterateClearArea::deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	auto order = std::make_shared<OrderAlterateClearArea>();
	if (!order->setData(data, dataLength, versionMinor))
		return nullptr;
	return order;
}
