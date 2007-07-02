/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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

#include <stdlib.h>

#include "Marshaling.h"
#include "Order.h"
#include "Utilities.h"
#include "Brush.h"

Order::Order(void)
{
	sender=-1;
	gameCheckSum=-1;
}

boost::shared_ptr<Order> Order::getOrder(const Uint8 *netData, int netDataLength)
{
	if (netDataLength<1)
		return boost::shared_ptr<Order>();
	if (netData==NULL)
		return boost::shared_ptr<Order>();
	
	switch (netData[0])
	{

	case ORDER_CREATE:
	{
		return boost::shared_ptr<Order>(new OrderCreate(netData+1, netDataLength-1));
	}
	case ORDER_DELETE:
	{
		return boost::shared_ptr<Order>(new OrderDelete(netData+1, netDataLength-1));
	}
	case ORDER_CANCEL_DELETE:
	{
		return boost::shared_ptr<Order>(new OrderCancelDelete(netData+1, netDataLength-1));
	}
	case ORDER_CONSTRUCTION:
	{
		return boost::shared_ptr<Order>(new OrderConstruction(netData+1, netDataLength-1));
	}
	case ORDER_CANCEL_CONSTRUCTION:
	{
		return boost::shared_ptr<Order>(new OrderCancelConstruction(netData+1, netDataLength-1));
	}
	case ORDER_MODIFY_BUILDING:
	{
		return boost::shared_ptr<Order>(new OrderModifyBuilding(netData+1, netDataLength-1));
	}
	case ORDER_MODIFY_EXCHANGE:
	{
		return boost::shared_ptr<Order>(new OrderModifyExchange(netData+1, netDataLength-1));
	}
	case ORDER_MODIFY_SWARM:
	{
		return boost::shared_ptr<Order>(new OrderModifySwarm(netData+1, netDataLength-1));
	}
	case ORDER_MODIFY_FLAG:
	{
		return boost::shared_ptr<Order>(new OrderModifyFlag(netData+1, netDataLength-1));
	}
	case ORDER_MODIFY_CLEARING_FLAG:
	{
		return boost::shared_ptr<Order>(new OrderModifyClearingFlag(netData+1, netDataLength-1));
	}
	case ORDER_MODIFY_MIN_LEVEL_TO_FLAG:
	{
		return boost::shared_ptr<Order>(new OrderModifyMinLevelToFlag(netData+1, netDataLength-1));
	}
	case ORDER_MOVE_FLAG:
	{
		return boost::shared_ptr<Order>(new OrderMoveFlag(netData+1, netDataLength-1));
	}
	case ORDER_ALTERATE_FORBIDDEN:
	{
		return boost::shared_ptr<Order>(new OrderAlterateForbidden(netData+1, netDataLength-1));
	}
	case ORDER_ALTERATE_GUARD_AREA:
	{
		return boost::shared_ptr<Order>(new OrderAlterateGuardArea(netData+1, netDataLength-1));
	}
	case ORDER_ALTERATE_CLEAR_AREA:
	{
		return boost::shared_ptr<Order>(new OrderAlterateClearArea(netData+1, netDataLength-1));
	}
	case ORDER_NULL:
	{
		return boost::shared_ptr<Order>(new NullOrder());
	}
	case ORDER_TEXT_MESSAGE:
	{
		return boost::shared_ptr<Order>(new MessageOrder(netData+1, netDataLength-1));
	}
	case ORDER_VOICE_DATA:
	{
		return boost::shared_ptr<Order>(new OrderVoiceData(netData+1, netDataLength-1));
	}
	case ORDER_SET_ALLIANCE:
	{
		return boost::shared_ptr<Order>(new SetAllianceOrder(netData+1, netDataLength-1));
	}
	case ORDER_MAP_MARK:
	{
		return boost::shared_ptr<Order>(new MapMarkOrder(netData+1, netDataLength-1));
	}
	case ORDER_PAUSE_GAME:
	{
		return boost::shared_ptr<Order>(new PauseGameOrder(netData+1, netDataLength-1));
	}
	case ORDER_PLAYER_QUIT_GAME :
	{
		return boost::shared_ptr<Order>(new PlayerQuitsGameOrder(netData+1, netDataLength-1));
	}
	default:
		printf("Bad packet recieved in Order.cpp (%d)\n", netData[0]);
		
	}
	return boost::shared_ptr<Order>();
}

// OrderCreate's code

OrderCreate::OrderCreate(const Uint8 *data, int dataLength)
:Order()
{
	assert(dataLength==24);//if changed don't forget order.h update
	bool good=setData(data, dataLength);
	assert(good);
}

OrderCreate::OrderCreate(Sint32 teamNumber, Sint32 posX, Sint32 posY, Sint32 typeNum, Sint32 unitWorking, Sint32 unitWorkingFuture)
{
	this->teamNumber=teamNumber;
	this->posX=posX;
	this->posY=posY;
	this->typeNum=typeNum;
	this->unitWorking=unitWorking;
	this->unitWorkingFuture=unitWorkingFuture;
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
	
	return data;
}

bool OrderCreate::setData(const Uint8 *data, int dataLength)
{
	if (dataLength!=getDataLength())
		return false;
	
	this->teamNumber=getSint32(data, 0);
	this->posX=getSint32(data, 4);
	this->posY=getSint32(data, 8);
	this->typeNum=getSint32(data, 12);
	this->unitWorking=getSint32(data, 16);
	this->unitWorkingFuture=getSint32(data, 20);
	
	memcpy(this->data, data, dataLength);
	
	return true;
}

// OrderDelete's code

OrderDelete::OrderDelete(const Uint8 *data, int dataLength)
:Order()
{
	assert(dataLength==2);
	bool good=setData(data, dataLength);
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

bool OrderDelete::setData(const Uint8 *data, int dataLength)
{
	if (dataLength!=getDataLength())
		return false;
	this->gid=getUint16(data, 0);
	memcpy(this->data, data, dataLength);
	return true;
}

// OrderCancelDelete's code

OrderCancelDelete::OrderCancelDelete(const Uint8 *data, int dataLength)
{
	assert(dataLength==2);
	bool good=setData(data, dataLength);
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

bool OrderCancelDelete::setData(const Uint8 *data, int dataLength)
{
	if(dataLength != getDataLength())
		return false;
	this->gid = getUint16(data, 0);
	memcpy(this->data, data, dataLength);
	return true;
}

// OrderConstruction's code

OrderConstruction::OrderConstruction(const Uint8 *data, int dataLength)
:Order()
{
	assert(dataLength==10);
	bool good=setData(data, dataLength);
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

bool OrderConstruction::setData(const Uint8 *data, int dataLength)
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

OrderCancelConstruction::OrderCancelConstruction(const Uint8 *data, int dataLength)
:Order()
{
	assert(dataLength==6);
	bool good=setData(data, dataLength);
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

bool OrderCancelConstruction::setData(const Uint8 *data, int dataLength)
{
	if (dataLength!=getDataLength())
		return false;
	this->gid=getUint16(data, 0);
	this->unitWorking=getUint32(data, 2);
	memcpy(this->data, data, dataLength);
	return true;
}

// OrderModify' code

OrderModify::OrderModify()
:Order()
{
}

// OrderModifyBuildings' code

OrderModifyBuilding::OrderModifyBuilding(const Uint8 *data, int dataLength)
:OrderModify()
{
	assert(dataLength==4);
	bool good=setData(data, dataLength);
	assert(good);
}

OrderModifyBuilding::OrderModifyBuilding(Uint16 gid, Uint16 numberRequested)
{
	assert(gid<32768);
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

bool OrderModifyBuilding::setData(const Uint8 *data, int dataLength)
{
	if (dataLength!=4)
		return false;
	gid=getUint16(data, 0);
	numberRequested=getUint16(data, 2);
	return true;
}

// OrderModifyExchange' code

OrderModifyExchange::OrderModifyExchange(const Uint8 *data, int dataLength)
:OrderModify()
{
	assert(dataLength==10);
	bool good=setData(data, dataLength);
	assert(good);
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

bool OrderModifyExchange::setData(const Uint8 *data, int dataLength)
{
	if (dataLength!=10)
		return false;
	gid=getUint16(data, 0);
	receiveRessourceMask=getUint32(data, 2);
	sendRessourceMask=getUint32(data, 6);
	return true;
}

// OrderModifySwarm's code

OrderModifySwarm::OrderModifySwarm(const Uint8 *data, int dataLength)
:OrderModify()
{
	assert(dataLength == getDataLength());
	bool good = setData(data, dataLength);
	assert(good);
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

bool OrderModifySwarm::setData(const Uint8 *data, int dataLength)
{
	if (dataLength != getDataLength())
		return false;
	gid = getUint16(data, 0);
	for (int i=0; i<NB_UNIT_TYPE; i++)
		ratio[i] = getSint32(data, 2+4*i);
	return true;
}

// OrderModifyFlag' code

OrderModifyFlag::OrderModifyFlag(const Uint8 *data, int dataLength)
:OrderModify()
{
	assert(dataLength==6);
	bool good=setData(data, dataLength);
	assert(good);
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

bool OrderModifyFlag::setData(const Uint8 *data, int dataLength)
{
	if (dataLength!=6)
		return false;
	gid=getUint16(data, 0);
	range=getSint32(data,2);
	return true;
}

// OrderModifyClearingFlags' code

OrderModifyClearingFlag::OrderModifyClearingFlag(const Uint8 *data, int dataLength)
:OrderModify()
{
	this->data=NULL;
	assert(dataLength==2+BASIC_COUNT);
	bool good=setData(data, dataLength);
	assert(good);
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

bool OrderModifyClearingFlag::setData(const Uint8 *data, int dataLength)
{
	if (dataLength!=getDataLength())
		return false;
	this->gid=getUint16(data, 0);
	for (int i=0; i<BASIC_COUNT; i++)
		clearingRessources[i]=(bool)getUint8(data, 2+i);
	
	return true;
}

// OrderModifyMinLevelToFlag's code

OrderModifyMinLevelToFlag::OrderModifyMinLevelToFlag(const Uint8 *data, int dataLength)
:OrderModify()
{
	assert(dataLength==4);
	bool good=setData(data, dataLength);
	assert(good);
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

bool OrderModifyMinLevelToFlag::setData(const Uint8 *data, int dataLength)
{
	if (dataLength!=getDataLength())
		return false;
	this->gid=getUint16(data, 0);
	this->minLevelToFlag=getUint16(data, 2);
	return true;
}

// OrderMoveFlags' code

OrderMoveFlag::OrderMoveFlag(const Uint8 *data, int dataLength)
:OrderModify()
{
	assert(dataLength==11);
	bool good=setData(data, dataLength);
	assert(good);
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

bool OrderMoveFlag::setData(const Uint8 *data, int dataLength)
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

OrderAlterateArea::OrderAlterateArea(const Uint8 *data, int dataLength)
{
	_data = NULL;
	
	bool good=setData(data, dataLength);
	assert(good);
}

OrderAlterateArea::OrderAlterateArea(Uint8 teamNumber, Uint8 type, BrushAccumulator *acc)
{
	assert(acc);
	_data = NULL;
	
	BrushAccumulator::AreaDimensions dim;
	acc->getBitmap(&mask, &dim);
	this->teamNumber = teamNumber;
	this->type = type;
	centerX = dim.centerX;
	centerY = dim.centerY;
	minX = dim.minX;
	minY = dim.minY;
	maxX = dim.maxX;
	maxY = dim.maxY;
	assert(maxX-minX <= 512);
	assert(maxY-minY <= 512);
}

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

bool OrderAlterateArea::setData(const Uint8 *data, int dataLength)
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
	assert(maxX-minX <= 512);
	assert(maxY-minY <= 512);
	mask.deserialize(data+14, (maxX-minX)*(maxY-minY));
	
	return true;
}

int OrderAlterateArea::getDataLength(void)
{
	int length=14+mask.getByteLength();
	assert(length>=14);
	return length;
}

// MiscOrder's code

MiscOrder::MiscOrder()
:Order()
{
}

// NullOrder's code

NullOrder::NullOrder()
:MiscOrder()
{
}

// MessageOrder's code

MessageOrder::MessageOrder(const Uint8 *data, int dataLength)
:MiscOrder()
{
	this->data=NULL;
	assert(dataLength>=9);
	bool good=setData(data, dataLength);
	assert(good);
}

MessageOrder::MessageOrder(Uint32 recepientsMask, Uint32 messageOrderType, const char *text)
{
	length=Utilities::strmlen(text, 256)+9;
	data=(Uint8 *)malloc(length);
	memcpy(data+9, text, length-9);
	data[length-1]=0;
	addUint32(data, recepientsMask, 0);
	addUint32(data, messageOrderType, 4);
	addUint8(data, (Uint8)(length-9), 8);
	this->recepientsMask=recepientsMask;
	this->messageOrderType=messageOrderType;
}

MessageOrder::~MessageOrder()
{
	assert(data);
	free(data);
}

Uint8 *MessageOrder::getData(void)
{
	return data;
}

bool MessageOrder::setData(const Uint8 *data, int dataLength)
{
	if (dataLength<9)
		return false;
	this->length=dataLength;
	this->recepientsMask=getUint32(data, 0);
	this->messageOrderType=getUint32(data, 4);
	Uint8 textLength=getUint8(data, 8);
	if (this->data!=NULL)
		free(this->data);
	this->data=(Uint8 *)malloc(dataLength);
	memcpy(this->data, data, dataLength);
	if (this->data[dataLength-1]!=0)
		return false;
	if (textLength!=Utilities::strmlen((const char *)(this->data+9), 256))
		return false;
	if (textLength!=dataLength-9)
		return false;
	return true;
}

// OrderVoiceData's code

OrderVoiceData::OrderVoiceData(const Uint8 *data, int dataLength)
:MiscOrder()
{
	this->data = NULL;
	assert(dataLength >= 5);
	bool good = setData(data, dataLength);
	assert(good);
}

OrderVoiceData::OrderVoiceData(Uint32 recepientsMask, size_t framesDatasLength, Uint8 frameCount, const Uint8 *framesDatas)
{
	this->recepientsMask = recepientsMask;
	this->framesDatasLength = framesDatasLength;
	this->frameCount = frameCount;
	
	data = (Uint8 *)malloc(framesDatasLength+5);
	if (framesDatas)
		memcpy(data+5, framesDatas, framesDatasLength);
}

OrderVoiceData::~OrderVoiceData()
{
	assert(data);
	free(data);
}

Uint8 *OrderVoiceData::getData(void)
{
	addUint32(data, recepientsMask, 0);
	addUint8(data, frameCount, 4);
	return data;
}

bool OrderVoiceData::setData(const Uint8 *data, int dataLength)
{
	assert(dataLength >= 5);
	if (dataLength<5)
		return false;
		
	this->framesDatasLength = (size_t)dataLength - 5;
	this->recepientsMask = getUint32(data, 0);
	this->frameCount = getUint8(data, 4);
	
	if (this->data != NULL)
		free(this->data);
	this->data = (Uint8 *)malloc(dataLength);
	memcpy(this->data, data, dataLength);
	return true;
}

// SetAllianceOrder's code

SetAllianceOrder::SetAllianceOrder(const Uint8 *data, int dataLength)
:MiscOrder()
{
	assert(dataLength==24);
	bool good=setData(data, dataLength);
	assert(good);
}

SetAllianceOrder::SetAllianceOrder(Uint32 teamNumber, Uint32 alliedMask, Uint32 enemyMask, Uint32 visionExchangeMask, Uint32 visionFoodMask, Uint32 visionOtherMask)
{
	this->teamNumber=teamNumber;
	this->alliedMask=alliedMask;
	this->enemyMask=enemyMask;
	this->visionExchangeMask=visionExchangeMask;
	this->visionFoodMask=visionFoodMask;
	this->visionOtherMask=visionOtherMask;
}

Uint8 *SetAllianceOrder::getData(void)
{
	assert(sizeof(data) == getDataLength());
	addUint32(data, this->teamNumber, 0);
	addUint32(data, this->alliedMask, 4);
	addUint32(data, this->enemyMask, 8);
	addUint32(data, this->visionExchangeMask, 12);
	addUint32(data, this->visionFoodMask, 16);
	addUint32(data, this->visionOtherMask, 20);
	return data;
}

bool SetAllianceOrder::setData(const Uint8 *data, int dataLength)
{
	if(dataLength!=getDataLength())
		return false;
	this->teamNumber=getUint32(data, 0);
	this->alliedMask=getUint32(data, 4);
	this->enemyMask=getUint32(data, 8);
	this->visionExchangeMask=getUint32(data, 12);
	this->visionFoodMask=getUint32(data, 16);
	this->visionOtherMask=getUint32(data, 20);
	memcpy(this->data, data, dataLength);
	return true;
}

// MapMarkOrder's code

MapMarkOrder::MapMarkOrder(const Uint8 *data, int dataLength)
:MiscOrder()
{
	assert(dataLength==12);
	bool good=setData(data, dataLength);
	assert(good);
}

MapMarkOrder::MapMarkOrder(Uint32 teamNumber, Sint32 x, Sint32 y)
{
	this->teamNumber=teamNumber;
	this->x=x;
	this->y=y;
}

Uint8 *MapMarkOrder::getData(void)
{
	assert(sizeof(data) == getDataLength());
	addUint32(data, this->teamNumber, 0);
	addSint32(data, this->x, 4);
	addSint32(data, this->y, 8);
	return data;
}

bool MapMarkOrder::setData(const Uint8 *data, int dataLength)
{
	if(dataLength!=getDataLength())
		return false;

	this->teamNumber=getUint32(data, 0);
	this->x=getSint32(data, 4);
	this->y=getSint32(data, 8);

	memcpy(this->data, data, dataLength);

	return true;
}

// PauseGameOrder's code

PauseGameOrder::PauseGameOrder(const Uint8 *data, int dataLength)
:MiscOrder()
{
	assert(dataLength==1);
	bool good=setData(data, dataLength);
	assert(good);
}

PauseGameOrder::PauseGameOrder(bool pause)
{
	this->pause=pause;
}

Uint8 *PauseGameOrder::getData(void)
{
	assert(sizeof(data) == getDataLength());
	data[0]=(Uint8)pause;
	return data;
}

bool PauseGameOrder::setData(const Uint8 *data, int dataLength)
{
	if(dataLength!=getDataLength())
		return false;
	pause=(bool)data[0];
	memcpy(this->data, data, dataLength);
	return true;
}

// PlayerQuitsGameOrder code

PlayerQuitsGameOrder::PlayerQuitsGameOrder(const Uint8 *data, int dataLength)
:MiscOrder()
{
	assert(dataLength==4);
	bool good=setData(data, dataLength);
	assert(good);
}

PlayerQuitsGameOrder::PlayerQuitsGameOrder(Sint32 player)
{
	this->player=player;
}

Uint8 *PlayerQuitsGameOrder::getData(void)
{
	assert(sizeof(data) == getDataLength());
	addUint32(data, this->player, 0);
	return data;
}

bool PlayerQuitsGameOrder::setData(const Uint8 *data, int dataLength)
{
	if(dataLength!=getDataLength())
		return false;

	this->player=getUint32(data, 0);
	
	memcpy(this->data, data, dataLength);
	
	return true;
}
