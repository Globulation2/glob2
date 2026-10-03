// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "OrderMessages.h"
#include <TextStream.h>
#include <iostream>
#include <sstream>
#include <vector>
#include "Version.h"

using namespace GAGCore;

NetSendOrder::NetSendOrder()
{
	decodeVersionMinor=VERSION_MINOR;
}

NetSendOrder::NetSendOrder(std::shared_ptr<Order> newOrder)
{
	order=newOrder;
	decodeVersionMinor=VERSION_MINOR;
}

void NetSendOrder::setDecodeVersionMinor(Uint32 newVersionMinor)
{
	decodeVersionMinor=newVersionMinor;
}

void NetSendOrder::changeOrder(std::shared_ptr<Order> newOrder)
{
	order = newOrder;
}

std::shared_ptr<Order> NetSendOrder::getOrder()
{
	return order;
}

Uint8 NetSendOrder::getMessageType() const
{
	return MNetSendOrder;
}

void NetSendOrder::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetSendOrder");
	Uint32 orderLength = order->getDataLength();
	stream->writeUint32(orderLength+1, "size");
	stream->writeUint8(order->getOrderType(), "orderType");
	stream->write(order->getData(), order->getDataLength(), "data");
	stream->writeUint8(order->sender, "sender");
	stream->writeUint32(order->gameCheckSum, "checksum");
	stream->writeLeaveSection();
}

void NetSendOrder::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSendOrder");
	const Uint32 size = stream->readUint32("size");

	// Validate before allocating: an attacker- or corruption-supplied multi-GB
	// size would otherwise either throw std::bad_alloc (which callers catch
	// only as ios_base::failure and therefore miss) or waste a large
	// allocation before the downstream "bad format" path fires. The buffer
	// below is RAII-managed so any subsequent throw can't leak it.
	if (size == 0 || size > MAX_NET_SEND_ORDER_SIZE)
	{
		std::ostringstream msg;
		msg << "NetSendOrder size " << size << " exceeds max " << MAX_NET_SEND_ORDER_SIZE;
		throw std::ios_base::failure(msg.str());
	}

	std::vector<Uint8> buffer(size);
	if (dynamic_cast<GAGCore::TextInputStream *>(stream))
	{
		// Binary streams concatenate orderType and data; named text fields do not.
		buffer[0] = stream->readUint8("orderType");
		stream->read(buffer.data()+1, size-1, "data");
	}
	else
		stream->read(buffer.data(), size, "data");

	order = Order::getOrder(buffer.data(), size, decodeVersionMinor);

	// If this couldn't be interpreted return it returned a NULL order, so we throw.
	if (order == std::shared_ptr<Order>())
		throw std::ios_base::failure("Couldn't decode data stream to an Order: bad format.");

	order->sender = stream->readUint8("sender");
	order->gameCheckSum = stream->readUint32("checksum");
	stream->readLeaveSection();
}

std::string NetSendOrder::format() const
{
	std::stringstream s;
	if(order==NULL)
	{
		s<<"NetSendOrder()";
	}
	else
	{
		s<<"NetSendOrder(orderType="<<static_cast<int>(order->getOrderType())<<")";
	}
	return s.str();
}

bool NetSendOrder::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSendOrder))
	{
		const NetSendOrder& r = dynamic_cast<const NetSendOrder&>(rhs);
		if(order==NULL || r.order==NULL)
		{
			return order == r.order;
		}
		if(typeid(r.order) == typeid(order))
		{
			return true;
		}
	}
	return false;
}
