// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "OrderMessages.h"
#include <iostream>
#include <sstream>
#include <vector>
#include "Version.h"

using namespace GAGCore;

NetSendOrder::NetSendOrder()
{
}

NetSendOrder::NetSendOrder(std::shared_ptr<Order> newOrder)
{
	order=newOrder;
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
	if (size > MAX_NET_SEND_ORDER_SIZE)
	{
		std::ostringstream msg;
		msg << "NetSendOrder size " << size << " exceeds max " << MAX_NET_SEND_ORDER_SIZE;
		throw std::ios_base::failure(msg.str());
	}

	std::vector<Uint8> buffer(size);
	stream->read(buffer.data(), size, "data");
	stream->readLeaveSection();

	order = Order::getOrder(buffer.data(), size, VERSION_MINOR);

	// If this couldn't be interpreted return it returned a NULL order, so we throw.
	if (order == std::shared_ptr<Order>())
		throw std::ios_base::failure("Couldn't decode data stream to an Order: bad format.");

	order->sender = stream->readUint8("sender");
	order->gameCheckSum = stream->readUint32("checksum");
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

NetPing::NetPing()
{

}

Uint8 NetPing::getMessageType() const
{
	return MNetPing;
}

void NetPing::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetPing");
	stream->writeLeaveSection();
}

void NetPing::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetPing");
	stream->readLeaveSection();
}

std::string NetPing::format() const
{
	std::ostringstream s;
	s<<"NetPing()";
	return s.str();
}

bool NetPing::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetPing))
	{
		//const NetPing& r = dynamic_cast<const NetPing&>(rhs);
		return true;
	}
	return false;
}

NetPingReply::NetPingReply()
{

}

Uint8 NetPingReply::getMessageType() const
{
	return MNetPingReply;
}

void NetPingReply::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetPingReply");
	stream->writeLeaveSection();
}

void NetPingReply::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetPingReply");
	stream->readLeaveSection();
}

std::string NetPingReply::format() const
{
	std::ostringstream s;
	s<<"NetPingReply()";
	return s.str();
}

bool NetPingReply::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetPingReply))
	{
		//const NetPingReply& r = dynamic_cast<const NetPingReply&>(rhs);
		return true;
	}
	return false;
}

NetSetLatencyMode::NetSetLatencyMode()
	: latencyAdjustment(0)
{

}

NetSetLatencyMode::NetSetLatencyMode(Uint8 latencyAdjustment)
	:latencyAdjustment(latencyAdjustment)
{
}

Uint8 NetSetLatencyMode::getMessageType() const
{
	return MNetSetLatencyMode;
}

void NetSetLatencyMode::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetSetLatencyMode");
	stream->writeUint8(latencyAdjustment, "latencyAdjustment");
	stream->writeLeaveSection();
}

void NetSetLatencyMode::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSetLatencyMode");
	latencyAdjustment = stream->readUint8("latencyAdjustment");
	stream->readLeaveSection();
}

std::string NetSetLatencyMode::format() const
{
	std::ostringstream s;
	s<<"NetSetLatencyMode("<<"latencyAdjustment="<<static_cast<int>(latencyAdjustment)<<"; "<<")";
	return s.str();
}

bool NetSetLatencyMode::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSetLatencyMode))
	{
		const NetSetLatencyMode& r = dynamic_cast<const NetSetLatencyMode&>(rhs);
		if(r.latencyAdjustment == latencyAdjustment)
			return true;
	}
	return false;
}

Uint8 NetSetLatencyMode::getLatencyAdjustment() const
{
	return latencyAdjustment;
}
