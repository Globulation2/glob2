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

#ifndef __ORDER_H
#define __ORDER_H

#include <assert.h>
#include <SDL.h>

#include "NetConsts.h"
#include "Ressource.h"
#include "UnitConsts.h"

class Order
{
public:
 	Order(void);
	virtual ~Order(void) {}
	virtual Uint8 getOrderType(void)=0;

	static Order *getOrder(const Uint8 *netData, int netDataLength);

	virtual Uint8 *getData(void)=0;
	virtual bool setData(const Uint8 *data, int dataLength)=0;
	virtual int getDataLength(void)=0;
	
	int sender; // sender player number, setby NetGame in getOrder() only
	Uint8 wishedLatency;
	Uint8 wishedDelay;
	bool latencyPadding; // True if this order has been added to increase latency.
	Uint32 ustep;
	Uint32 gameCheckSum;
};


// Creation orders
class OrderCreate:public Order
{
public:
	OrderCreate(const Uint8 *data, int dataLength);
	OrderCreate(Uint32 team, Sint32 posX, Sint32 posY, Sint32 typeNumber);
	virtual ~OrderCreate(void) {}
	Uint8 getOrderType(void) { return ORDER_CREATE; }
	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength);
	int getDataLength(void) { return 16; }

	Uint32 team;
	Sint32 posX;
	Sint32 posY;
	Sint32 typeNumber;

 private:
	Uint8 data[16];
};


// Deletion orders

class OrderDelete:public Order
{
public:
	OrderDelete(const Uint8 *data, int dataLength);
	OrderDelete(Uint16 gid);
	virtual ~OrderDelete(void) {}
	Uint8 getOrderType(void) { return ORDER_DELETE; }
	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength);
	int getDataLength(void) { return 2; }

	Uint16 gid;

protected:
	Uint8 data[2];
};

class OrderCancelDelete:public Order
{
public:
	OrderCancelDelete(const Uint8 *data, int dataLength);
	OrderCancelDelete(Uint16 gid);
	virtual ~OrderCancelDelete(void) {}
	Uint8 getOrderType(void) { return ORDER_CANCEL_DELETE; }
	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength);
	int getDataLength(void) { return 2; }

	Uint16 gid;

protected:
	Uint8 data[2];
};

class OrderConstruction:public Order //! Means Upgarde or Repair Order
{
public:
	OrderConstruction(const Uint8 *data, int dataLength);
	OrderConstruction(Uint16 gid);
	virtual ~OrderConstruction(void) {}
	Uint8 getOrderType(void) { return ORDER_CONSTRUCTION; }
	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength);
	int getDataLength(void) { return 2; }

	Uint16 gid;

protected:
	Uint8 data[2];
};

class OrderCancelConstruction:public Order
{
public:
	OrderCancelConstruction(const Uint8 *data, int dataLength);
	OrderCancelConstruction(Uint16 gid);
	virtual ~OrderCancelConstruction(void) {}
	Uint8 getOrderType(void) { return ORDER_CANCEL_CONSTRUCTION; }
	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength);
	int getDataLength(void) { return 2; }

	Uint16 gid;

protected:
	Uint8 data[2];
};


// Modification orders

class OrderModify:public Order
{
public:
 	OrderModify();
	virtual ~OrderModify(void) {}
};

class OrderModifyBuilding:public OrderModify
{
public:
	OrderModifyBuilding(const Uint8 *data, int dataLength);
	OrderModifyBuilding(Uint16 gid, Uint16 numberRequested);
	virtual ~OrderModifyBuilding(void) {}

	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength);
	int getDataLength(void) { return 4; }
	Uint8 getOrderType(void) { return ORDER_MODIFY_BUILDING; }

	Uint16 gid;
	Uint16 numberRequested;
	
protected:
	Uint8 data[4];
};

class OrderModifyExchange:public OrderModify
{
public:
	OrderModifyExchange(const Uint8 *data, int dataLength);
	OrderModifyExchange(Uint16 gid, Uint32 receiveRessourceMask, Uint32 sendRessourceMask);
	virtual ~OrderModifyExchange(void) {}

	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength);
	int getDataLength(void) { return 10; }
	Uint8 getOrderType(void) { return ORDER_MODIFY_EXCHANGE; }

	Uint16 gid;
	Uint32 receiveRessourceMask;
	Uint32 sendRessourceMask;
	
protected:
	Uint8 data[10];
};

class OrderModifySwarm:public OrderModify
{
public:
	OrderModifySwarm(const Uint8 *data, int dataLength);
	OrderModifySwarm(Uint16 gid, Sint32 ratio[NB_UNIT_TYPE]);
	virtual ~OrderModifySwarm(void) {}

	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength);
	int getDataLength(void) { return 2+4*NB_UNIT_TYPE; }
	Uint8 getOrderType(void) { return ORDER_MODIFY_SWARM; }

	Uint16 gid;
	Sint32 ratio[NB_UNIT_TYPE];

protected:
	Uint8 data[10];
};

class OrderModifyFlag:public OrderModify
{
public:
	OrderModifyFlag(const Uint8 *data, int dataLength);
	OrderModifyFlag(Uint16 gid, Sint32 range);
	virtual ~OrderModifyFlag(void) {}

	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength);
	int getDataLength(void) { return 6; }
	Uint8 getOrderType(void) { return ORDER_MODIFY_FLAG; }

	Uint16 gid;
	Sint32 range;

protected:
	Uint8 data[6];
};

class OrderModifyClearingFlag:public OrderModify
{
public:
	OrderModifyClearingFlag(const Uint8 *data, int dataLength);
	OrderModifyClearingFlag(Uint16 gid, bool clearingRessources[BASIC_COUNT]);
	virtual ~OrderModifyClearingFlag(void);

	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength);
	int getDataLength(void) { return 2+BASIC_COUNT; }
	Uint8 getOrderType(void) { return ORDER_MODIFY_CLEARING_FLAG; }

	Uint16 gid;
	bool clearingRessources[BASIC_COUNT];

protected:
	Uint8 *data;
};

class OrderMoveFlag:public OrderModify
{
public:
	OrderMoveFlag(const Uint8 *data, int dataLength);
	OrderMoveFlag(Uint16 gid, Sint32 x, Sint32 y, bool drop);
	virtual ~OrderMoveFlag(void) {}

	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength);
	int getDataLength(void) { return 11; }
	Uint8 getOrderType(void) { return ORDER_MOVE_FLAG; }

	Uint16 gid;
	Sint32 x;
	Sint32 y;
	bool drop;

protected:
	Uint8 data[11];
};



// Misc orders

class MiscOrder:public Order
{
public:
	MiscOrder();
	virtual ~MiscOrder(void) { }
};

class NullOrder:public MiscOrder
{
public:
	NullOrder();
	virtual ~NullOrder(void) { }

	Uint8 *getData(void) { return NULL; }
	bool setData(const Uint8 *data, int dataLength) { return true; }
	int getDataLength(void) { return 0; }
	Uint8 getOrderType(void) { return ORDER_NULL; }
};

//! only used as a communication channel between NetGame and GameGUI.
class QuitedOrder:public MiscOrder
{
public:
	QuitedOrder();
	virtual ~QuitedOrder(void) { }

	Uint8 *getData(void) { return NULL; }
	bool setData(const Uint8 *data, int dataLength) { return true; }
	int getDataLength(void) { return 0; }
	Uint8 getOrderType(void) { return ORDER_QUITED; }
};

//! only used as a communication channel between NetGame and GameGUI.
class DeconnectedOrder:public MiscOrder
{
public:
	DeconnectedOrder();
	virtual ~DeconnectedOrder(void) { }

	Uint8 *getData(void) { return NULL; }
	bool setData(const Uint8 *data, int dataLength) { return true; }
	int getDataLength(void) { return 0; }
	Uint8 getOrderType(void) { return ORDER_DECONNECTED; }
};

class MessageOrder:public MiscOrder
{
public:
	MessageOrder(const Uint8 *data, int dataLength);
	MessageOrder(Uint32 recepientsMask, Uint32 messageOrderType, const char *text);
	virtual ~MessageOrder(void);

	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength);
	int getDataLength(void) { return length; }
	char *getText(void) { return (char *)(data+9); }
	Uint8 getOrderType(void) { return ORDER_TEXT_MESSAGE; }

	Uint32 recepientsMask;
	enum MessageOrderType
	{
		BAD_MESSAGE_TYPE=0,
		NORMAL_MESSAGE_TYPE=1,
		PRIVATE_MESSAGE_TYPE=2,
		PRIVATE_RECEIPT_TYPE=3
	};
	Uint32 messageOrderType;

 protected:
	Uint8 *data;
	int length;
};

class SetAllianceOrder:public MiscOrder
{
public:
	SetAllianceOrder(const Uint8 *data, int dataLength);
	SetAllianceOrder(Uint32 teamNumber, Uint32 alliedMask, Uint32 enemyMask, Uint32 visionExchangeMask, Uint32 visionFoodMask, Uint32 visionOtherMask);
	virtual ~SetAllianceOrder(void) { }

	Uint8 getOrderType(void) { return ORDER_SET_ALLIANCE; }
	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength);
	int getDataLength(void) { return 24; }

	Uint32 teamNumber;
	Uint32 alliedMask;
	Uint32 enemyMask;
	Uint32 visionExchangeMask;
	Uint32 visionFoodMask;
	Uint32 visionOtherMask;

 protected:
	Uint8 data[24];
};

class MapMarkOrder:public MiscOrder
{
public:
	MapMarkOrder(const Uint8 *data, int dataLength);
	MapMarkOrder(Uint32 teamNumber, Sint32 x, Sint32 y);
	virtual ~MapMarkOrder(void) { }
	
	Uint8 getOrderType(void) { return ORDER_MAP_MARK; }
	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength);
	int getDataLength(void) { return 12; }

	Uint32 teamNumber;
	Sint32 x;
	Sint32 y;

private:
	Uint8 data[12];
};

// Net orders

class WaitingForPlayerOrder:public MiscOrder
{
public:
	WaitingForPlayerOrder(const Uint8 *data, int dataLength);
	WaitingForPlayerOrder(Uint32 maskAwayPlayer);
	virtual ~WaitingForPlayerOrder(void) { }

	Uint8 getOrderType(void) { return ORDER_WAITING_FOR_PLAYER; }
	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength);
	int getDataLength(void) { return 4; }

	Uint32 maskAwayPlayer;
	
private:
	Uint8 data[4];
};

class PauseGameOrder:public MiscOrder
{
public:
	PauseGameOrder(const Uint8 *data, int dataLength);
	PauseGameOrder(bool startPause);
	virtual ~PauseGameOrder(void) { }

	Uint8 getOrderType(void) { return ORDER_PAUSE_GAME; }
	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength);
	int getDataLength(void) { return 1; }

	bool pause;
	
private:
	Uint8 data[1];
};

class DroppingPlayerOrder:public MiscOrder
{
public:
	DroppingPlayerOrder(const Uint8 *data, int dataLength);
	DroppingPlayerOrder(Uint32 dropingPlayersMask);
	virtual ~DroppingPlayerOrder(void) { }

	Uint8 getOrderType(void) { return ORDER_DROPPING_PLAYER; }
	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength);
	int getDataLength(void) { return 4; }
	
	Uint32 dropingPlayersMask;
	
private:
	Uint8 data[4];
};

class RequestingDeadAwayOrder:public MiscOrder
{
public:
	RequestingDeadAwayOrder(const Uint8 *data, int dataLength);
	RequestingDeadAwayOrder(Sint32 player, Sint32 missingStep, Sint32 lastAviableStep);
	virtual ~RequestingDeadAwayOrder(void) { }

	Uint8 getOrderType(void) { return ORDER_REQUESTING_AWAY; }
	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength);
	int getDataLength(void) { return 12; }
	
	Sint32 player;
	Sint32 missingStep;
	Sint32 lastAviableStep;
	
private:
	Uint8 data[12];
};

class NoMoreOrdersAviable:public MiscOrder
{
public:
	NoMoreOrdersAviable(const Uint8 *data, int dataLength);
	NoMoreOrdersAviable(Sint32 player, Sint32 lastAviableStep);
	virtual ~NoMoreOrdersAviable(void) { }

	Uint8 getOrderType(void) { return ORDER_NO_MORE_ORDER_AVIABLES; }
	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength);
	int getDataLength(void) { return 8; }

	Sint32 player;
	Sint32 lastAviableStep;
	
private:
	Uint8 data[8];
};

class PlayerQuitsGameOrder:public MiscOrder
{
public:
	PlayerQuitsGameOrder(const Uint8 *data, int dataLength);
	PlayerQuitsGameOrder(Sint32 player);
	virtual ~PlayerQuitsGameOrder(void) { }

	Uint8 getOrderType(void) { return ORDER_PLAYER_QUIT_GAME; }
	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength);
	int getDataLength(void) { return 4; }
	
	Sint32 player;
	
private:
	Uint8 data[4];
};

#endif
 
