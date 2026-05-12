// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Order.h"

Order::Order(void)
{
	sender=ORDER_SENDER_NONE;
	gameCheckSum=ORDER_CHECKSUM_NONE;
}

std::shared_ptr<Order> Order::getOrder(const Uint8 *netData, int netDataLength, Uint32 versionMinor)
{
	if (netDataLength<1 || netData==NULL)
		return std::shared_ptr<Order>();

	switch (netData[0])
	{
	case ORDER_CREATE:
		return std::shared_ptr<Order>(new OrderCreate(netData+1, netDataLength-1, versionMinor));
	case ORDER_DELETE:
		return std::shared_ptr<Order>(new OrderDelete(netData+1, netDataLength-1, versionMinor));
	case ORDER_CANCEL_DELETE:
		return std::shared_ptr<Order>(new OrderCancelDelete(netData+1, netDataLength-1, versionMinor));
	case ORDER_CONSTRUCTION:
		return std::shared_ptr<Order>(new OrderConstruction(netData+1, netDataLength-1, versionMinor));
	case ORDER_CANCEL_CONSTRUCTION:
		return std::shared_ptr<Order>(new OrderCancelConstruction(netData+1, netDataLength-1, versionMinor));
	case ORDER_MODIFY_BUILDING:
		return OrderModifyBuilding::deserialize(netData+1, netDataLength-1, versionMinor);
	case ORDER_MODIFY_EXCHANGE:
		return OrderModifyExchange::deserialize(netData+1, netDataLength-1, versionMinor);
	case ORDER_MODIFY_SWARM:
		return OrderModifySwarm::deserialize(netData+1, netDataLength-1, versionMinor);
	case ORDER_MODIFY_FLAG:
		return OrderModifyFlag::deserialize(netData+1, netDataLength-1, versionMinor);
	case ORDER_MODIFY_CLEARING_FLAG:
		return OrderModifyClearingFlag::deserialize(netData+1, netDataLength-1, versionMinor);
	case ORDER_MODIFY_MIN_LEVEL_TO_FLAG:
		return OrderModifyMinLevelToFlag::deserialize(netData+1, netDataLength-1, versionMinor);
	case ORDER_MOVE_FLAG:
		return OrderMoveFlag::deserialize(netData+1, netDataLength-1, versionMinor);
	case ORDER_CHANGE_PRIORITY:
		return std::shared_ptr<Order>(new OrderChangePriority(netData+1, netDataLength-1, versionMinor));
	case ORDER_ALTERATE_FORBIDDEN:
		return OrderAlterateForbidden::deserialize(netData+1, netDataLength-1, versionMinor);
	case ORDER_ALTERATE_GUARD_AREA:
		return OrderAlterateGuardArea::deserialize(netData+1, netDataLength-1, versionMinor);
	case ORDER_ALTERATE_CLEAR_AREA:
		return OrderAlterateClearArea::deserialize(netData+1, netDataLength-1, versionMinor);
	case ORDER_NULL:
		return std::shared_ptr<Order>(new NullOrder());
	case ORDER_TEXT_MESSAGE:
		return std::shared_ptr<Order>(new MessageOrder(netData+1, netDataLength-1, versionMinor));
	case ORDER_VOICE_DATA:
		return std::shared_ptr<Order>(new OrderVoiceData(netData+1, netDataLength-1, versionMinor));
	case ORDER_SET_ALLIANCE:
		return std::shared_ptr<Order>(new SetAllianceOrder(netData+1, netDataLength-1, versionMinor));
	case ORDER_MAP_MARK:
		return std::shared_ptr<Order>(new MapMarkOrder(netData+1, netDataLength-1, versionMinor));
	case ORDER_PAUSE_GAME:
		return std::shared_ptr<Order>(new PauseGameOrder(netData+1, netDataLength-1, versionMinor));
	case ORDER_PLAYER_QUIT_GAME :
		return std::shared_ptr<Order>(new PlayerQuitsGameOrder(netData+1, netDataLength-1, versionMinor));
	case ORDER_ADJUST_LATENCY :
		return std::shared_ptr<Order>(new AdjustLatency(netData+1, netDataLength-1, versionMinor));
	default:
		printf("Bad packet recieved in Order.cpp (%d)\n", netData[0]);
	}
	return std::shared_ptr<Order>();
}
