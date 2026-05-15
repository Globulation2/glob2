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
class OrderCreate:public Order
{
public:
	OrderCreate() = default;
	OrderCreate(Sint32 teamNumber, Sint32 posX, Sint32 posY, Sint32 typeNum, Sint32 unitWorking, Sint32 unitWorkingFuture, Sint32 flagRadius=-1);
	virtual ~OrderCreate(void) {}

	//! See OrderModifyBuilding::deserialize.
	static std::shared_ptr<OrderCreate> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);

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
	OrderDelete() = default;
	OrderDelete(Uint16 gid);
	virtual ~OrderDelete(void) {}

	//! See OrderModifyBuilding::deserialize.
	static std::shared_ptr<OrderDelete> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);

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
	OrderCancelDelete() = default;
	OrderCancelDelete(Uint16 gid);
	virtual ~OrderCancelDelete(void) {}

	//! See OrderModifyBuilding::deserialize.
	static std::shared_ptr<OrderCancelDelete> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);

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
	OrderConstruction() = default;
	OrderConstruction(Uint16 gid, Uint32 unitWorking, Uint32 unitWorkingFuture);
	virtual ~OrderConstruction(void) {}

	//! See OrderModifyBuilding::deserialize.
	static std::shared_ptr<OrderConstruction> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);

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
	OrderCancelConstruction() = default;
	OrderCancelConstruction(Uint16 gid, Uint32 unitWorking);
	virtual ~OrderCancelConstruction(void) {}

	//! See OrderModifyBuilding::deserialize.
	static std::shared_ptr<OrderCancelConstruction> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);

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
	OrderChangePriority() = default;
	OrderChangePriority(Uint16 gid, Sint32 priority);
	virtual ~OrderChangePriority(void) {}

	//! See OrderModifyBuilding::deserialize.
	static std::shared_ptr<OrderChangePriority> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);

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
class OrderModifyBuilding:public OrderModify
{
public:
	OrderModifyBuilding() = default;
	OrderModifyBuilding(Uint16 gid, Uint16 numberRequested);
	virtual ~OrderModifyBuilding(void) {}

	//! Decode a wire payload (no leading order-type byte). Returns nullptr on
	//! malformed input; Order::getOrder treats that as a dropped order.
	static std::shared_ptr<OrderModifyBuilding> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);

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
	OrderModifyExchange() = default;
	OrderModifyExchange(Uint16 gid, Uint32 receiveRessourceMask, Uint32 sendRessourceMask);
	virtual ~OrderModifyExchange(void) {}

	//! See OrderModifyBuilding::deserialize.
	static std::shared_ptr<OrderModifyExchange> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);

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
	OrderModifySwarm() = default;
	OrderModifySwarm(Uint16 gid, Sint32 ratio[NB_UNIT_TYPE]);
	virtual ~OrderModifySwarm(void) {}

	//! See OrderModifyBuilding::deserialize.
	static std::shared_ptr<OrderModifySwarm> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);

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
	OrderModifyFlag() = default;
	OrderModifyFlag(Uint16 gid, Sint32 range);
	virtual ~OrderModifyFlag(void) {}

	//! See OrderModifyBuilding::deserialize.
	static std::shared_ptr<OrderModifyFlag> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);

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
	OrderModifyClearingFlag() = default;
	OrderModifyClearingFlag(Uint16 gid, bool clearingRessources[BASIC_COUNT]);
	virtual ~OrderModifyClearingFlag(void);

	//! See OrderModifyBuilding::deserialize.
	static std::shared_ptr<OrderModifyClearingFlag> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);

	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength, Uint32 versionMinor);
	int getDataLength(void) { return 2+BASIC_COUNT; }
	Uint8 getOrderType(void) { return ORDER_MODIFY_CLEARING_FLAG; }

	Uint16 gid;
	bool clearingRessources[BASIC_COUNT];

protected:
	Uint8 *data = nullptr;
};

class OrderModifyMinLevelToFlag:public OrderModify
{
public:
	OrderModifyMinLevelToFlag() = default;
	OrderModifyMinLevelToFlag(Uint16 gid, Uint16 minLevelToFlag);
	virtual ~OrderModifyMinLevelToFlag(void);

	//! See OrderModifyBuilding::deserialize.
	static std::shared_ptr<OrderModifyMinLevelToFlag> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);

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
	OrderMoveFlag() = default;
	OrderMoveFlag(Uint16 gid, Sint32 x, Sint32 y, bool drop);
	virtual ~OrderMoveFlag(void) {}

	//! See OrderModifyBuilding::deserialize.
	static std::shared_ptr<OrderMoveFlag> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);

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

//! Number of bytes in the fixed-width header that precedes the variable-length
//! BitArray mask payload in every OrderAlterateArea wire encoding:
//! teamNumber(1) + type(1) + centerX/Y(2*2) + minX/Y(2*2) + maxX/Y(2*2) = 14.
static constexpr int ALTERATE_AREA_HEADER_BYTES = 14;

class OrderAlterateArea:public OrderModify
{
public:
	OrderAlterateArea() = default;
	#ifndef YOG_SERVER_ONLY
	OrderAlterateArea(Uint8 teamNumber, Uint8 type, BrushAccumulator *acc, const Map* map);
	#endif
	virtual ~OrderAlterateArea(void);

	Uint8 *getData(void);

	//! Parse the wire format for an OrderAlterate{Forbidden,GuardArea,ClearArea}
	//! packet. Layout: 14-byte fixed header
	//! (teamNumber: Uint8, type: Uint8, centerX/Y: Sint16, minX/Y: Sint16,
	//! maxX/Y: Sint16, all big-endian)
	//! followed by ceil((maxX-minX) * (maxY-minY) / 8) bitmap bytes.
	//!
	//! Returns false (without mutating the bitmap) on any of:
	//!   - dataLength < ALTERATE_AREA_HEADER_BYTES
	//!   - maxX < minX or maxY < minY (negative-side dimensions)
	//!   - maxX-minX or maxY-minY > ORDER_AREA_BRUSH_MAX_SIDE
	//!   - dataLength does not equal header + expected bitmap byte count
	//!
	//! These rejections are required because the source `data` buffer comes
	//! from network or replay traffic and its length is the only ground
	//! truth for the bitmap-payload size — the header-declared dimensions
	//! cannot be trusted, and BitArray::deserialize does no bound check.
	//! See BH-195.
	bool setData(const Uint8 *data, int dataLength, Uint32 versionMinor);
	int getDataLength(void);

	//! Returns the expected number of bitmap payload bytes for the given
	//! brush bounding box, or std::nullopt if the box is invalid (negative
	//! side, or side > ORDER_AREA_BRUSH_MAX_SIDE). Pure helper, separated
	//! from setData so the malformed-packet rejections can be unit-tested
	//! without a full wire-buffer round-trip.
	static std::optional<size_t> expectedBitmapBytes(Sint16 minX, Sint16 minY,
	                                                 Sint16 maxX, Sint16 maxY);

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
	Uint8 *_data = nullptr;
};

class OrderAlterateForbidden:public OrderAlterateArea
{
public:
	OrderAlterateForbidden() = default;
	#ifndef YOG_SERVER_ONLY
	OrderAlterateForbidden(Uint8 teamNumber, Uint8 type, BrushAccumulator *acc, const Map* map) : OrderAlterateArea(teamNumber, type, acc, map) { }
	#endif

	//! See OrderModifyBuilding::deserialize.
	static std::shared_ptr<OrderAlterateForbidden> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);

	Uint8 getOrderType(void) { return ORDER_ALTERATE_FORBIDDEN; }
};

class OrderAlterateGuardArea:public OrderAlterateArea
{
public:
	OrderAlterateGuardArea() = default;
	#ifndef YOG_SERVER_ONLY
	OrderAlterateGuardArea(Uint8 teamNumber, Uint8 type, BrushAccumulator *acc, const Map* map) : OrderAlterateArea(teamNumber, type, acc, map) { }
	#endif

	//! See OrderModifyBuilding::deserialize.
	static std::shared_ptr<OrderAlterateGuardArea> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);

	Uint8 getOrderType(void) { return ORDER_ALTERATE_GUARD_AREA; }
};

class OrderAlterateClearArea:public OrderAlterateArea
{
public:
	OrderAlterateClearArea() = default;
	#ifndef YOG_SERVER_ONLY
	OrderAlterateClearArea(Uint8 teamNumber, Uint8 type, BrushAccumulator *acc, const Map* map) : OrderAlterateArea(teamNumber, type, acc, map) { }
	#endif

	//! See OrderModifyBuilding::deserialize.
	static std::shared_ptr<OrderAlterateClearArea> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);

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
	MessageOrder() = default;
	MessageOrder(Uint32 recepientsMask, Uint32 messageOrderType, const char * text);
	virtual ~MessageOrder(void);

	//! See OrderModifyBuilding::deserialize.
	static std::shared_ptr<MessageOrder> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);

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
	Uint8 *data = nullptr;
	int length = 0;
};

//! A voice message
class OrderVoiceData:public MiscOrder
{
public:
	OrderVoiceData() = default;
	OrderVoiceData(Uint32 recepientsMask, size_t framesDatasLength, Uint8 frameCount, const Uint8 *framesDatas);
	virtual ~OrderVoiceData(void);

	//! See OrderModifyBuilding::deserialize.
	static std::shared_ptr<OrderVoiceData> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);

	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength, Uint32 versionMinor);
	int getDataLength(void) { return framesDatasLength+5; }
	int getStrippedDataLength(void) { return 5; }
	Uint8 getOrderType(void) { return ORDER_VOICE_DATA; }
	Uint8 *getFramesData(void) { return data+5; }

	Uint32 recepientsMask;
	size_t framesDatasLength = 0;
	Uint8 frameCount = 0;
	Uint8 *data = nullptr;
};

class SetAllianceOrder:public MiscOrder
{
public:
	SetAllianceOrder() = default;
	SetAllianceOrder(Uint32 teamNumber, Uint32 alliedMask, Uint32 enemyMask, Uint32 visionExchangeMask, Uint32 visionFoodMask, Uint32 visionOtherMask);
	virtual ~SetAllianceOrder(void) { }

	//! See OrderModifyBuilding::deserialize.
	static std::shared_ptr<SetAllianceOrder> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);

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
	MapMarkOrder() = default;
	MapMarkOrder(Uint32 teamNumber, Sint32 x, Sint32 y);
	virtual ~MapMarkOrder(void) { }

	//! See OrderModifyBuilding::deserialize.
	static std::shared_ptr<MapMarkOrder> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);

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
	PauseGameOrder() = default;
	PauseGameOrder(bool startPause);
	virtual ~PauseGameOrder(void) { }

	//! See OrderModifyBuilding::deserialize.
	static std::shared_ptr<PauseGameOrder> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);

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
	PlayerQuitsGameOrder() = default;
	PlayerQuitsGameOrder(Sint32 player);
	virtual ~PlayerQuitsGameOrder(void) { }

	//! See OrderModifyBuilding::deserialize.
	static std::shared_ptr<PlayerQuitsGameOrder> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);

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
	AdjustLatency() = default;
	AdjustLatency(Uint16 latencyAdjustment);
	virtual ~AdjustLatency(void) { }

	//! See OrderModifyBuilding::deserialize.
	static std::shared_ptr<AdjustLatency> deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor);

	Uint8 getOrderType(void) { return ORDER_ADJUST_LATENCY; }
	Uint8 *getData(void);
	bool setData(const Uint8 *data, int dataLength, Uint32 versionMinor);
	int getDataLength(void) { return 2; }
	
	Uint16 latencyAdjustment;
	
private:
	Uint8 data[2];
};
 
