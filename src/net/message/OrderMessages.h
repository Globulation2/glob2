// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include <memory>
#include <string>

#include "NetMessage.h"
#include "NetMessageType.h"
#include "Order.h"

/// Bound the envelope before allocation, independently of transport framing.
/// Replay streams have no network frame limit. Individual order decoders must
/// still enforce their own size/work limits; this cap is only a backstop.
constexpr Uint32 MAX_NET_SEND_ORDER_SIZE = 1u << 20;

/// Wraps an Order for transmission across the network. The simulation engine
/// produces Orders from local input and AI; NetSendOrder is the wire envelope.
class NetSendOrder : public NetMessage
{
public:
	/// Creates an envelope holding a NULL Order.
	NetSendOrder();

	/// Takes ownership of the supplied Order.
	NetSendOrder(std::shared_ptr<Order> newOrder);

	/// Replaces any existing Order with the new one.
	void addOrder(std::shared_ptr<Order> newOrder);

	std::shared_ptr<Order> getOrder();

	void changeOrder(std::shared_ptr<Order> newOrder);

	/// Sets the version used by subsequent decodeData() calls.
	/// Defaults to VERSION_MINOR; replay readers supply their header version.
	/// Does not affect encoding.
	void setDecodeVersionMinor(Uint32 newVersionMinor);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	/// Wire format: Uint32 size | size bytes payload | Uint8 sender | Uint32 checksum.
	/// Throws std::ios_base::failure for an oversized or invalid order payload.
	/// ReplayReader rejects the replay; NetConnection catches decode exceptions
	/// at the receive boundary and closes the malformed peer. The generic
	/// NetMessage dispatcher propagates exceptions to its caller.
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;
private:
	std::shared_ptr<Order> order;

	Uint32 decodeVersionMinor;
};

/// Latency probe sent periodically to measure round-trip time.
class NetPing : public NetMessage
{
public:
	NetPing();

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;
};

/// Reply to NetPing.
class NetPingReply : public NetMessage
{
public:
	NetPingReply();

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;
};

/// Server -> all clients: change the order-pipeline latency adjustment, used
/// to keep all clients deterministically in sync under variable network conditions.
class NetSetLatencyMode : public NetMessage
{
public:
	NetSetLatencyMode();
	NetSetLatencyMode(Uint8 latencyAdjustment);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	Uint8 getLatencyAdjustment() const;
private:
	Uint8 latencyAdjustment;
};
