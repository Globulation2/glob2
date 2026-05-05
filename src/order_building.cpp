// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <stdlib.h>

#include "Marshaling.h"
#include "Order.h"

// OrderCreate's code

OrderCreate::OrderCreate(const Uint8 *data, int dataLength, Uint32 versionMinor)
:Order()
{
	assert(dataLength==28);//if changed don't forget order.h update
	bool good=setData(data, dataLength, versionMinor);
	assert(good);
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
	if(versionMinor<=77 && dataLength!=20)
		return false;
	else if (versionMinor>=78 && dataLength!=getDataLength())
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

OrderDelete::OrderDelete(const Uint8 *data, int dataLength, Uint32 versionMinor)
:Order()
{
	assert(dataLength==2);
	bool good=setData(data, dataLength, versionMinor);
	assert(good);
}

OrderDelete::OrderDelete(Uint16 gid)
{
	assert(gid<32768);
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

OrderCancelDelete::OrderCancelDelete(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	assert(dataLength==2);
	bool good=setData(data, dataLength, versionMinor);
	assert(good);
}

OrderCancelDelete::OrderCancelDelete(Uint16 gid)
{
	assert(gid<32768);
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

OrderConstruction::OrderConstruction(const Uint8 *data, int dataLength, Uint32 versionMinor)
:Order()
{
	assert(dataLength==10);
	bool good=setData(data, dataLength, versionMinor);
	assert(good);
}

OrderConstruction::OrderConstruction(Uint16 gid, Uint32 unitWorking, Uint32 unitWorkingFuture)
{
	assert(gid<32768);
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

OrderCancelConstruction::OrderCancelConstruction(const Uint8 *data, int dataLength, Uint32 versionMinor)
:Order()
{
	assert(dataLength==6);
	bool good=setData(data, dataLength, versionMinor);
	assert(good);
}

OrderCancelConstruction::OrderCancelConstruction(Uint16 gid, Uint32 unitWorking)
{
	assert(gid<32768);
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

OrderChangePriority::OrderChangePriority(const Uint8 *data, int dataLength, Uint32 versionMinor)
:Order()
{
	assert(dataLength==6);
	bool good=setData(data, dataLength, versionMinor);
	assert(good);
}

OrderChangePriority::OrderChangePriority(Uint16 gid, Sint32 priority)
{
	assert(gid<32768);
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
