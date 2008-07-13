/*
  Copyright (C) 2007 Bradley Arsenault

  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
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
#ifndef DX9_BACKEND	// TODO:Die!
#include <SDL.h>
#else
#include <Types.h>
#endif

#include "NetConsts.h"
#include "Ressource.h"
#include "UnitConsts.h"
#include "BitArray.h"
#include <boost/shared_ptr.hpp>

class Map;

//! An Order represents a synchronized event in the game
class Order
{
public:
	///Contructs an Order
 	Order(void);
	virtual ~Order(void) {}
	///Returns the Order Type
	virtual Uint8 getOrderType(void)=0;

	///Takes in an arbitrary amount of information and returns its assocciatted order
	static boost::shared_ptr<Order> getOrder(const Uint8 *netData, int netDataLength, Uint32 versionMinor);

	///Returns the encoded data buffer of data for the Order
	virtual Uint8 *getData(void)=0;
	
	///Sets the Orders local data from a data buffer
	virtual bool setData(const Uint8 *data, int dataLength, Uint32 versionMinor)=0;
	
	///Returns the length of the data
	virtual int getDataLength(void)=0;
	
	int sender; // sender player number, setby NetGame in getOrder() only
	Uint32 gameCheckSum;
};


//! Building creation order
class OrderCreate:public Order
{
public:
	OrderCreate(const Uint8 *data, int dataLength, Uint32 versionMinor);
	OrderCreate(Sint32 teamNumber, Sint32 posX, Sint32 posY, Sint32 typeNum, Sint32 unitWorking, Sint32 unitWorkingFuture, Sint32 flagRadius=0);
	virtual ~OrderCreate(void) {}
	Uint8 getOrderType(void) { return ORDER_CREATE; }
	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength, Uint32 versionMinor);
	int getDataLength(void) { return 28; }

	Sint32 teamNumber;
	Sint32 posX;
	Sint32 posY;
	Sint32 typeNum;
	Sint32 unitWorking;
	Sint32 unitWorkingFuture;
	Sint32 flagRadius;

 private:
	Uint8 data[28];
};


//! Building deletion order
class OrderDelete:public Order
{
public:
	OrderDelete(const Uint8 *data, int dataLength, Uint32 versionMinor);
	OrderDelete(Uint16 gid);
	virtual ~OrderDelete(void) {}
	Uint8 getOrderType(void) { return ORDER_DELETE; }
	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength, Uint32 versionMinor);
	int getDataLength(void) { return 2; }

	Uint16 gid;

protected:
	Uint8 data[2];
};

//! Cancel a building deletion if pending
class OrderCancelDelete:public Order
{
public:
	OrderCancelDelete(const Uint8 *data, int dataLength, Uint32 versionMinor);
	OrderCancelDelete(Uint16 gid);
	virtual ~OrderCancelDelete(void) {}
	Uint8 getOrderType(void) { return ORDER_CANCEL_DELETE; }
	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength, Uint32 versionMinor);
	int getDataLength(void) { return 2; }

	Uint16 gid;

protected:
	Uint8 data[2];
};

// Upgrade or Repair a building
class OrderConstruction:public Order
{
public:
	OrderConstruction(const Uint8 *data, int dataLength, Uint32 versionMinor);
	OrderConstruction(Uint16 gid, Uint32 unitWorking, Uint32 unitWorkingFuture);
	virtual ~OrderConstruction(void) {}
	Uint8 getOrderType(void) { return ORDER_CONSTRUCTION; }
	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength, Uint32 versionMinor);
	int getDataLength(void) { return 10; }

	Uint16 gid;
	Uint32 unitWorking;
	Uint32 unitWorkingFuture;

protected:
	Uint8 data[10];
};

//! Cancel a building upgarde or repair if pending
class OrderCancelConstruction:public Order
{
public:
	OrderCancelConstruction(const Uint8 *data, int dataLength, Uint32 versionMinor);
	OrderCancelConstruction(Uint16 gid, Uint32 unitWorking);
	virtual ~OrderCancelConstruction(void) {}
	Uint8 getOrderType(void) { return ORDER_CANCEL_CONSTRUCTION; }
	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength, Uint32 versionMinor);
	int getDataLength(void) { return 6; }

	Uint16 gid;
	Uint32 unitWorking;

protected:
	Uint8 data[6];
};


//! Changes the priority of a building
class OrderChangePriority:public Order
{
public:
	OrderChangePriority(const Uint8 *data, int dataLength, Uint32 versionMinor);
	OrderChangePriority(Uint16 gid, Sint32 priority);
	virtual ~OrderChangePriority(void) {}
	Uint8 getOrderType(void) { return ORDER_CHANGE_PRIORITY; }
	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength, Uint32 versionMinor);
	int getDataLength(void) { return 2; }

	Uint16 gid;
	Sint32 priority;

protected:
	Uint8 data[6];
};


//! Modification orders
class OrderModify:public Order
{
public:
 	OrderModify();
	virtual ~OrderModify(void) {}
};

//! Change the number of unit assigned to a building
class OrderModifyBuilding:public OrderModify
{
public:
	OrderModifyBuilding(const Uint8 *data, int dataLength, Uint32 versionMinor);
	OrderModifyBuilding(Uint16 gid, Uint16 numberRequested);
	virtual ~OrderModifyBuilding(void) {}

	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength, Uint32 versionMinor);
	int getDataLength(void) { return 4; }
	Uint8 getOrderType(void) { return ORDER_MODIFY_BUILDING; }

	Uint16 gid;
	Uint16 numberRequested;
	
protected:
	Uint8 data[4];
};

//! Change the 
class OrderModifyExchange:public OrderModify
{
public:
	OrderModifyExchange(const Uint8 *data, int dataLength, Uint32 versionMinor);
	OrderModifyExchange(Uint16 gid, Uint32 receiveRessourceMask, Uint32 sendRessourceMask);
	virtual ~OrderModifyExchange(void) {}

	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength, Uint32 versionMinor);
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
	OrderModifySwarm(const Uint8 *data, int dataLength, Uint32 versionMinor);
	OrderModifySwarm(Uint16 gid, Sint32 ratio[NB_UNIT_TYPE]);
	virtual ~OrderModifySwarm(void) {}

	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength, Uint32 versionMinor);
	int getDataLength(void) { return 2+4*NB_UNIT_TYPE; }
	Uint8 getOrderType(void) { return ORDER_MODIFY_SWARM; }

	Uint16 gid;
	Sint32 ratio[NB_UNIT_TYPE];

protected:
	Uint8 data[14];
};

class OrderModifyFlag:public OrderModify
{
public:
	OrderModifyFlag(const Uint8 *data, int dataLength, Uint32 versionMinor);
	OrderModifyFlag(Uint16 gid, Sint32 range);
	virtual ~OrderModifyFlag(void) {}

	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength, Uint32 versionMinor);
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
	OrderModifyClearingFlag(const Uint8 *data, int dataLength, Uint32 versionMinor);
	OrderModifyClearingFlag(Uint16 gid, bool clearingRessources[BASIC_COUNT]);
	virtual ~OrderModifyClearingFlag(void);

	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength, Uint32 versionMinor);
	int getDataLength(void) { return 2+BASIC_COUNT; }
	Uint8 getOrderType(void) { return ORDER_MODIFY_CLEARING_FLAG; }

	Uint16 gid;
	bool clearingRessources[BASIC_COUNT];

protected:
	Uint8 *data;
};

class OrderModifyMinLevelToFlag:public OrderModify
{
public:
	OrderModifyMinLevelToFlag(const Uint8 *data, int dataLength, Uint32 versionMinor);
	OrderModifyMinLevelToFlag(Uint16 gid, Uint16 minLevelToFlag);
	virtual ~OrderModifyMinLevelToFlag(void);

	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength, Uint32 versionMinor);
	int getDataLength(void) { return 4; }
	Uint8 getOrderType(void) { return ORDER_MODIFY_MIN_LEVEL_TO_FLAG; }

	Uint16 gid;
	Uint16 minLevelToFlag;

protected:
	Uint8 data[4];
};

class OrderMoveFlag:public OrderModify
{
public:
	OrderMoveFlag(const Uint8 *data, int dataLength, Uint32 versionMinor);
	OrderMoveFlag(Uint16 gid, Sint32 x, Sint32 y, bool drop);
	virtual ~OrderMoveFlag(void) {}

	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength, Uint32 versionMinor);
	int getDataLength(void) { return 11; }
	Uint8 getOrderType(void) { return ORDER_MOVE_FLAG; }

	Uint16 gid;
	Sint32 x;
	Sint32 y;
	bool drop;

protected:
	Uint8 data[11];
};

class BrushAccumulator;

class OrderAlterateArea:public OrderModify
{
public:
	OrderAlterateArea(const Uint8 *data, int dataLength, Uint32 versionMinor);
	#ifndef YOG_SERVER_ONLY
	OrderAlterateArea(Uint8 teamNumber, Uint8 type, BrushAccumulator *acc, const Map* map);
	#endif
	virtual ~OrderAlterateArea(void);
	
	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength, Uint32 versionMinor);
	int getDataLength(void);
	
	Uint8 teamNumber;
	Uint8 type;
	Sint16 centerX;
	Sint16 centerY;
	Sint16 minX;
	Sint16 minY;
	Sint16 maxX;
	Sint16 maxY;
	Utilities::BitArray mask;
	
protected:
	Uint8 *_data;
};

class OrderAlterateForbidden:public OrderAlterateArea
{
public:
	OrderAlterateForbidden(const Uint8 *data, int dataLength, Uint32 versionMinor) : OrderAlterateArea(data, dataLength, versionMinor) { }
	#ifndef YOG_SERVER_ONLY
	OrderAlterateForbidden(Uint8 teamNumber, Uint8 type, BrushAccumulator *acc, const Map* map) : OrderAlterateArea(teamNumber, type, acc, map) { }
	#endif
	
	Uint8 getOrderType(void) { return ORDER_ALTERATE_FORBIDDEN; }
};

class OrderAlterateGuardArea:public OrderAlterateArea
{
public:
	OrderAlterateGuardArea(const Uint8 *data, int dataLength, Uint32 versionMinor) : OrderAlterateArea(data, dataLength, versionMinor) { }
	#ifndef YOG_SERVER_ONLY
	OrderAlterateGuardArea(Uint8 teamNumber, Uint8 type, BrushAccumulator *acc, const Map* map) : OrderAlterateArea(teamNumber, type, acc, map) { }
	#endif
	
	Uint8 getOrderType(void) { return ORDER_ALTERATE_GUARD_AREA; }
};

class OrderAlterateClearArea:public OrderAlterateArea
{
public:
	OrderAlterateClearArea(const Uint8 *data, int dataLength, Uint32 versionMinor) : OrderAlterateArea(data, dataLength, versionMinor) { }
	#ifndef YOG_SERVER_ONLY
	OrderAlterateClearArea(Uint8 teamNumber, Uint8 type, BrushAccumulator *acc, const Map* map) : OrderAlterateArea(teamNumber, type, acc, map) { }
	#endif
	
	Uint8 getOrderType(void) { return ORDER_ALTERATE_CLEAR_AREA; }
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
	bool setData(const Uint8 *data, int dataLength, Uint32 versionMinor) { return true; }
	int getDataLength(void) { return 0; }
	Uint8 getOrderType(void) { return ORDER_NULL; }
};

class MessageOrder:public MiscOrder
{
public:
	MessageOrder(const Uint8 *data, int dataLength, Uint32 versionMinor);
	MessageOrder(Uint32 recepientsMask, Uint32 messageOrderType, const char *text);
	virtual ~MessageOrder(void);

	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength, Uint32 versionMinor);
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

//! A voice message
class OrderVoiceData:public MiscOrder
{
public:
	OrderVoiceData(const Uint8 *data, int dataLength, Uint32 versionMinor);
	OrderVoiceData(Uint32 recepientsMask, size_t framesDatasLength, Uint8 frameCount, const Uint8 *framesDatas);
	virtual ~OrderVoiceData(void);

	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength, Uint32 versionMinor);
	int getDataLength(void) { return framesDatasLength+5; }
	int getStrippedDataLength(void) { return 5; }
	Uint8 getOrderType(void) { return ORDER_VOICE_DATA; }
	Uint8 *getFramesData(void) { return data+5; }

	Uint32 recepientsMask;
	size_t framesDatasLength;
	Uint8 frameCount;
	Uint8 *data;
};

class SetAllianceOrder:public MiscOrder
{
public:
	SetAllianceOrder(const Uint8 *data, int dataLength, Uint32 versionMinor);
	SetAllianceOrder(Uint32 teamNumber, Uint32 alliedMask, Uint32 enemyMask, Uint32 visionExchangeMask, Uint32 visionFoodMask, Uint32 visionOtherMask);
	virtual ~SetAllianceOrder(void) { }

	Uint8 getOrderType(void) { return ORDER_SET_ALLIANCE; }
	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength, Uint32 versionMinor);
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
	MapMarkOrder(const Uint8 *data, int dataLength, Uint32 versionMinor);
	MapMarkOrder(Uint32 teamNumber, Sint32 x, Sint32 y);
	virtual ~MapMarkOrder(void) { }
	
	Uint8 getOrderType(void) { return ORDER_MAP_MARK; }
	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength, Uint32 versionMinor);
	int getDataLength(void) { return 12; }

	Uint32 teamNumber;
	Sint32 x;
	Sint32 y;

private:
	Uint8 data[12];
};

// Net orders

class PauseGameOrder:public MiscOrder
{
public:
	PauseGameOrder(const Uint8 *data, int dataLength, Uint32 versionMinor);
	PauseGameOrder(bool startPause);
	virtual ~PauseGameOrder(void) { }

	Uint8 getOrderType(void) { return ORDER_PAUSE_GAME; }
	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength, Uint32 versionMinor);
	int getDataLength(void) { return 1; }

	bool pause;
	
private:
	Uint8 data[1];
};

class PlayerQuitsGameOrder:public MiscOrder
{
public:
	PlayerQuitsGameOrder(const Uint8 *data, int dataLength, Uint32 versionMinor);
	PlayerQuitsGameOrder(Sint32 player);
	virtual ~PlayerQuitsGameOrder(void) { }

	Uint8 getOrderType(void) { return ORDER_PLAYER_QUIT_GAME; }
	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength, Uint32 versionMinor);
	int getDataLength(void) { return 4; }
	
	Sint32 player;
	
private:
	Uint8 data[4];
};

#endif
 
