// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <assert.h>

#include "NetConsts.h"
#include "Ressource.h"
#include "UnitConsts.h"
#include "BitArray.h"
#include <memory>
#include <optional>

class Map;

// === Order-protocol sentinels (cross-slice) ===

//! "Checksum not yet set" sentinel for Order::gameCheckSum. The Order is
//! sent before the receiving end has computed its post-tick checksum, so
//! this value means "skip the cross-check this tick". See Order.cpp:9,
//! MultiplayerGame.cpp:522, NetEngine.cpp:158, 211, 217-219.
static constexpr Uint32 ORDER_CHECKSUM_NONE = static_cast<Uint32>(-1);

//! "Sender unset" sentinel for Order::sender (set by NetGame::getOrder()
//! once the wire data has been mapped to a player number). See Order.cpp:8.
static constexpr int ORDER_SENDER_NONE = -1;

//! Length, in bytes, of the big-endian length prefix that precedes every
//! framed network message (TCP and UDP alike). See
//! NetConnectionThread.cpp:111-115, 182, 192-194; NetBroadcaster.cpp:53-55;
//! NetBroadcastListener.cpp:38.
static constexpr int NET_FRAME_LENGTH_PREFIX_BYTES = 2;

//! Maximum chat-message text length (including NUL terminator) used by
//! MessageOrder when validating wire-side text payloads, and matched by
//! the MultiplayerGameScreen TextInput widget's max-length.
//! See OrderMisc.cpp:37, 73; MultiplayerGameScreen.cpp:115.
static constexpr int ORDER_TEXT_MESSAGE_MAX_LEN = 256;

//! Maximum width or height (in tiles) for the bounding box of an
//! area-alteration brush stroke encoded by OrderAlterateArea.
//! See OrderModify.cpp:302-303, 350-351.
static constexpr int ORDER_AREA_BRUSH_MAX_SIDE = 512;

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
	static std::shared_ptr<Order> getOrder(const Uint8 *netData, int netDataLength, Uint32 versionMinor);

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
//!
//! Wire encoding: Sint32 teamNumber || Sint32 posX || Sint32 posY || Sint32
//! typeNum || Sint32 unitWorking || Sint32 unitWorkingFuture || [Sint32
//! flagRadius (versionMinor >= 78)].
//!
//! C++: OrderCreate (Order.h:63-68) — carries both `unitWorking` (workers
//! during construction) and `unitWorkingFuture` (workers restored once
//! construction completes).
class OrderCreate:public Order
{
public:
	OrderCreate();
	static std::optional<std::shared_ptr<OrderCreate>> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);
	OrderCreate(Sint32 teamNumber, Sint32 posX, Sint32 posY, Sint32 typeNum, Sint32 unitWorking, Sint32 unitWorkingFuture, Sint32 flagRadius=-1);
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
//!
//! Wire encoding: Uint16 gid.
class OrderDelete:public Order
{
public:
	OrderDelete();
	static std::optional<std::shared_ptr<OrderDelete>> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);
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
//!
//! Wire encoding: Uint16 gid.
class OrderCancelDelete:public Order
{
public:
	OrderCancelDelete();
	static std::optional<std::shared_ptr<OrderCancelDelete>> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);
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
//!
//! Wire encoding: Uint16 gid || Uint32 unitWorking || Uint32 unitWorkingFuture.
class OrderConstruction:public Order
{
public:
	OrderConstruction();
	static std::optional<std::shared_ptr<OrderConstruction>> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);
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
//!
//! Wire encoding: Uint16 gid || Uint32 unitWorking.
class OrderCancelConstruction:public Order
{
public:
	OrderCancelConstruction();
	static std::optional<std::shared_ptr<OrderCancelConstruction>> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);
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
//!
//! Wire encoding: Uint16 gid || Sint32 priority.
class OrderChangePriority:public Order
{
public:
	OrderChangePriority();
	static std::optional<std::shared_ptr<OrderChangePriority>> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);
	OrderChangePriority(Uint16 gid, Sint32 priority);
	virtual ~OrderChangePriority(void) {}
	Uint8 getOrderType(void) { return ORDER_CHANGE_PRIORITY; }
	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength, Uint32 versionMinor);
	int getDataLength(void) { return 6; }

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
//!
//! Wire encoding: Uint16 gid || Uint16 numberRequested.
class OrderModifyBuilding:public OrderModify
{
public:
	OrderModifyBuilding();
	static std::optional<std::shared_ptr<OrderModifyBuilding>> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);
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

//! Change the exchange resource masks for a building.
//!
//! Wire encoding: Uint16 gid || Uint32 receiveRessourceMask || Uint32 sendRessourceMask.
class OrderModifyExchange:public OrderModify
{
public:
	OrderModifyExchange();
	static std::optional<std::shared_ptr<OrderModifyExchange>> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);
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
	OrderModifySwarm();
	static std::optional<std::shared_ptr<OrderModifySwarm>> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);
	OrderModifySwarm(Uint16 gid, Sint32 ratio[NB_UNIT_TYPE]);
	virtual ~OrderModifySwarm(void) {}

	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength, Uint32 versionMinor);
	int getDataLength(void) { return 2+4*NB_UNIT_TYPE; }
	Uint8 getOrderType(void) { return ORDER_MODIFY_SWARM; }

	Uint16 gid;
	Sint32 ratio[NB_UNIT_TYPE];

protected:
	//! Wire encoding buffer: Uint16 gid || Sint32 ratio[NB_UNIT_TYPE].
	//! Size must track getDataLength() — keep both as 2+4*NB_UNIT_TYPE.
	Uint8 data[2+4*NB_UNIT_TYPE];
};

class OrderModifyFlag:public OrderModify
{
public:
	OrderModifyFlag();
	static std::optional<std::shared_ptr<OrderModifyFlag>> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);
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
	OrderModifyClearingFlag();
	static std::optional<std::shared_ptr<OrderModifyClearingFlag>> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);
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
	OrderModifyMinLevelToFlag();
	static std::optional<std::shared_ptr<OrderModifyMinLevelToFlag>> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);
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
	OrderMoveFlag();
	static std::optional<std::shared_ptr<OrderMoveFlag>> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);
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
	OrderAlterateArea();
	static std::optional<std::shared_ptr<OrderAlterateArea>> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);
	#ifndef YOG_SERVER_ONLY
	OrderAlterateArea(Uint8 teamNumber, Uint8 type, BrushAccumulator *acc, const Map* map);
	#endif
	virtual ~OrderAlterateArea(void);
	
	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength, Uint32 versionMinor);
	int getDataLength(void);
	Uint8 getOrderType(void) { return ORDER_ALTERATE_FORBIDDEN; }
	
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
	static std::optional<std::shared_ptr<OrderAlterateForbidden>> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor)
	{
		if (auto area = OrderAlterateArea::deserialize(data, dataLength, versionMinor))
			return std::static_pointer_cast<OrderAlterateForbidden>(area.value());
		return std::nullopt;
	}
	#ifndef YOG_SERVER_ONLY
	OrderAlterateForbidden(Uint8 teamNumber, Uint8 type, BrushAccumulator *acc, const Map* map) : OrderAlterateArea(teamNumber, type, acc, map) { }
	#endif
	
	Uint8 getOrderType(void) { return ORDER_ALTERATE_FORBIDDEN; }
};

class OrderAlterateGuardArea:public OrderAlterateArea
{
public:
	static std::optional<std::shared_ptr<OrderAlterateGuardArea>> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor)
	{
		if (auto area = OrderAlterateArea::deserialize(data, dataLength, versionMinor))
			return std::static_pointer_cast<OrderAlterateGuardArea>(area.value());
		return std::nullopt;
	}
	#ifndef YOG_SERVER_ONLY
	OrderAlterateGuardArea(Uint8 teamNumber, Uint8 type, BrushAccumulator *acc, const Map* map) : OrderAlterateArea(teamNumber, type, acc, map) { }
	#endif
	
	Uint8 getOrderType(void) { return ORDER_ALTERATE_GUARD_AREA; }
};

class OrderAlterateClearArea:public OrderAlterateArea
{
public:
	static std::optional<std::shared_ptr<OrderAlterateClearArea>> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor)
	{
		if (auto area = OrderAlterateArea::deserialize(data, dataLength, versionMinor))
			return std::static_pointer_cast<OrderAlterateClearArea>(area.value());
		return std::nullopt;
	}
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

//! A chat message.
//!
//! Wire encoding: Uint32 recepientsMask || Uint32 messageOrderType || Uint8
//! textLength || char text[textLength] || NUL terminator.
class MessageOrder:public MiscOrder
{
public:
	MessageOrder();
	static std::optional<std::shared_ptr<MessageOrder>> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);
	MessageOrder(Uint32 recepientsMask, Uint32 messageOrderType, const char * text);
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

//! A voice message.
//!
//! Wire encoding: Uint32 recepientsMask || Uint8 frameCount || Uint8 frames[frameCount].
class OrderVoiceData:public MiscOrder
{
public:
	OrderVoiceData();
	static std::optional<std::shared_ptr<OrderVoiceData>> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);
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
	SetAllianceOrder();
	static std::optional<std::shared_ptr<SetAllianceOrder>> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);
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
	MapMarkOrder();
	static std::optional<std::shared_ptr<MapMarkOrder>> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);
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
	PauseGameOrder();
	static std::optional<std::shared_ptr<PauseGameOrder>> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);
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
	PlayerQuitsGameOrder();
	static std::optional<std::shared_ptr<PlayerQuitsGameOrder>> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);
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


class AdjustLatency:public MiscOrder
{
public:
	AdjustLatency();
	static std::optional<std::shared_ptr<AdjustLatency>> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);
	AdjustLatency(Uint16 latencyAdjustment);
	virtual ~AdjustLatency(void) { }

	Uint8 getOrderType(void) { return ORDER_ADJUST_LATENCY; }
	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength, Uint32 versionMinor);
	int getDataLength(void) { return 2; }
	
	Uint16 latencyAdjustment;
	
private:
	Uint8 data[2];
};
 
