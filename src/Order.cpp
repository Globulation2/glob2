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
		return OrderCreate::deserialize(netData+1, netDataLength-1, versionMinor);
	case ORDER_DELETE:
		return OrderDelete::deserialize(netData+1, netDataLength-1, versionMinor);
	case ORDER_CANCEL_DELETE:
		return OrderCancelDelete::deserialize(netData+1, netDataLength-1, versionMinor);
	case ORDER_CONSTRUCTION:
		return OrderConstruction::deserialize(netData+1, netDataLength-1, versionMinor);
	case ORDER_CANCEL_CONSTRUCTION:
		return OrderCancelConstruction::deserialize(netData+1, netDataLength-1, versionMinor);
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
		return OrderChangePriority::deserialize(netData+1, netDataLength-1, versionMinor);
	case ORDER_ALTERATE_FORBIDDEN:
		return OrderAlterateForbidden::deserialize(netData+1, netDataLength-1, versionMinor);
	case ORDER_ALTERATE_GUARD_AREA:
		return OrderAlterateGuardArea::deserialize(netData+1, netDataLength-1, versionMinor);
	case ORDER_ALTERATE_CLEAR_AREA:
		return OrderAlterateClearArea::deserialize(netData+1, netDataLength-1, versionMinor);
	case ORDER_NULL:
		return std::shared_ptr<Order>(new NullOrder());
	case ORDER_TEXT_MESSAGE:
		return MessageOrder::deserialize(netData+1, netDataLength-1, versionMinor);
	case ORDER_VOICE_DATA:
		return OrderVoiceData::deserialize(netData+1, netDataLength-1, versionMinor);
	case ORDER_SET_ALLIANCE:
		return SetAllianceOrder::deserialize(netData+1, netDataLength-1, versionMinor);
	case ORDER_MAP_MARK:
		return MapMarkOrder::deserialize(netData+1, netDataLength-1, versionMinor);
	case ORDER_PAUSE_GAME:
		return PauseGameOrder::deserialize(netData+1, netDataLength-1, versionMinor);
	case ORDER_PLAYER_QUIT_GAME :
		return PlayerQuitsGameOrder::deserialize(netData+1, netDataLength-1, versionMinor);
	case ORDER_ADJUST_LATENCY :
		return AdjustLatency::deserialize(netData+1, netDataLength-1, versionMinor);
	default:
		printf("Bad packet recieved in Order.cpp (%d)\n", netData[0]);
	}
	return std::shared_ptr<Order>();
}
