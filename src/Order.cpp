/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

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
	wishedLatency=0;
	wishedDelay=0;
	latencyPadding=false;
	ustep=0;
	gameCheckSum=0;
}

Order *Order::getOrder(const Uint8 *netData, int netDataLength)
{
	if (netDataLength<1)
		return NULL;
	if (netData==NULL)
		return NULL;
	
	switch (netData[0])
	{

	case ORDER_CREATE:
	{
		return new OrderCreate(netData+1, netDataLength-1);
	}
	case ORDER_DELETE:
	{
		return new OrderDelete(netData+1, netDataLength-1);
	}
	case ORDER_CANCEL_DELETE:
	{
		return new OrderCancelDelete(netData+1, netDataLength-1);
	}
	case ORDER_CONSTRUCTION:
	{
		return new OrderConstruction(netData+1, netDataLength-1);
	}
	case ORDER_CANCEL_CONSTRUCTION:
	{
		return new OrderCancelConstruction(netData+1, netDataLength-1);
	}
	case ORDER_MODIFY_BUILDING:
	{
		return new OrderModifyBuilding(netData+1, netDataLength-1);
	}
	case ORDER_MODIFY_EXCHANGE:
	{
		return new OrderModifyExchange(netData+1, netDataLength-1);
	}
	case ORDER_MODIFY_SWARM:
	{
		return new OrderModifySwarm(netData+1, netDataLength-1);
	}
	case ORDER_MODIFY_FLAG:
	{
		return new OrderModifyFlag(netData+1, netDataLength-1);
	}
	case ORDER_MODIFY_CLEARING_FLAG:
	{
		return new OrderModifyClearingFlag(netData+1, netDataLength-1);
	}
	case ORDER_MODIFY_WAR_FLAG:
	{
		return new OrderModifyWarFlag(netData+1, netDataLength-1);
	}
	case ORDER_MOVE_FLAG:
	{
		return new OrderMoveFlag(netData+1, netDataLength-1);
	}
	case ORDER_ALTERATE_FORBIDDEN:
	{
		return new OrderAlterateForbidden(netData+1, netDataLength-1);
	}
	case ORDER_ALTERATE_GUARD_AREA:
	{
		return new OrderAlterateGuardArea(netData+1, netDataLength-1);
	}
	case ORDER_QUITED:
	{
		assert(false); // Currently, QuitedOrder has to be used only between NetGame and GameGUI, but not the network.
		return new QuitedOrder();
	}
	case ORDER_DECONNECTED:
	{
		assert(false); // Currently, DeconnectedOrder has to be used only between NetGame and GameGUI, but not the network.
		return new DeconnectedOrder();
	}
	case ORDER_NULL:
	{
		return new NullOrder();
	}
	case ORDER_TEXT_MESSAGE:
	{
		return new MessageOrder(netData+1, netDataLength-1);
	}
	case ORDER_SET_ALLIANCE:
	{
		return new SetAllianceOrder(netData+1, netDataLength-1);
	}
	case ORDER_MAP_MARK:
	{
		return new MapMarkOrder(netData+1, netDataLength-1);
	}
	case ORDER_WAITING_FOR_PLAYER:
	{
		return new WaitingForPlayerOrder(netData+1, netDataLength-1);
	}
	case ORDER_PAUSE_GAME:
	{
		return new PauseGameOrder(netData+1, netDataLength-1);
	}
	case ORDER_DROPPING_PLAYER:
	{
		return new DroppingPlayerOrder(netData+1, netDataLength-1);
	}
	case ORDER_REQUESTING_AWAY:
	{
		return new RequestingDeadAwayOrder(netData+1, netDataLength-1);
	}
	case ORDER_NO_MORE_ORDER_AVAILABLES:
	{
		return new NoMoreOrdersAvailable(netData+1, netDataLength-1);
	}
	case ORDER_PLAYER_QUIT_GAME :
	{
		return new PlayerQuitsGameOrder(netData+1, netDataLength-1);
	}
	default:
		printf("Bad packet recieved in Order.cpp (%d)\n", netData[0]);
		
	}
	return NULL;
}

// OrderCreate's code

OrderCreate::OrderCreate(const Uint8 *data, int dataLength)
:Order()
{
	assert(dataLength==16);
	bool good=setData(data, dataLength);
	assert(good);
}

OrderCreate::OrderCreate(Sint32 teamNumber, Sint32 posX, Sint32 posY, Sint32 typeNum)
{
	this->teamNumber=teamNumber;
	this->posX=posX;
	this->posY=posY;
	this->typeNum=typeNum;
}

Uint8 *OrderCreate::getData(void)
{
	addSint32(data, this->teamNumber, 0);
	addSint32(data, this->posX, 4);
	addSint32(data, this->posY, 8);
	addSint32(data, this->typeNum, 12);
	
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
	addUint16(data, this->gid, 0);
	return data;
}

bool OrderCancelDelete::setData(const Uint8 *data, int dataLength)
{
	if(dataLength!=getDataLength())
		return false;
	this->gid=getUint16(data, 0);
	memcpy(this->data, data, dataLength);
	return true;
}

// OrderConstruction's code

OrderConstruction::OrderConstruction(const Uint8 *data, int dataLength)
:Order()
{
	assert(dataLength==2);
	bool good=setData(data, dataLength);
	assert(good);
}

OrderConstruction::OrderConstruction(Uint16 gid)
{
	assert(gid<32768);
	this->gid=gid;
}

Uint8 *OrderConstruction::getData(void)
{
	addUint16(data, this->gid, 0);
	return data;
}

bool OrderConstruction::setData(const Uint8 *data, int dataLength)
{
	if (dataLength!=getDataLength())
		return false;
	this->gid=getUint16(data, 0);
	memcpy(this->data, data, dataLength);
	return true;
}

// OrderCancelConstruction's code

OrderCancelConstruction::OrderCancelConstruction(const Uint8 *data, int dataLength)
:Order()
{
	assert(dataLength==2);
	bool good=setData(data, dataLength);
	assert(good);
}

OrderCancelConstruction::OrderCancelConstruction(Uint16 gid)
{
	assert(gid<32768);
	this->gid=gid;
}

Uint8 *OrderCancelConstruction::getData(void)
{
	addUint16(data, this->gid, 0);
	return data;
}

bool OrderCancelConstruction::setData(const Uint8 *data, int dataLength)
{
	if (dataLength!=getDataLength())
		return false;
	this->gid=getUint16(data, 0);
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
	assert(dataLength==getDataLength());
	bool good=setData(data, dataLength);
	assert(good);
}

OrderModifySwarm::OrderModifySwarm(Uint16 gid, Sint32 ratio[NB_UNIT_TYPE])
{
	this->gid=gid;
	memcpy(this->ratio, ratio, 4*NB_UNIT_TYPE);
}

Uint8 *OrderModifySwarm::getData(void)
{
	addUint16(data, gid, 0);
	for (int i=0; i<NB_UNIT_TYPE; i++)
		addSint32(data, ratio[i], 2+4*i);
	return data;
}

bool OrderModifySwarm::setData(const Uint8 *data, int dataLength)
{
	if (dataLength!=getDataLength())
		return false;
	gid=getUint16(data, 0);
	for (int i=0; i<NB_UNIT_TYPE; i++)
		ratio[i]=getSint32(data, 2+4*i);
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

// OrderModifyWarFlag' code

OrderModifyWarFlag::OrderModifyWarFlag(const Uint8 *data, int dataLength)
:OrderModify()
{
	assert(dataLength==4);
	bool good=setData(data, dataLength);
	assert(good);
}

OrderModifyWarFlag::OrderModifyWarFlag(Uint16 gid, Uint16 minLevelToFlag)
{
	this->gid=gid;
	this->minLevelToFlag=minLevelToFlag;
}

OrderModifyWarFlag::~OrderModifyWarFlag(void)
{
}

Uint8 *OrderModifyWarFlag::getData(void)
{
	addUint16(data, gid, 0);
	addUint16(data, minLevelToFlag, 2);
	return data;
}

bool OrderModifyWarFlag::setData(const Uint8 *data, int dataLength)
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
	x = dim.minX;
	y = dim.minY;
	w = dim.maxX-dim.minX;
	h = dim.maxY-dim.minY;
	assert(w<=512);
	assert(h<=512);
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
	addSint16(_data, x, 2);
	addSint16(_data, y, 4);
	addUint16(_data, w, 6);
	addUint16(_data, h, 8);
	mask.serialize(_data+10);
	
	return _data;
}

bool OrderAlterateArea::setData(const Uint8 *data, int dataLength)
{
	if (dataLength < 10)
	{
		printf("OrderAlterateForbidden::setData(dataLength=%d) failure\n", dataLength);
		for (int i=0; i<dataLength; i++)
			printf("data[%d]=%d\n", i, data[i]);
		return false;
	}
	
	teamNumber = getUint8(data, 0);
	type = getUint8(data, 1);
	x = getSint16(data, 2);
	y = getSint16(data, 4);
	w = getUint16(data, 6);
	h = getUint16(data, 8);
	assert(w<=512);
	assert(h<=512);
	mask.deserialize(data+10, w*h);
	
	return true;
}

int OrderAlterateArea::getDataLength(void)
{
	int length=10+mask.getByteLength();
	assert(length>=10);
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

// QuitedOrder's code

QuitedOrder::QuitedOrder()
:MiscOrder()
{
}

// QuitedOrder's code

DeconnectedOrder::DeconnectedOrder()
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

// WaitingForPlayerOrder's code

WaitingForPlayerOrder::WaitingForPlayerOrder(const Uint8 *data, int dataLength)
:MiscOrder()
{
	assert(dataLength==4);
	bool good=setData(data, dataLength);
	assert(good);
}

WaitingForPlayerOrder::WaitingForPlayerOrder(Uint32 maskAwayPlayer)
{
	this->maskAwayPlayer=maskAwayPlayer;
}

Uint8 *WaitingForPlayerOrder::getData(void)
{
	addUint32(data, this->maskAwayPlayer, 0);
	return data;
}

bool WaitingForPlayerOrder::setData(const Uint8 *data, int dataLength)
{
	if(dataLength!=getDataLength())
		return false;
	this->maskAwayPlayer=getUint32(data, 0);
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

// DroppingPlayerOrder's code

DroppingPlayerOrder::DroppingPlayerOrder(const Uint8 *data, int dataLength)
:MiscOrder()
{
	assert(dataLength==4);
	bool good=setData(data, dataLength);
	assert(good);
}

DroppingPlayerOrder::DroppingPlayerOrder(Uint32 dropingPlayersMask)
{
	this->dropingPlayersMask=dropingPlayersMask;
}

Uint8 *DroppingPlayerOrder::getData(void)
{
	addUint32(data, this->dropingPlayersMask, 0);
	return data;
}

bool DroppingPlayerOrder::setData(const Uint8 *data, int dataLength)
{
	if(dataLength!=getDataLength())
		return false;
	this->dropingPlayersMask=getUint32(data, 0);
	memcpy(this->data, data, dataLength);
	return true;
}


// RequestingDeadAwayOrder's code

RequestingDeadAwayOrder::RequestingDeadAwayOrder(const Uint8 *data, int dataLength)
:MiscOrder()
{
	assert(dataLength==12);
	bool good=setData(data, dataLength);
	assert(good);
}

RequestingDeadAwayOrder::RequestingDeadAwayOrder(Sint32 player, Sint32 missingStep, Sint32 lastAvailableStep)
{
	this->player=player;
	this->missingStep=missingStep;
	this->lastAvailableStep=lastAvailableStep;
}

Uint8 *RequestingDeadAwayOrder::getData(void)
{
	addUint32(data, this->player, 0);
	addUint32(data, this->missingStep, 4);
	addUint32(data, this->lastAvailableStep, 8);
	return data;
}

bool RequestingDeadAwayOrder::setData(const Uint8 *data, int dataLength)
{
	if(dataLength!=getDataLength())
		return false;

	this->player=getUint32(data, 0);
	this->missingStep=getUint32(data, 4);
	this->lastAvailableStep=getUint32(data, 8);
	
	memcpy(this->data, data, dataLength);
	
	return true;
}

// NoMoreOrdersAvailable code

NoMoreOrdersAvailable::NoMoreOrdersAvailable(const Uint8 *data, int dataLength)
:MiscOrder()
{
	assert(dataLength==8);
	bool good=setData(data, dataLength);
	assert(good);
}

NoMoreOrdersAvailable::NoMoreOrdersAvailable(Sint32 player, Sint32 lastAvailableStep)
{
	this->player=player;
	this->lastAvailableStep=lastAvailableStep;
}

Uint8 *NoMoreOrdersAvailable::getData(void)
{
	addUint32(data, this->player, 0);
	addUint32(data, this->lastAvailableStep, 4);
	return data;
}

bool NoMoreOrdersAvailable::setData(const Uint8 *data, int dataLength)
{
	if(dataLength!=getDataLength())
		return false;

	this->player=getUint32(data, 0);
	this->lastAvailableStep=getUint32(data, 4);
	
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
