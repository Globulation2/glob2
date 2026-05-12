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
		if (auto o = OrderCreate::deserialize(netData+1, netDataLength-1, versionMinor))
			return o.value();
		break;
	case ORDER_DELETE:
		if (auto o = OrderDelete::deserialize(netData+1, netDataLength-1, versionMinor))
			return o.value();
		break;
	case ORDER_CANCEL_DELETE:
		if (auto o = OrderCancelDelete::deserialize(netData+1, netDataLength-1, versionMinor))
			return o.value();
		break;
	case ORDER_CONSTRUCTION:
		if (auto o = OrderConstruction::deserialize(netData+1, netDataLength-1, versionMinor))
			return o.value();
		break;
	case ORDER_CANCEL_CONSTRUCTION:
		if (auto o = OrderCancelConstruction::deserialize(netData+1, netDataLength-1, versionMinor))
			return o.value();
		break;
	case ORDER_MODIFY_BUILDING:
		if (auto o = OrderModifyBuilding::deserialize(netData+1, netDataLength-1, versionMinor))
			return o.value();
		break;
	case ORDER_MODIFY_EXCHANGE:
		if (auto o = OrderModifyExchange::deserialize(netData+1, netDataLength-1, versionMinor))
			return o.value();
		break;
	case ORDER_MODIFY_SWARM:
		if (auto o = OrderModifySwarm::deserialize(netData+1, netDataLength-1, versionMinor))
			return o.value();
		break;
	case ORDER_MODIFY_FLAG:
		if (auto o = OrderModifyFlag::deserialize(netData+1, netDataLength-1, versionMinor))
			return o.value();
		break;
	case ORDER_MODIFY_CLEARING_FLAG:
		if (auto o = OrderModifyClearingFlag::deserialize(netData+1, netDataLength-1, versionMinor))
			return o.value();
		break;
	case ORDER_MODIFY_MIN_LEVEL_TO_FLAG:
		if (auto o = OrderModifyMinLevelToFlag::deserialize(netData+1, netDataLength-1, versionMinor))
			return o.value();
		break;
	case ORDER_MOVE_FLAG:
		if (auto o = OrderMoveFlag::deserialize(netData+1, netDataLength-1, versionMinor))
			return o.value();
		break;
	case ORDER_CHANGE_PRIORITY:
		if (auto o = OrderChangePriority::deserialize(netData+1, netDataLength-1, versionMinor))
			return o.value();
		break;
	case ORDER_ALTERATE_FORBIDDEN:
		if (auto o = OrderAlterateForbidden::deserialize(netData+1, netDataLength-1, versionMinor))
			return o.value();
		break;
	case ORDER_ALTERATE_GUARD_AREA:
		if (auto o = OrderAlterateGuardArea::deserialize(netData+1, netDataLength-1, versionMinor))
			return o.value();
		break;
	case ORDER_ALTERATE_CLEAR_AREA:
		if (auto o = OrderAlterateClearArea::deserialize(netData+1, netDataLength-1, versionMinor))
			return o.value();
		break;
	case ORDER_NULL:
		return std::shared_ptr<Order>(new NullOrder());
	case ORDER_TEXT_MESSAGE:
		if (auto o = MessageOrder::deserialize(netData+1, netDataLength-1, versionMinor))
			return o.value();
		break;
	case ORDER_VOICE_DATA:
		if (auto o = OrderVoiceData::deserialize(netData+1, netDataLength-1, versionMinor))
			return o.value();
		break;
	case ORDER_SET_ALLIANCE:
		if (auto o = SetAllianceOrder::deserialize(netData+1, netDataLength-1, versionMinor))
			return o.value();
		break;
	case ORDER_MAP_MARK:
		if (auto o = MapMarkOrder::deserialize(netData+1, netDataLength-1, versionMinor))
			return o.value();
		break;
	case ORDER_PAUSE_GAME:
		if (auto o = PauseGameOrder::deserialize(netData+1, netDataLength-1, versionMinor))
			return o.value();
		break;
	case ORDER_PLAYER_QUIT_GAME :
		if (auto o = PlayerQuitsGameOrder::deserialize(netData+1, netDataLength-1, versionMinor))
			return o.value();
		break;
	case ORDER_ADJUST_LATENCY :
		if (auto o = AdjustLatency::deserialize(netData+1, netDataLength-1, versionMinor))
			return o.value();
		break;
	default:
		printf("Bad packet recieved in Order.cpp (%d)\n", netData[0]);
	}
	return std::shared_ptr<Order>();
}
