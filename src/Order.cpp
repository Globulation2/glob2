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

#include "Order.h"
#include "Marshaling.h"

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
	case ORDER_MODIFY_UNIT:
	{
		assert(false);
		return new OrderModifyUnits(netData+1, netDataLength-1);
	}
	case ORDER_MODIFY_BUILDING:
	{
		return new OrderModifyBuildings(netData+1, netDataLength-1);
	}
	case ORDER_MODIFY_EXCHANGE:
	{
		return new OrderModifyExchange(netData+1, netDataLength-1);
	}
	case ORDER_MODIFY_SWARM:
	{
		return new OrderModifySwarms(netData+1, netDataLength-1);
	}
	case ORDER_MODIFY_FLAG:
	{
		return new OrderModifyFlags(netData+1, netDataLength-1);
	}
	case ORDER_MOVE_FLAG:
	{
		return new OrderMoveFlags(netData+1, netDataLength-1);
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
	case ORDER_NO_MORE_ORDER_AVIABLES:
	{
		return new NoMoreOrdersAviable(netData+1, netDataLength-1);
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

OrderCreate::OrderCreate(Uint32 team, Sint32 posX, Sint32 posY, Sint32 typeNumber)
{
	this->team=team;
	this->posX=posX;
	this->posY=posY;
	this->typeNumber=typeNumber;
}

Uint8 *OrderCreate::getData(void)
{
	addSint32(data, this->team, 0);
	addSint32(data, this->posX, 4);
	addSint32(data, this->posY, 8);
	addSint32(data, (Sint32)this->typeNumber, 12);
	
	return data;
}

bool OrderCreate::setData(const Uint8 *data, int dataLength)
{
	if(dataLength!=getDataLength())
		return false;
	
	this->team=getSint32(data, 0);
	this->posX=getSint32(data, 4);
	this->posY=getSint32(data, 8);
	this->typeNumber=getSint32(data, 12);
	
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
	if(dataLength!=getDataLength())
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
	if(dataLength!=getDataLength())
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
	if(dataLength!=getDataLength())
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

// OrderModifyUnits' code

OrderModifyUnits::OrderModifyUnits(const Uint8 *data, int dataLength)
:OrderModify()
{
	assert((dataLength%10)==0);
	bool good=setData(data, dataLength);
	assert(good);
}

OrderModifyUnits::OrderModifyUnits(Uint16 *gid, Sint32 *trigHP, Sint32 *trigHungry, int length)
{
	this->length=length;

	this->gid=(Uint16 *)malloc(length*2);
	this->trigHP=(Sint32 *)malloc(length*4);
	this->trigHungry=(Sint32 *)malloc(length*4);

	this->data=(Uint8 *)malloc(10*length);

	memcpy(this->gid, gid, length);
	memcpy(this->trigHP, trigHP, length);
	memcpy(this->trigHungry, trigHungry, length);
}

OrderModifyUnits::~OrderModifyUnits()
{
	free(data);
	free(gid);
	free(trigHP);
	free(trigHungry);
}

Uint8 *OrderModifyUnits::getData(void)
{
	for (int i=0; i<length; i++)
	{
		addUint16(data, (this->gid)[i], 10*i+0);
		addSint32(data, (this->trigHP)[i], 10*i+2);
		addSint32(data, (this->trigHungry)[i], 10*i+6);
	}
	return data;
}

bool OrderModifyUnits::setData(const Uint8 *data, int dataLength)
{
	if ((dataLength%10)!=0)
		return false;
	
	this->length=dataLength/10;

	this->gid=(Uint16 *)malloc(length*2);
	this->trigHP=(Sint32 *)malloc(length*4);
	this->trigHungry=(Sint32 *)malloc(length*4);

	this->data=(Uint8 *)malloc(12*length);

	for (int i=0; i<length; i++)
	{
		(this->gid)[i]=getUint16(data, 12*i+0);
		(this->trigHP)[i]=getSint32(data, 12*i+2);
		(this->trigHungry)[i]=getSint32(data, 12*i+6);
	}

	memcpy(this->data, data, dataLength);
	return true;
}


// OrderModifyBuildings' code

OrderModifyBuildings::OrderModifyBuildings(const Uint8 *data, int dataLength)
:OrderModify()
{
	assert((dataLength%6)==0);

	this->length=dataLength/6;

	this->gid=(Uint16 *)malloc(length*2);
	this->numberRequested=(Sint32 *)malloc(length*4);
	this->data=(Uint8 *)malloc(6*length);
	
	bool good=setData(data, dataLength);
	assert(good);
}

OrderModifyBuildings::OrderModifyBuildings(Uint16 *gid, Sint32 *numberRequested, int length)
{
	this->length=length;

	this->gid=(Uint16 *)malloc(length*2);
	this->numberRequested=(Sint32 *)malloc(length*4);
	this->data=(Uint8 *)malloc(6*length);

	memcpy(this->gid,gid,length*2);
	memcpy(this->numberRequested,numberRequested,length*4);
	memset(data, 0, length*6);
}

OrderModifyBuildings::~OrderModifyBuildings()
{
	free(data);
	free(gid);
	free(numberRequested);
}

Uint8 *OrderModifyBuildings::getData(void)
{
	for (int i=0; i<length; i++)
	{
		addUint16(data, (this->gid)[i], 6*i+0);
		addSint32(data, (this->numberRequested)[i], 6*i+2);
	}
	return data;
}

bool OrderModifyBuildings::setData(const Uint8 *data, int dataLength)
{
	if ((dataLength%6)!=0)
		return false;
	
	this->length=dataLength/6;

	free(this->gid);
	free(this->numberRequested);
	free(this->data);

	this->gid=(Uint16 *)malloc(length*2);
	this->numberRequested=(Sint32 *)malloc(length*4);
	this->data=(Uint8 *)malloc(6*length);

	for (int i=0; i<length; i++)
	{
		(this->gid)[i]=getUint16(data, 6*i+0);
		(this->numberRequested)[i]=getSint32(data, 6*i+2);
	}
	
	memcpy(this->data, data, dataLength);
	return true;
}

// OrderModifyExchange' code

OrderModifyExchange::OrderModifyExchange(const Uint8 *data, int dataLength)
:OrderModify()
{
	assert((dataLength%10)==0);

	this->length=dataLength/10;

	this->gid=(Uint16 *)malloc(length*2);
	this->receiveRessourceMask=(Uint32 *)malloc(length*4);
	this->sendRessourceMask=(Uint32 *)malloc(length*4);
	this->data=(Uint8 *)malloc(10*length);

	bool good=setData(data, dataLength);
	assert(good);
}

OrderModifyExchange::OrderModifyExchange(Uint16 *gid, Uint32 *receiveRessourceMask, Uint32 *sendRessourceMask, int length)
{
	this->length=length;

	this->gid=(Uint16 *)malloc(length*2);
	this->receiveRessourceMask=(Uint32 *)malloc(length*4);
	this->sendRessourceMask=(Uint32 *)malloc(length*4);
	this->data=(Uint8 *)malloc(10*length);

	memcpy(this->gid, gid,length*2);
	memcpy(this->receiveRessourceMask, receiveRessourceMask, length*4);
	memcpy(this->sendRessourceMask, sendRessourceMask, length*4);
	memset(data, 0, length*10);
}

OrderModifyExchange::~OrderModifyExchange()
{
	free(data);
	free(gid);
	free(receiveRessourceMask);
	free(sendRessourceMask);
}

Uint8 *OrderModifyExchange::getData(void)
{
	for (int i=0; i<length; i++)
	{
		addUint16(data, (this->gid)[i], 10*i+0);
		addUint32(data, (this->receiveRessourceMask)[i], 10*i+2);
		addUint32(data, (this->sendRessourceMask)[i], 10*i+6);
	}
	return data;
}

bool OrderModifyExchange::setData(const Uint8 *data, int dataLength)
{
	if ((dataLength%10)!=0)
		return false;
	
	this->length=dataLength/10;

	free(this->gid);
	free(this->receiveRessourceMask);
	free(this->sendRessourceMask);
	free(this->data);

	this->gid=(Uint16 *)malloc(length*2);
	this->receiveRessourceMask=(Uint32 *)malloc(length*4);
	this->sendRessourceMask=(Uint32 *)malloc(length*4);
	this->data=(Uint8 *)malloc(10*length);

	for (int i=0; i<length; i++)
	{
		(this->gid)[i]=getUint16(data, 10*i+0);
		(this->receiveRessourceMask)[i]=getUint32(data, 10*i+2);
		(this->sendRessourceMask)[i]=getUint32(data, 10*i+6);
	}
	
	memcpy(this->data, data, dataLength);
	return true;
}

// OrderModifySwarms' code

OrderModifySwarms::OrderModifySwarms(const Uint8 *data, int dataLength)
:OrderModify()
{
	assert(NB_UNIT_TYPE==3);
	assert((dataLength%14)==0);
	bool good=setData(data, dataLength);
	assert(good);
}

OrderModifySwarms::OrderModifySwarms(Uint16 *gid, Sint32 *ratios, int length)
{
	assert(NB_UNIT_TYPE==3);
	this->length=length;

	this->gid=(Uint16 *)malloc(length*2);
	this->ratio=(Sint32 *)malloc(NB_UNIT_TYPE*length*4);
	this->data=(Uint8 *)malloc(14*length);

	memcpy(this->gid, gid, length*2);
	memcpy(this->ratio, ratios, NB_UNIT_TYPE*length*4);
}

OrderModifySwarms::~OrderModifySwarms()
{
	free(gid);
	free(ratio);
	free(data);
}

Uint8 *OrderModifySwarms::getData(void)
{
	assert(NB_UNIT_TYPE==3);
	
	for (int i=0; i<length; i++)
	{
		addUint16(data, gid[i], 14*i+0);
		for (int j=0; j<NB_UNIT_TYPE; j++)
			addSint32(data, ratio[i*3+j], (14*i)+(4*j)+2);
	}
	return data;
}

bool OrderModifySwarms::setData(const Uint8 *data, int dataLength)
{
	if (NB_UNIT_TYPE!=3)
		return false;
	if ((dataLength%14)!=0)
		return false;

	this->length=dataLength/14;

	this->gid=(Uint16 *)malloc(length*2);
	this->ratio=(Sint32 *)malloc(NB_UNIT_TYPE*length*4);
	this->data=(Uint8 *)malloc(14*length);

	for (int i=0; i<length; i++)
	{
		this->gid[i]=getUint16(data, 14*i+0);
		for (int j=0; j<NB_UNIT_TYPE; j++)
			this->ratio[i*3+j]=getSint32(data, (14*i)+(4*j)+2);
	}

	memcpy(this->data, data, 14*length);
	return true;
}

// OrderModifyFlags' code

OrderModifyFlags::OrderModifyFlags(const Uint8 *data, int dataLength)
:OrderModify()
{
	assert((dataLength%6)==0);
	bool good=setData(data, dataLength);
	assert(good);
}

OrderModifyFlags::OrderModifyFlags(Uint16 *gid, Sint32 *range, int length)
{
	this->length=length;

	this->gid=(Uint16 *)malloc(length*2);
	this->range=(Sint32 *)malloc(length*4);

	this->data=(Uint8 *)malloc(6*length);

	memcpy(this->gid, gid, length*2);
	memcpy(this->range, range, length*4);
}

OrderModifyFlags::~OrderModifyFlags()
{
	free(gid);
	free(range);
	free(data);
}

Uint8 *OrderModifyFlags::getData(void)
{
	for (int i=0; i<(this->length); i++)
	{
		addUint16(data, (this->gid)[i], 6*i+0);
		addSint32(data, (this->range)[i], 6*i+2);
	}
	return data;
}

bool OrderModifyFlags::setData(const Uint8 *data, int dataLength)
{
	if((dataLength%6)!=0)
		return false;
	
	this->length=dataLength/6;

	this->gid=(Uint16 *)malloc(length*2);
	this->range=(Sint32 *)malloc(length*4);

	this->data=(Uint8 *)malloc(6*length);

	for (int i=0; i<length; i++)
	{
		(this->gid)[i]=getUint16(data, 6*i+0);
		(this->range)[i]=getSint32(data, 6*i+2);
   	}

	memcpy(this->data, data, dataLength);
	return true;
}

// OrderMoveFlags' code

OrderMoveFlags::OrderMoveFlags(const Uint8 *data, int dataLength)
:OrderModify()
{
	assert((dataLength%11)==0);
	bool good=setData(data, dataLength);
	assert(good);
}

OrderMoveFlags::OrderMoveFlags(Uint16 *gid, Sint32 *x, Sint32 *y, bool *drop, int length)
{
	this->length=length;
	
	this->gid=(Uint16 *)malloc(length*2);
	this->x=(Sint32 *)malloc(length*4);
	this->y=(Sint32 *)malloc(length*4);
	this->drop=(bool *)malloc(length*sizeof(bool));

	this->data=(Uint8 *)malloc(11*length);

	memcpy(this->gid, gid, length*2);
	memcpy(this->x, x, length*4);
	memcpy(this->y, y, length*4);
	memcpy(this->drop, drop, length*sizeof(bool));
}

OrderMoveFlags::~OrderMoveFlags()
{
	free(gid);
	free(x);
	free(y);
	free(drop);
	free(data);
}

Uint8 *OrderMoveFlags::getData(void)
{
	for (int i=0; i<length; i++)
	{
		addUint16(data, (this->gid)[i], 11*i+0);
		addSint32(data, (this->x)[i], 11*i+2);
		addSint32(data, (this->y)[i], 11*i+6);
		addUint8(data, (Uint8)((this->drop)[i]), 11*i+10);
	}
	return data;
}

bool OrderMoveFlags::setData(const Uint8 *data, int dataLength)
{
	if((dataLength%11)!=0)
		return false;
	
	this->length=dataLength/11;

	this->gid=(Uint16 *)malloc(length*2);
	this->x=(Sint32 *)malloc(length*4);
	this->y=(Sint32 *)malloc(length*4);
	this->drop=(bool *)malloc(length*sizeof(bool));

	this->data=(Uint8 *)malloc(11*length);

	for (int i=0; i<length; i++)
	{
		(this->gid)[i]=getUint16(data, 11*i+0);
		(this->x)[i]=getSint32(data, 11*i+2);
		(this->y)[i]=getSint32(data, 11*i+6);
		(this->drop)[i]=getUint8(data, 11*i+10);
	}

	memcpy(this->data, data, dataLength);
	
	return true;
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
	assert(dataLength>=5);
	bool good=setData(data, dataLength);
	assert(good);
}

MessageOrder::MessageOrder(Uint32 recepientsMask, Uint32 messageOrderType, const char *text)
{
	this->length=strlen(text)+9;
	this->data=(Uint8 *)malloc(length);
	memcpy(data+8, text, length-9);
	this->data[length-1]=0;
	addUint32(data, recepientsMask, 0);
	addUint32(data, messageOrderType, 4);
	this->recepientsMask=recepientsMask;
	this->messageOrderType=messageOrderType;
}

MessageOrder::~MessageOrder()
{
	free(this->data);
}

Uint8 *MessageOrder::getData(void)
{
	return this->data;
}

bool MessageOrder::setData(const Uint8 *data, int dataLength)
{
	if(dataLength<5)
		return false;

	this->length=dataLength;
	this->recepientsMask=getUint32(data, 0);
	this->messageOrderType=getUint32(data, 4);

	this->data=(Uint8 *)malloc(dataLength);
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

// SubmitCheckSum's code
/*
SubmitCheckSumOrder::SubmitCheckSumOrder(const Uint8 *data, int dataLength)
:MiscOrder()
{
	assert(dataLength==4);
	bool good=setData(data, dataLength);
	assert(good);
}

SubmitCheckSumOrder::SubmitCheckSumOrder(Sint32 checkSumValue)
{
	this->checkSumValue=checkSumValue;
}

Uint8 *SubmitCheckSumOrder::getData(void)
{
	addUint32(data, this->checkSumValue, 0);
	return data;
}

bool SubmitCheckSumOrder::setData(const Uint8 *data, int dataLength)
{
	if(dataLength!=getDataLength())
		return false;

	this->checkSumValue=getUint32(data, 0);
	
	memcpy(this->data, data, dataLength);
	
	return true;
}
*/
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

RequestingDeadAwayOrder::RequestingDeadAwayOrder(Sint32 player, Sint32 missingStep, Sint32 lastAviableStep)
{
	this->player=player;
	this->missingStep=missingStep;
	this->lastAviableStep=lastAviableStep;
}

Uint8 *RequestingDeadAwayOrder::getData(void)
{
	addUint32(data, this->player, 0);
	addUint32(data, this->missingStep, 4);
	addUint32(data, this->lastAviableStep, 8);
	return data;
}

bool RequestingDeadAwayOrder::setData(const Uint8 *data, int dataLength)
{
	if(dataLength!=getDataLength())
		return false;

	this->player=getUint32(data, 0);
	this->missingStep=getUint32(data, 4);
	this->lastAviableStep=getUint32(data, 8);
	
	memcpy(this->data, data, dataLength);
	
	return true;
}

// NoMoreOrdersAviable code

NoMoreOrdersAviable::NoMoreOrdersAviable(const Uint8 *data, int dataLength)
:MiscOrder()
{
	assert(dataLength==8);
	bool good=setData(data, dataLength);
	assert(good);
}

NoMoreOrdersAviable::NoMoreOrdersAviable(Sint32 player, Sint32 lastAviableStep)
{
	this->player=player;
	this->lastAviableStep=lastAviableStep;
}

Uint8 *NoMoreOrdersAviable::getData(void)
{
	addUint32(data, this->player, 0);
	addUint32(data, this->lastAviableStep, 4);
	return data;
}

bool NoMoreOrdersAviable::setData(const Uint8 *data, int dataLength)
{
	if(dataLength!=getDataLength())
		return false;

	this->player=getUint32(data, 0);
	this->lastAviableStep=getUint32(data, 4);
	
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
