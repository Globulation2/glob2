/*
 * Globulation 2 player support
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#ifndef __ORDER_H
#define __ORDER_H

#include "GAG.h"
#include "Building.h"
#include "BuildingType.h"
#include "NetConsts.h"

class Order
{
 public:
	virtual ~Order(void) { }
	virtual Uint8 getOrderType(void)=0;

	static Order *getOrder(const char *netData, int netDataLength);

	virtual char *getData(void)=0;
	virtual bool setData(const char *data, int dataLength)=0;
	virtual int getDataLength(void)=0;
	
	virtual Sint32 checkSum()=0;
};


// Creation orders
// NOTE : we pass a BuildingType but it's a int and it's un typenum so we need to CLEAN THIS !!!
class OrderCreate:public Order
{
 public:
	OrderCreate(const char *data, int dataLength);
	OrderCreate(Uint32 team, Sint32 posX, Sint32 posY, BuildingType::BuildingTypeNumber typeNumber);
	virtual ~OrderCreate(void) { }
	Uint8 getOrderType(void) { return ORDER_CREATE; }
	char *getData(void);
	bool setData(const char *data, int dataLength);
	int getDataLength(void) { return 16; }
	Sint32 checkSum() { return ORDER_CREATE; }

	Uint32 team;
	Sint32 posX;
	Sint32 posY;
	BuildingType::BuildingTypeNumber typeNumber;

 private:
	char data[16];
};


// Deletion orders

class OrderDelete:public Order
{
 public:
	OrderDelete(const char *data, int dataLength);
	OrderDelete(Sint32 UID);
	virtual ~OrderDelete(void) { }
	Uint8 getOrderType(void) { return ORDER_DELETE; }
	char *getData(void);
	bool setData(const char *data, int dataLength);
	int getDataLength(void) { return 4; }
	Sint32 checkSum() { return ORDER_DELETE; }

	Sint32 UID;

protected:
	char data[4];
};

class OrderUpgrade:public Order
{
 public:
	OrderUpgrade(const char *data, int dataLength);
	OrderUpgrade(Sint32 UID);
	virtual ~OrderUpgrade(void) { }
	Uint8 getOrderType(void) { return ORDER_UPGRADE; }
	char *getData(void);
	bool setData(const char *data, int dataLength);
	int getDataLength(void) { return 4; }
	Sint32 checkSum() { return ORDER_UPGRADE; }

	Sint32 UID;

protected:
	char data[4];
};


// Modification orders

class OrderModify:public Order
{
 public:
	virtual ~OrderModify(void) { }

	Uint8 getOrderType(void) { return 40; }
 protected:
	char *data;
	int length;
};

class OrderModifyUnits:public OrderModify
{
 public:
	OrderModifyUnits(const char *data, int dataLength);
	OrderModifyUnits(Sint32 *UID, Sint32 *trigHP, Sint32 *trigHungry, int length);
	virtual ~OrderModifyUnits(void);
	
	char *getData(void);
	bool setData(const char *data, int dataLength);
	int getDataLength(void) { return length*12; }
	int getNumberOfUnit(void) { return length; }
	Uint8 getOrderType(void) { return ORDER_MODIFY_UNIT; }
	Sint32 checkSum() { return ORDER_MODIFY_UNIT; }

	Sint32 *UID;
	Sint32 *trigHP;
	Sint32 *trigHungry;
};

class OrderModifyBuildings:public OrderModify
{
 public:
	OrderModifyBuildings(const char *data, int dataLength);
	OrderModifyBuildings(Sint32 *UID, Sint32 *numberRequested, int length);
	virtual ~OrderModifyBuildings(void);

	char *getData(void);
	bool setData(const char *data, int dataLength);
	int getDataLength(void) { return length*8; }
	int getNumberOfBuilding(void) { return length; }
	Uint8 getOrderType(void) { return ORDER_MODIFY_BUILDING; }
	Sint32 checkSum() { return ORDER_MODIFY_BUILDING; }

	Sint32 *UID;
	Sint32 *numberRequested;
};

class OrderModifySwarms:public OrderModify
{
 public:
	OrderModifySwarms(const char *data, int dataLength);
	OrderModifySwarms(Sint32 *UID, Sint32 ratio[][UnitType::NB_UNIT_TYPE], int length);
	virtual ~OrderModifySwarms(void);

	char *getData(void);
	bool setData(const char *data, int dataLength);
	int getDataLength(void) { assert(UnitType::NB_UNIT_TYPE==3); return length*16; }
	int getNumberOfSwarm(void) { return length; }

	Sint32 *UID;
	Sint32 *ratio;

	Uint8 getOrderType(void) { return ORDER_MODIFY_SWARM; }
	Sint32 checkSum() { return ORDER_MODIFY_SWARM; }
};


class OrderModifyFlags:public OrderModify
{
 public:
	OrderModifyFlags(const char *data, int dataLength);
	OrderModifyFlags(Sint32 *UID, Sint32 *range, int length);
	virtual ~OrderModifyFlags(void);

	char *getData(void);
	bool setData(const char *data, int dataLength);
	int getDataLength(void) { return length*8; }
	int getNumberOfBuilding(void) { return length; }

	Sint32 *UID;
	Sint32 *range;

	Uint8 getOrderType(void) { return ORDER_MODIFY_FLAG; }
	Sint32 checkSum() { return ORDER_MODIFY_FLAG; }
};


// Misc orders

class MiscOrder:public Order
{
 public:
	virtual ~MiscOrder(void) { }

	Uint8 getOrderType(void) { return 50; }
};

class NullOrder:public MiscOrder
{
 public:
	
	virtual ~NullOrder(void) { }

	char *getData(void) { return NULL; }
	bool setData(const char *data, int dataLength) { return (dataLength==0);}
	int getDataLength(void) { return 0; }
	Uint8 getOrderType(void) { return ORDER_NULL; }
	Sint32 checkSum() { return ORDER_NULL; }
	
};

class MessageOrder:public MiscOrder
{
 public:
	MessageOrder(const char *data, int dataLength);
	MessageOrder(const char *text, Uint32 recepientsMask);
	virtual ~MessageOrder(void);

	char *getData(void);
	bool setData(const char *data, int dataLength);
	int getDataLength(void) { return length; }
	char *getText(void) { return (data+4); }
	Uint8 getOrderType(void) { return ORDER_TEXT_MESSAGE; }
	Sint32 checkSum() { return ORDER_TEXT_MESSAGE; }

	Uint32 recepientsMask;

 protected:
	char *data;
	int length;
};

class SetAllianceOrder:public MiscOrder
{
 public:
	SetAllianceOrder(const char *data, int dataLength);
	SetAllianceOrder(Uint32 allianceMask);
	virtual ~SetAllianceOrder(void) { }

	Uint8 getOrderType(void) { return ORDER_SET_ALLIANCE; }
	char *getData(void);
	bool setData(const char *data, int dataLength);
	int getDataLength(void) { return 4; }
	Sint32 checkSum() { return ORDER_SET_ALLIANCE; }

	Uint32 allianceMask;

 protected:
	char data[4];
};

class SubmitCheckSumOrder:public MiscOrder
{
 public:
	SubmitCheckSumOrder(const char *data, int dataLength);
	SubmitCheckSumOrder(Sint32 checkSumValue);
	virtual ~SubmitCheckSumOrder(void) { }

	Uint8 getOrderType(void) { return ORDER_SUBMIT_CHECK_SUM; }
	char *getData(void);
	bool setData(const char *data, int dataLength);
	int getDataLength(void) { return 4; }
	Sint32 checkSum() { return ORDER_SUBMIT_CHECK_SUM; }

	Sint32 checkSumValue;

 private:
	char data[4];
};

// Net orders

class WaitingForPlayerOrder:public MiscOrder
{
 public:
	WaitingForPlayerOrder(const char *data, int dataLength);
	WaitingForPlayerOrder(Uint32 maskAwayPlayer);
	virtual ~WaitingForPlayerOrder(void) { }

	Uint8 getOrderType(void) { return ORDER_WAITING_FOR_PLAYER; }
	char *getData(void);
	bool setData(const char *data, int dataLength);
	int getDataLength(void) { return 4; }
	Sint32 checkSum() { return ORDER_WAITING_FOR_PLAYER; }
	
	Uint32 maskAwayPlayer;
	
 private:
	char data[4];
};

class DroppingPlayerOrder:public MiscOrder
{
 public:
	DroppingPlayerOrder(const char *data, int dataLength);
	DroppingPlayerOrder(Uint32 stayingPlayersMask, Sint32 dropState);
	virtual ~DroppingPlayerOrder(void) { }

	Uint8 getOrderType(void) { return ORDER_DROPPING_PLAYER; }
	char *getData(void);
	bool setData(const char *data, int dataLength);
	int getDataLength(void) { return 8; }
	Sint32 checkSum() { return ORDER_DROPPING_PLAYER; }
	
	Uint32 stayingPlayersMask;
	Sint32 dropState;
	
 private:
	char data[8];
};

class RequestingDeadAwayOrder:public MiscOrder
{
 public:
	RequestingDeadAwayOrder(const char *data, int dataLength);
	RequestingDeadAwayOrder(Sint32 player, Sint32 missingStep, Sint32 lastAviableStep);
	virtual ~RequestingDeadAwayOrder(void) { }

	Uint8 getOrderType(void) { return ORDER_REQUESTING_AWAY; }
	char *getData(void);
	bool setData(const char *data, int dataLength);
	int getDataLength(void) { return 12; }
	Sint32 checkSum() { return ORDER_REQUESTING_AWAY; }
	
	Sint32 player;
	Sint32 missingStep;
	Sint32 lastAviableStep;
	
 private:
	char data[12];
};

class NoMoreOrdersAviable:public MiscOrder
{
 public:
	NoMoreOrdersAviable(const char *data, int dataLength);
	NoMoreOrdersAviable(Sint32 player, Sint32 lastAviableStep);
	virtual ~NoMoreOrdersAviable(void) { }

	Uint8 getOrderType(void) { return ORDER_NO_MORE_ORDER_AVIABLES; }
	char *getData(void);
	bool setData(const char *data, int dataLength);
	int getDataLength(void) { return 8; }
	Sint32 checkSum() { return ORDER_NO_MORE_ORDER_AVIABLES; }
	
	Sint32 player;
	Sint32 lastAviableStep;
	
 private:
	char data[8];
};

// Usefull function for marshalling

inline void addSint32(const char *data, Sint32 val, int pos)
{
	*((Sint32 *)(((Uint8 *)data)+pos))=SDL_SwapBE32(val);
}

inline Sint32 getSint32(const char *data, int pos)
{
	return (Sint32)SDL_SwapBE32( *( (Sint32 *) (((Uint8 *)data) +pos) ) );
}

inline void addUint32(const char *data, Uint32 val, int pos)
{
	*((Uint32 *)(((Uint8 *)data)+pos))=SDL_SwapBE32(val);
}

inline Uint32 getUint32(const char *data, int pos)
{
	return (Uint32)SDL_SwapBE32( *( (Uint32 *) (((Uint8 *)data) +pos) ) );
}

#endif
 
