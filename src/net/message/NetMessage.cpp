// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "NetMessage.h"

#include <iostream>

#include "OrderMessages.h"
#include "TurnMessages.h"

std::shared_ptr<NetMessage> NetMessage::getNetMessage(GAGCore::InputStream* stream)
{
	Uint8 netType = stream->readUint8("messageType");
	std::shared_ptr<NetMessage> message;
	switch(netType)
	{
		case MNetSendOrder:
		message.reset(new NetSendOrder);
		break;
		default:
		if (netType >= MNetTurnFirst && netType <= MNetTurnLast)
		{
			message = Turn::TurnCodec::create(netType);
			if (message)
				break;
		}
		// Untrusted byte from the wire didn't match any known opcode.
		// Drop the message and let the caller handle the null shared_ptr
		// (existing call sites already guard with `if(!message) return;`).
		std::cerr << "NetMessage::getNetMessage: unknown opcode " << (int)netType << std::endl;
		return std::shared_ptr<NetMessage>();
	}
	message->decodeData(stream);
	return message;
}
