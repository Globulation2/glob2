// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include <memory>
#include <string>

#include "NetMessage.h"
#include "NetMessageType.h"
#include "Order.h"

/// Maximum byte length of the payload inside a NetSendOrder envelope. The
/// largest legitimate Order is OrderVoiceData (capped at 2048 bytes by
/// VoiceRecorder); every other Order is tens of bytes. 1 MiB mirrors the
/// string-length cap in BinaryInputStream::readText and leaves three orders
/// of magnitude of headroom against future order growth, while keeping any
/// pre-allocation under control. Used by NetSendOrder::decodeData to reject
/// attacker-supplied envelope sizes before they reach `new Uint8[size]`.
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

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	/// Wire format: Uint32 size | size bytes payload | Uint8 sender | Uint32 checksum.
	/// Throws std::ios_base::failure if size > MAX_NET_SEND_ORDER_SIZE or if
	/// Order::getOrder cannot interpret the payload. All callers
	/// (ReplayReader::loadReplay, retrieveOrder, NetMessage::getNetMessage)
	/// already treat ios_base::failure as a clean "drop this message" signal.
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;
private:
	std::shared_ptr<Order> order;
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
