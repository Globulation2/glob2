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

#include "Order.h"

Order *Order::getOrder(const char *netData, int netDataLength)
{
	if (netDataLength<1)
		return NULL;
	
	switch (netData[0])
	{

	case ORDER_CREATE:
	{
		return new OrderCreate(netData+4,netDataLength-4);	  
	}
	case ORDER_DELETE:
	{
		return new OrderDelete(netData+4,netDataLength-4);
	}
	case ORDER_UPGRADE:
	{
		return new OrderUpgrade(netData+4,netDataLength-4);
	}
	case ORDER_CANCEL_UPGRADE:
	{
		return new OrderCancelUpgrade(netData+4,netDataLength-4);
	}
	case PLAYER_EXPLAINS_HOST_IP:
	{
		return new PlayerExplainsHostIP(netData+4,netDataLength-4);
	}
	case ORDER_MODIFY_UNIT:
	{
		assert(false);
		return new OrderModifyUnits(netData+4,netDataLength-4);
	}
	case ORDER_MODIFY_BUILDING:
	{
		return new OrderModifyBuildings(netData+4,netDataLength-4);
	}
	case ORDER_MODIFY_SWARM:
	{
		return new OrderModifySwarms(netData+4,netDataLength-4);
	}
	case ORDER_MODIFY_FLAG:
	{
		return new OrderModifyFlags(netData+4,netDataLength-4);
	}
	case ORDER_MOVE_FLAG:
	{
		return new OrderMoveFlags(netData+4,netDataLength-4);
	}
	case ORDER_QUITED:
	{
		return new QuitedOrder();
	}
	case ORDER_NULL:
	{
		return new NullOrder();
	}
	case ORDER_TEXT_MESSAGE:
	{
		return new MessageOrder(netData+4,netDataLength-4);
	}
	case ORDER_SET_ALLIANCE:
	{
		return new SetAllianceOrder(netData+4,netDataLength-4);
	}
	case ORDER_SUBMIT_CHECK_SUM:
	{
		return new SubmitCheckSumOrder(netData+4,netDataLength-4);
	}
	case ORDER_WAITING_FOR_PLAYER:
	{
		return new WaitingForPlayerOrder(netData+4,netDataLength-4);
	}
	case ORDER_DROPPING_PLAYER:
	{
		return new DroppingPlayerOrder(netData+4,netDataLength-4);
	}
	case ORDER_REQUESTING_AWAY:
	{
		return new RequestingDeadAwayOrder(netData+4,netDataLength-4);
	}
	case ORDER_NO_MORE_ORDER_AVIABLES:
	{
		return new NoMoreOrdersAviable(netData+4,netDataLength-4);
	}
	case ORDER_PLAYER_QUIT_GAME :
	{
		return new PlayerQuitsGameOrder(netData+4,netDataLength-4);
	}
	default:
		printf("Bad packet recieved in Order.cpp (%d)\n", netData[0]);
		
	}
	return NULL;
}

// OrderCreate's code

OrderCreate::OrderCreate(const char *data, int dataLength)
{
	assert(dataLength==16);
	
	setData(data, dataLength);
}

OrderCreate::OrderCreate(Uint32 team, Sint32 posX, Sint32 posY, BuildingType::BuildingTypeNumber typeNumber)
{
	this->team=team;
	this->posX=posX;
	this->posY=posY;
	this->typeNumber=typeNumber;
}

char *OrderCreate::getData(void)
{
	addSint32(data, this->team, 0);
	addSint32(data, this->posX, 4);
	addSint32(data, this->posY, 8);
	addUint32(data, (Uint32)this->typeNumber, 12);
	
	return data;
}

bool OrderCreate::setData(const char *data, int dataLength)
{
	if(dataLength!=getDataLength())
		return false;
	
	this->team=getSint32(data, 0);
	this->posX=getSint32(data, 4);
	this->posY=getSint32(data, 8);
	this->typeNumber=(BuildingType::BuildingTypeNumber)getUint32(data, 12);
	
	memcpy(this->data,data,dataLength);
	
	return true;
}

// OrderDelete's code

OrderDelete::OrderDelete(const char *data, int dataLength)
{
	assert(dataLength==4);

	setData(data, dataLength);
}

OrderDelete::OrderDelete(Sint32 UID)
{
	this->UID=UID;
}

char *OrderDelete::getData(void)
{
	addSint32(data, this->UID, 0);
	return data;
}

bool OrderDelete::setData(const char *data, int dataLength)
{
	if(dataLength!=getDataLength())
		return false;
	
	this->UID=getSint32(data, 0);

	memcpy(this->data,data,dataLength);
	
	return true;
}

// OrderUpgrade's code

OrderUpgrade::OrderUpgrade(const char *data, int dataLength)
{
	assert(dataLength==4);
	
	setData(data, dataLength);

}

OrderUpgrade::OrderUpgrade(Sint32 UID)
{
	this->UID=UID;
}

char *OrderUpgrade::getData(void)
{
	addSint32(data, this->UID, 0);
	return data;
}

bool OrderUpgrade::setData(const char *data, int dataLength)
{
	if(dataLength!=getDataLength())
		return false;
	
	this->UID=getSint32(data, 0);
	
	memcpy(this->data,data,dataLength);
	
	return true;
}

// OrderCancelUpgrade's code

OrderCancelUpgrade::OrderCancelUpgrade(const char *data, int dataLength)
{
	assert(dataLength==4);
	
	setData(data, dataLength);

}

OrderCancelUpgrade::OrderCancelUpgrade(Sint32 UID)
{
	this->UID=UID;
}

char *OrderCancelUpgrade::getData(void)
{
	addSint32(data, this->UID, 0);
	return data;
}

bool OrderCancelUpgrade::setData(const char *data, int dataLength)
{
	if(dataLength!=getDataLength())
		return false;
	
	this->UID=getSint32(data, 0);
	
	memcpy(this->data,data,dataLength);
	
	return true;
}

// OrderModifyUnits' code

OrderModifyUnits::OrderModifyUnits(const char *data, int dataLength)
{
	assert((dataLength%12)==0);
	
	setData(data, dataLength);
}

OrderModifyUnits::OrderModifyUnits(Sint32 *UID, Sint32 *trigHP, Sint32 *trigHungry, int length)
{
	this->length=length;

	this->UID=(Sint32 *)malloc(length*4);
	this->trigHP=(Sint32 *)malloc(length*4);
	this->trigHungry=(Sint32 *)malloc(length*4);

	this->data=(char *)malloc(12*length);

	memcpy(this->UID,UID,length);
	memcpy(this->trigHP,trigHP,length);
	memcpy(this->trigHungry,trigHungry,length);
}

OrderModifyUnits::~OrderModifyUnits()
{
	free(data);
	free(UID);
	free(trigHP);
	free(trigHungry);
}

char *OrderModifyUnits::getData(void)
{
	int i;
	for (i=0; i<(this->length); i++)
	{
		addSint32(data, (this->UID )[i], 12*i+0);
		addUint32(data, (this->trigHP)[i], 12*i+4);
		addUint32(data, (this->trigHungry)[i], 12*i+8);
	}
	return data;
}

bool OrderModifyUnits::setData(const char *data, int dataLength)
{
	if ((dataLength%12)!=0)
		return false;
	
	this->length=dataLength/12;

	this->UID=(Sint32 *)malloc(length*4);
	this->trigHP=(Sint32 *)malloc(length*4);
	this->trigHungry=(Sint32 *)malloc(length*4);

	this->data=(char *)malloc(12*length);

	int i;
	for (i=0; i<(this->length); i++)
	{
		(this->UID)[i]=getSint32(data, 12*i+0);
		(this->trigHP)[i]=getUint32(data, 12*i+4);
		(this->trigHungry)[i]=getUint32(data, 12*i+8);
	}

	memcpy(this->data,data,dataLength);
	
	return true;
}


// OrderModifyBuildings' code

OrderModifyBuildings::OrderModifyBuildings(const char *data, int dataLength)
{
	assert((dataLength%8)==0);

	this->length=dataLength/8;

	this->UID=(Sint32 *)malloc(length*4);
	this->numberRequested=(Sint32 *)malloc(length*4);
	this->data=(char *)malloc(8*length);
	
	setData(data, dataLength);
}

OrderModifyBuildings::OrderModifyBuildings(Sint32 *UID, Sint32 *numberRequested, int length)
{
	this->length=length;

	this->UID=(Sint32 *)malloc(length*4);
	this->numberRequested=(Sint32 *)malloc(length*4);
	this->data=(char *)malloc(8*length);

	memcpy(this->UID,UID,length*4);
	memcpy(this->numberRequested,numberRequested,length*4);
	memset(data, 0, length*8);
}

OrderModifyBuildings::~OrderModifyBuildings()
{
	free(data);
	free(UID);
	free(numberRequested);
}

char *OrderModifyBuildings::getData(void)
{
	int i;
	for (i=0; i<(this->length); i++)
	{
		addSint32(data, (this->UID )[i], 8*i+0);
		addUint32(data, (this->numberRequested)[i], 8*i+4);
	}
	return data;
}

bool OrderModifyBuildings::setData(const char *data, int dataLength)
{
	if ((dataLength%8)!=0)
		return false;
	
	if (this->length!=dataLength/8)
	{
		this->length=dataLength/8;
		
		free(this->UID);
		free(this->numberRequested);
		free(this->data);

		this->UID=(Sint32 *)malloc(length*4);
		this->numberRequested=(Sint32 *)malloc(length*4);
		this->data=(char *)malloc(8*length);
		
		assert(false);//remove this assert when you wants to use more than one building modification.
	}
	
	int i;
	for (i=0; i<(this->length); i++)
	{
		(this->UID)[i]=getSint32(data, 8*i+0);
		(this->numberRequested)[i]=getUint32(data, 8*i+4);
	}
	
	memcpy(this->data,data,dataLength);
	
	return true;
}

// OrderModifySwarms' code

OrderModifySwarms::OrderModifySwarms(const char *data, int dataLength)
{
	assert(UnitType::NB_UNIT_TYPE==3);
	assert((dataLength%16)==0);
	
	setData(data, dataLength);
}

OrderModifySwarms::OrderModifySwarms(Sint32 *UID, Sint32 ratio[][UnitType::NB_UNIT_TYPE], int length)
{
	assert(UnitType::NB_UNIT_TYPE==3);
	this->length=length;

	this->UID=(Sint32 *)malloc(length*4);
	this->ratio=(Sint32 *)malloc(UnitType::NB_UNIT_TYPE*length*4);
	this->data=(char *)malloc(16*length);

	memcpy(this->UID, UID, length*4);
	memcpy(this->ratio, ratio, UnitType::NB_UNIT_TYPE*length*4);
}

OrderModifySwarms::~OrderModifySwarms()
{
	free(UID);
	free(ratio);
	free(data);
}

char *OrderModifySwarms::getData(void)
{
	assert(UnitType::NB_UNIT_TYPE==3);
	
	int i;
	for (i=0; i<(length); i++)
	{
		addSint32(data, (UID)[i], 16*i+0);
		for (int j=0; j<UnitType::NB_UNIT_TYPE; j++)
			addSint32(data, ratio[i*3+j], (16*i)+(4*j)+4);
	}
	return data;
}

bool OrderModifySwarms::setData(const char *data, int dataLength)
{
	if (UnitType::NB_UNIT_TYPE!=3)
		return false;
	if ((dataLength%16)!=0)
		return false;

	this->length=dataLength/16;

	this->UID=(Sint32 *)malloc(length*4);
	this->ratio=(Sint32 *)malloc(UnitType::NB_UNIT_TYPE*length*4);
	this->data=(char *)malloc(16*length);

	int i;
	for (i=0; i<(this->length); i++)
	{
		this->UID[i]=getSint32(data, 16*i+0);
		for (int j=0; j<UnitType::NB_UNIT_TYPE; j++)
			this->ratio[i*3+j]=getSint32(data, (16*i)+(4*j)+4);
	}

	memcpy(this->data, data, 16*length);
	
	return true;
}

// OrderModifyFlags' code

OrderModifyFlags::OrderModifyFlags(const char *data, int dataLength)
{
	assert((dataLength%8)==0);
	
	setData(data, dataLength);
}

OrderModifyFlags::OrderModifyFlags(Sint32 *UID, Sint32 *range, int length)
{
	this->length=length;

	this->UID=(Sint32 *)malloc(length*4);
	this->range=(Sint32 *)malloc(length*4);

	this->data=(char *)malloc(8*length);

	memcpy(this->UID,UID,length*4);
	memcpy(this->range,range,length*4);
}

OrderModifyFlags::~OrderModifyFlags()
{
	free(UID);
	free(range);
	free(data);
}

char *OrderModifyFlags::getData(void)
{
	int i;
	for (i=0; i<(this->length); i++)
	{
		addSint32(data, (this->UID )[i], 8*i+0);
		addSint32(data, (this->range )[i], 8*i+4);
	}
	return data;
}

bool OrderModifyFlags::setData(const char *data, int dataLength)
{
	if((dataLength%8)!=0)
		return false;
	
	this->length=dataLength/8;

	this->UID=(Sint32 *)malloc(length*4);
	this->range=(Sint32 *)malloc(length*4);

	this->data=(char *)malloc(8*length);

	int i;
	for (i=0; i<(this->length); i++)
	{
		(this->UID )[i]=getSint32(data, 8*i+0);
		(this->range )[i]=getSint32(data, 8*i+4);
   	}

	memcpy(this->data,data,dataLength);
	
	return true;
}

// OrderMoveFlags' code

OrderMoveFlags::OrderMoveFlags(const char *data, int dataLength)
{
	assert((dataLength%12)==0);
	
	setData(data, dataLength);
}

OrderMoveFlags::OrderMoveFlags(Sint32 *UID, Sint32 *x, Sint32 *y, int length)
{
	this->length=length;

	this->UID=(Sint32 *)malloc(length*4);
	this->x=(Sint32 *)malloc(length*4);
	this->y=(Sint32 *)malloc(length*4);

	this->data=(char *)malloc(12*length);

	memcpy(this->UID,UID,length*4);
	memcpy(this->x,x,length*4);
	memcpy(this->y,y,length*4);
}

OrderMoveFlags::~OrderMoveFlags()
{
	free(UID);
	free(x);
	free(y);
	free(data);
}

char *OrderMoveFlags::getData(void)
{
	int i;
	for (i=0; i<(this->length); i++)
	{
		addSint32(data, (this->UID )[i], 12*i+0);
		addSint32(data, (this->x )[i], 12*i+4);
		addSint32(data, (this->y )[i], 12*i+8);
	}
	return data;
}

bool OrderMoveFlags::setData(const char *data, int dataLength)
{
	if((dataLength%12)!=0)
		return false;
	
	this->length=dataLength/12;

	this->UID=(Sint32 *)malloc(length*4);
	this->x=(Sint32 *)malloc(length*4);
	this->y=(Sint32 *)malloc(length*4);

	this->data=(char *)malloc(12*length);

	int i;
	for (i=0; i<(this->length); i++)
	{
		(this->UID )[i]=getSint32(data, 12*i+0);
		(this->x )[i]=getSint32(data, 12*i+4);
		(this->y )[i]=getSint32(data, 12*i+8);
   	}

	memcpy(this->data,data,dataLength);
	
	return true;
}

// MessageOrder's code

MessageOrder::MessageOrder(const char *data, int dataLength)
{
	assert(dataLength>=5);

	setData(data, dataLength);
}

MessageOrder::MessageOrder(Uint32 recepientsMask, const char *text)
{
	this->length=strlen(text)+5;
	this->data=(char *)malloc(length);
	memcpy(data+4,text,length-5);
	this->data[length-1]=0;
	addUint32(data, recepientsMask, 0);
	this->recepientsMask=recepientsMask;
}

MessageOrder::~MessageOrder()
{
	free(this->data);
}

char *MessageOrder::getData(void)
{
	return this->data;
}

bool MessageOrder::setData(const char *data, int dataLength)
{
	if(dataLength<5)
		return false;

	this->length=dataLength;
	this->recepientsMask=getUint32(data, 0);

	this->data=(char *)malloc(dataLength);
	memcpy(this->data, data, dataLength);

	return true;
}

// OrderDelete's code

SetAllianceOrder::SetAllianceOrder(const char *data, int dataLength)
{
	assert(dataLength==12);

	setData(data, dataLength);
}

SetAllianceOrder::SetAllianceOrder(Uint32 teamNumber, Uint32 allianceMask, Uint32 visionMask)
{
	this->teamNumber=teamNumber;
	this->allianceMask=allianceMask;
	this->visionMask=visionMask;
}

char *SetAllianceOrder::getData(void)
{
	addUint32(data, this->teamNumber, 0);
	addUint32(data, this->allianceMask, 4);
	addUint32(data, this->visionMask, 8);
	return data;
}

bool SetAllianceOrder::setData(const char *data, int dataLength)
{
	if(dataLength!=getDataLength())
		return false;

	this->teamNumber=getUint32(data, 0);
	this->allianceMask=getUint32(data, 4);
	this->visionMask=getUint32(data, 8);

	memcpy(this->data,data,dataLength);

	return true;
}

// SubmitCheckSum's code

SubmitCheckSumOrder::SubmitCheckSumOrder(const char *data, int dataLength)
{
	assert(dataLength==4);

	setData(data, dataLength);
}

SubmitCheckSumOrder::SubmitCheckSumOrder(Sint32 checkSumValue)
{
	this->checkSumValue=checkSumValue;
}

char *SubmitCheckSumOrder::getData(void)
{
	addUint32(data, this->checkSumValue, 0);
	return data;
}

bool SubmitCheckSumOrder::setData(const char *data, int dataLength)
{
	if(dataLength!=getDataLength())
		return false;

	this->checkSumValue=getUint32(data, 0);
	
	memcpy(this->data,data,dataLength);
	
	return true;
}

// WaitingForPlayerOrder's code

WaitingForPlayerOrder::WaitingForPlayerOrder(const char *data, int dataLength)
{
	assert(dataLength==4);

	setData(data, dataLength);
}

WaitingForPlayerOrder::WaitingForPlayerOrder(Uint32 maskAwayPlayer)
{
	this->maskAwayPlayer=maskAwayPlayer;
}

char *WaitingForPlayerOrder::getData(void)
{
	addUint32(data, this->maskAwayPlayer, 0);
	return data;
}

bool WaitingForPlayerOrder::setData(const char *data, int dataLength)
{
	if(dataLength!=getDataLength())
		return false;

	this->maskAwayPlayer=getUint32(data, 0);
	
	memcpy(this->data,data,dataLength);
	
	return true;
}


// DroppingPlayerOrder's code

DroppingPlayerOrder::DroppingPlayerOrder(const char *data, int dataLength)
{
	assert(dataLength==8);

	setData(data, dataLength);
}

DroppingPlayerOrder::DroppingPlayerOrder(Uint32 stayingPlayersMask, Sint32 dropState)
{
	this->stayingPlayersMask=stayingPlayersMask;
	this->dropState=dropState;
}

char *DroppingPlayerOrder::getData(void)
{
	addUint32(data, this->stayingPlayersMask, 0);
	addUint32(data, this->dropState, 4);
	return data;
}

bool DroppingPlayerOrder::setData(const char *data, int dataLength)
{
	if(dataLength!=getDataLength())
		return false;

	this->stayingPlayersMask=getUint32(data, 0);
	this->dropState=getUint32(data, 4);
	
	memcpy(this->data,data,dataLength);
	
	return true;
}


// RequestingDeadAwayOrder's code

RequestingDeadAwayOrder::RequestingDeadAwayOrder(const char *data, int dataLength)
{
	assert(dataLength==12);

	setData(data, dataLength);
}

RequestingDeadAwayOrder::RequestingDeadAwayOrder(Sint32 player, Sint32 missingStep, Sint32 lastAviableStep)
{
	this->player=player;
	this->missingStep=missingStep;
	this->lastAviableStep=lastAviableStep;
}

char *RequestingDeadAwayOrder::getData(void)
{
	addUint32(data, this->player, 0);
	addUint32(data, this->missingStep, 4);
	addUint32(data, this->lastAviableStep, 8);
	return data;
}

bool RequestingDeadAwayOrder::setData(const char *data, int dataLength)
{
	if(dataLength!=getDataLength())
		return false;

	this->player=getUint32(data, 0);
	this->missingStep=getUint32(data, 4);
	this->lastAviableStep=getUint32(data, 8);
	
	memcpy(this->data,data,dataLength);
	
	return true;
}

// NoMoreOrdersAviable code

NoMoreOrdersAviable::NoMoreOrdersAviable(const char *data, int dataLength)
{
	assert(dataLength==8);

	setData(data, dataLength);
}

NoMoreOrdersAviable::NoMoreOrdersAviable(Sint32 player, Sint32 lastAviableStep)
{
	this->player=player;
	this->lastAviableStep=lastAviableStep;
}

char *NoMoreOrdersAviable::getData(void)
{
	addUint32(data, this->player, 0);
	addUint32(data, this->lastAviableStep, 4);
	return data;
}

bool NoMoreOrdersAviable::setData(const char *data, int dataLength)
{
	if(dataLength!=getDataLength())
		return false;

	this->player=getUint32(data, 0);
	this->lastAviableStep=getUint32(data, 4);
	
	memcpy(this->data,data,dataLength);
	
	return true;
}

// PlayerQuitsGameOrder code

PlayerQuitsGameOrder::PlayerQuitsGameOrder(const char *data, int dataLength)
{
	assert(dataLength==4);

	setData(data, dataLength);
}

PlayerQuitsGameOrder::PlayerQuitsGameOrder(Sint32 player)
{
	this->player=player;
}

char *PlayerQuitsGameOrder::getData(void)
{
	addUint32(data, this->player, 0);
	return data;
}

bool PlayerQuitsGameOrder::setData(const char *data, int dataLength)
{
	if(dataLength!=getDataLength())
		return false;

	this->player=getUint32(data, 0);
	
	memcpy(this->data,data,dataLength);
	
	return true;
}
// PlayerExplainsHostIP code

PlayerExplainsHostIP::PlayerExplainsHostIP(const char *data, int dataLength)
{
	assert(dataLength==8);

	setData(data, dataLength);
}

PlayerExplainsHostIP::PlayerExplainsHostIP(Uint32 host, Uint32 port)
{
	this->host=host;
	this->port=port;
}

char *PlayerExplainsHostIP::getData(void)
{
	addUint32(data, this->host, 0);
	addUint32(data, this->port, 4);
	return data;
}

bool PlayerExplainsHostIP::setData(const char *data, int dataLength)
{
	if(dataLength!=getDataLength())
		return false;

	this->host=getUint32(data, 0);
	this->port=getUint32(data, 4);
	
	memcpy(this->data,data,dataLength);
	
	return true;
}
