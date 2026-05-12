// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <stdlib.h>

#include "FileFormatVersions.h"
#include "Game.h"
#include "Marshaling.h"
#include "Order.h"

// OrderCreate's code

OrderCreate::OrderCreate()
:Order()
{
	memset(data, 0, sizeof(data));
}

std::optional<std::shared_ptr<OrderCreate>> OrderCreate::deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	// Wire encoding: Sint32 teamNumber || Sint32 posX || Sint32 posY || Sint32
	// typeNum || Sint32 unitWorking || Sint32 unitWorkingFuture || [Sint32
	// flagRadius (versionMinor >= 78)].
	std::shared_ptr<OrderCreate> order;
	if(versionMinor<FILE_FORMAT_VERSION_ORDER_CREATE_FLAG_RADIUS && dataLength!=20)
		return std::nullopt;
	else if (versionMinor>=FILE_FORMAT_VERSION_ORDER_CREATE_FLAG_RADIUS && dataLength!=28)
		return std::nullopt;

	order = std::make_shared<OrderCreate>();
	order->teamNumber=getSint32(data, 0);
	order->posX=getSint32(data, 4);
	order->posY=getSint32(data, 8);
	order->typeNum=getSint32(data, 12);
	order->unitWorking=getSint32(data, 16);
	order->unitWorkingFuture=getSint32(data, 20);
	if(versionMinor>=78)
		order->flagRadius=getSint32(data, 24);

	memcpy(order->data, data, dataLength);
	return order;
}

OrderCreate::OrderCreate(Sint32 teamNumber, Sint32 posX, Sint32 posY, Sint32 typeNum, Sint32 unitWorking, Sint32 unitWorkingFuture, Sint32 flagRadius)
{
	this->teamNumber=teamNumber;
	this->posX=posX;
	this->posY=posY;
	this->typeNum=typeNum;
	this->unitWorking=unitWorking;
	this->unitWorkingFuture=unitWorkingFuture;
	this->flagRadius=flagRadius;
}

Uint8 *OrderCreate::getData(void)
{
	assert(sizeof(data) == getDataLength());

	addSint32(data, this->teamNumber, 0);
	addSint32(data, this->posX, 4);
	addSint32(data, this->posY, 8);
	addSint32(data, this->typeNum, 12);
	addSint32(data, this->unitWorking, 16);
	addSint32(data, this->unitWorkingFuture, 20);
	addSint32(data, this->flagRadius, 24);

	return data;
}

bool OrderCreate::setData(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	if(versionMinor<FILE_FORMAT_VERSION_ORDER_CREATE_FLAG_RADIUS && dataLength!=20)
		return false;
	else if (versionMinor>=FILE_FORMAT_VERSION_ORDER_CREATE_FLAG_RADIUS && dataLength!=getDataLength())
		return false;

	this->teamNumber=getSint32(data, 0);
	this->posX=getSint32(data, 4);
	this->posY=getSint32(data, 8);
	this->typeNum=getSint32(data, 12);
	this->unitWorking=getSint32(data, 16);
	this->unitWorkingFuture=getSint32(data, 20);
	if(versionMinor>=78)
		this->flagRadius=getSint32(data, 24);

	memcpy(this->data, data, dataLength);

	return true;
}

// OrderDelete's code

OrderDelete::OrderDelete()
:Order()
{
	memset(this->data, 0, sizeof(this->data));
}

std::optional<std::shared_ptr<OrderDelete>> OrderDelete::deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	// Wire encoding: Uint16 gid.
	if (dataLength!=2)
		return std::nullopt;

	std::shared_ptr<OrderDelete> order = std::make_shared<OrderDelete>();
	order->gid=getUint16(data, 0);
	memcpy(order->data, data, dataLength);
	return order;
}

OrderDelete::OrderDelete(Uint16 gid)
{
	assert(gid<BUILDING_GID_MAX);
	this->gid=gid;
}

Uint8 *OrderDelete::getData(void)
{
	assert(sizeof(data) == getDataLength());
	addUint16(data, this->gid, 0);
	return data;
}

bool OrderDelete::setData(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	if (dataLength!=getDataLength())
		return false;
	this->gid=getUint16(data, 0);
	memcpy(this->data, data, dataLength);
	return true;
}

// OrderCancelDelete's code

OrderCancelDelete::OrderCancelDelete()
:Order()
{
	memset(this->data, 0, sizeof(this->data));
}

std::optional<std::shared_ptr<OrderCancelDelete>> OrderCancelDelete::deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	// Wire encoding: Uint16 gid.
	if (dataLength!=2)
		return std::nullopt;

	std::shared_ptr<OrderCancelDelete> order = std::make_shared<OrderCancelDelete>();
	order->gid=getUint16(data, 0);
	memcpy(order->data, data, dataLength);
	return order;
}

OrderCancelDelete::OrderCancelDelete(Uint16 gid)
{
	assert(gid<BUILDING_GID_MAX);
	this->gid=gid;
}

Uint8 *OrderCancelDelete::getData(void)
{
	assert(sizeof(data) == getDataLength());
	addUint16(data, this->gid, 0);
	return data;
}

bool OrderCancelDelete::setData(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	if(dataLength != getDataLength())
		return false;
	this->gid = getUint16(data, 0);
	memcpy(this->data, data, dataLength);
	return true;
}

// OrderConstruction's code

OrderConstruction::OrderConstruction()
:Order()
{
	memset(data, 0, sizeof(data));
}

std::optional<std::shared_ptr<OrderConstruction>> OrderConstruction::deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	// Wire encoding: Uint16 gid || Uint32 unitWorking || Uint32 unitWorkingFuture.
	if (dataLength!=10)
		return std::nullopt;

	std::shared_ptr<OrderConstruction> order = std::make_shared<OrderConstruction>();
	order->gid=getUint16(data, 0);
	order->unitWorking=getUint32(data, 2);
	order->unitWorkingFuture=getUint32(data, 6);
	memcpy(order->data, data, dataLength);
	return order;
}

OrderConstruction::OrderConstruction(Uint16 gid, Uint32 unitWorking, Uint32 unitWorkingFuture)
{
	assert(gid<BUILDING_GID_MAX);
	this->gid=gid;
	this->unitWorking=unitWorking;
	this->unitWorkingFuture=unitWorkingFuture;
}

Uint8 *OrderConstruction::getData(void)
{
	assert(sizeof(data) == getDataLength());
	addUint16(data, this->gid, 0);
	addUint32(data, this->unitWorking, 2);
	addUint32(data, this->unitWorkingFuture, 6);
	return data;
}

bool OrderConstruction::setData(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	if (dataLength!=getDataLength())
		return false;
	this->gid=getUint16(data, 0);
	this->unitWorking=getUint32(data, 2);
	this->unitWorkingFuture=getUint32(data, 6);
	memcpy(this->data, data, dataLength);
	return true;
}

// OrderCancelConstruction's code

OrderCancelConstruction::OrderCancelConstruction()
:Order()
{
	memset(data, 0, sizeof(data));
}

std::optional<std::shared_ptr<OrderCancelConstruction>> OrderCancelConstruction::deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	// Wire encoding: Uint16 gid || Uint32 unitWorking.
	if (dataLength!=6)
		return std::nullopt;

	std::shared_ptr<OrderCancelConstruction> order = std::make_shared<OrderCancelConstruction>();
	order->gid=getUint16(data, 0);
	order->unitWorking=getUint32(data, 2);
	memcpy(order->data, data, dataLength);
	return order;
}

OrderCancelConstruction::OrderCancelConstruction(Uint16 gid, Uint32 unitWorking)
{
	assert(gid<BUILDING_GID_MAX);
	this->gid=gid;
	this->unitWorking=unitWorking;
}

Uint8 *OrderCancelConstruction::getData(void)
{
	assert(sizeof(data) == getDataLength());
	addUint16(data, this->gid, 0);
	addUint32(data, this->unitWorking, 2);
	return data;
}

bool OrderCancelConstruction::setData(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	if (dataLength!=getDataLength())
		return false;
	this->gid=getUint16(data, 0);
	this->unitWorking=getUint32(data, 2);
	memcpy(this->data, data, dataLength);
	return true;
}

// OrderChangePriority's code

OrderChangePriority::OrderChangePriority()
:Order()
{
	memset(data, 0, sizeof(data));
}

std::optional<std::shared_ptr<OrderChangePriority>> OrderChangePriority::deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	// Wire encoding: Uint16 gid || Sint32 priority.
	if (dataLength!=6)
		return std::nullopt;

	std::shared_ptr<OrderChangePriority> order = std::make_shared<OrderChangePriority>();
	order->gid=getUint16(data, 0);
	order->priority=getUint32(data, 2);
	memcpy(order->data, data, dataLength);
	return order;
}

OrderChangePriority::OrderChangePriority(Uint16 gid, Sint32 priority)
{
	assert(gid<BUILDING_GID_MAX);
	this->gid=gid;
	this->priority=priority;
}

Uint8 *OrderChangePriority::getData(void)
{
	assert(sizeof(data) == getDataLength());
	addUint16(data, this->gid, 0);
	addSint32(data, this->priority, 2);
	return data;
}

bool OrderChangePriority::setData(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	if (dataLength!=getDataLength())
		return false;
	this->gid=getUint16(data, 0);
	this->priority=getUint32(data, 2);
	memcpy(this->data, data, dataLength);
	return true;
}
