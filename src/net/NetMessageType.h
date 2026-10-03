// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

/// Type tags of the messages NetConnection frames. The YOG lobby protocol that used
/// values 0-63 was removed; MNetSendOrder keeps its old value 44 (the order codec the
/// replay files and tests use) and the remaining low values stay unused.
enum NetMessageType
{
	MNetSendOrder = 44,

	// 0xA0-0xBF is reserved for the turn protocol (src/net/turn/TurnProtocol.h,
	// docs/multiplayer/turn-protocol.md).
	MNetTurnFirst = 0xA0,
	MNetTurnLast = 0xBF,
	// 0xC0-0xCF is reserved for the LAN room protocol (src/net/lan/LanProtocol.h,
	// docs/multiplayer/lan.md), which shares a LAN guest's connection with the turn
	// protocol. NetMessage::getNetMessage does not decode it.
	MNetLanRoomFirst = 0xC0,
	MNetLanRoomLast = 0xCF,
};
