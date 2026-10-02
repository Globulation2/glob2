// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "TurnMessages.h"

#include <BinaryStream.h>
#include <StreamBackend.h>

#include <ios>
#include <sstream>
#include <stdexcept>

using namespace GAGCore;

namespace Turn
{
namespace
{
	[[noreturn]] void fail(const char* what)
	{
		throw std::ios_base::failure(std::string("Turn message: ") + what);
	}

	// MemoryStreamBackend supplies zeros on overread; network input must fail closed.
	class StrictInput final : public MemoryStreamBackend
	{
		std::size_t length;
	public:
		StrictInput(const void* bytes, std::size_t size) : MemoryStreamBackend(bytes, size), length(size) { seekFromStart(0); }
		void read(void* bytes, std::size_t size) override
		{
			if (size > length - getPosition())
				fail("truncated");
			MemoryStreamBackend::read(bytes, size);
		}
	};

	void writeBytes16(OutputStream* s, const std::vector<std::uint8_t>& bytes, const char* field)
	{
		if (bytes.size() > 0xFFFF)
			throw std::length_error("Turn message field too long");
		s->writeUint16(static_cast<Uint16>(bytes.size()), field);
		if (!bytes.empty())
			s->write(bytes.data(), bytes.size(), field);
	}

	std::vector<std::uint8_t> readBytes16(InputStream* s, std::size_t minSize, std::size_t maxSize, const char* field)
	{
		const std::size_t size = s->readUint16(field);
		if (size < minSize || size > maxSize)
			fail(field);
		std::vector<std::uint8_t> bytes(size);
		if (size)
			s->read(bytes.data(), size, field);
		return bytes;
	}

	void writeText32(OutputStream* s, const std::string& text, std::size_t maxSize, const char* field)
	{
		if (text.size() > maxSize)
			throw std::length_error(std::string("Turn message field too long: ") + field);
		s->writeUint32(static_cast<Uint32>(text.size()), field);
		if (!text.empty())
			s->write(text.data(), text.size(), field);
	}

	std::string readText32(InputStream* s, std::size_t maxSize, const char* field)
	{
		const std::size_t size = s->readUint32(field);
		if (size > maxSize)
			fail(field);
		std::string text(size, '\0');
		if (size)
			s->read(&text[0], size, field);
		return text;
	}

	void checkOrderSize(std::size_t size)
	{
		if (size < 1 || size > MAX_ORDER_BYTES)
			throw std::length_error("Turn order size out of range");
	}
}

std::string TurnMessage::format() const
{
	std::ostringstream out;
	out << name() << "(" << TurnCodec::encode(*this).size() << " bytes)";
	return out.str();
}

bool TurnMessage::operator==(const NetMessage& rhs) const
{
	if (rhs.getMessageType() != getMessageType())
		return false;
	return TurnCodec::encode(*this) == TurnCodec::encode(rhs);
}

// Hello

void Hello::encodeData(OutputStream* s) const
{
	s->writeUint16(protocolVersion, "protocolVersion");
	writeText32(s, ticket, MAX_TICKET_BYTES, "ticket");
	s->writeUint32(haveHorizon, "haveHorizon");
}

void Hello::decodeData(InputStream* s)
{
	protocolVersion = s->readUint16("protocolVersion");
	ticket = readText32(s, MAX_TICKET_BYTES, "ticket");
	haveHorizon = s->readUint32("haveHorizon");
}

// Welcome

void Welcome::encodeData(OutputStream* s) const
{
	s->writeUint16(protocolVersion, "protocolVersion");
	s->writeUint8(seat, "seat");
	s->writeUint32(humanSeatMask, "humanSeatMask");
	s->writeUint32(tickRateMilliHz, "tickRateMilliHz");
	s->writeUint8(bundleInterval, "bundleInterval");
	s->writeUint16(checksumInterval, "checksumInterval");
	s->writeUint32(relayTick, "relayTick");
	s->writeUint32(resumeFromTick, "resumeFromTick");
	s->writeUint32(graceTicks, "graceTicks");
	s->writeUint32(lastClientSequence, "lastClientSequence");
}

void Welcome::decodeData(InputStream* s)
{
	protocolVersion = s->readUint16("protocolVersion");
	seat = s->readUint8("seat");
	humanSeatMask = s->readUint32("humanSeatMask");
	tickRateMilliHz = s->readUint32("tickRateMilliHz");
	bundleInterval = s->readUint8("bundleInterval");
	checksumInterval = s->readUint16("checksumInterval");
	relayTick = s->readUint32("relayTick");
	resumeFromTick = s->readUint32("resumeFromTick");
	graceTicks = s->readUint32("graceTicks");
	lastClientSequence = s->readUint32("lastClientSequence");
	if (seat >= MAX_SEATS || !(humanSeatMask & (1u << seat)))
		fail("welcome seat");
	if (!bundleInterval || !checksumInterval || !tickRateMilliHz)
		fail("welcome timing");
}

// Reject

void Reject::encodeData(OutputStream* s) const
{
	s->writeUint8(static_cast<Uint8>(reason), "reason");
	writeText32(s, detail, MAX_REJECT_DETAIL_BYTES, "detail");
}

void Reject::decodeData(InputStream* s)
{
	const Uint8 r = s->readUint8("reason");
	if (r < 1 || r > REJECT_REASON_MAX)
		fail("reject reason");
	reason = static_cast<RejectReason>(r);
	detail = readText32(s, MAX_REJECT_DETAIL_BYTES, "detail");
}

// OrderSubmit

void OrderSubmit::encodeData(OutputStream* s) const
{
	checkOrderSize(order.size());
	s->writeUint32(clientSequence, "clientSequence");
	writeBytes16(s, order, "order");
}

void OrderSubmit::decodeData(InputStream* s)
{
	clientSequence = s->readUint32("clientSequence");
	order = readBytes16(s, 1, MAX_ORDER_BYTES, "order");
}

// TurnBundle

void TurnBundle::validate() const
{
	if (fromTick > horizonTick)
		fail("bundle range");
	if (entries.size() > 0xFFFF)
		fail("bundle count");
	for (std::size_t i = 0; i < entries.size(); ++i)
	{
		const TurnEntry& e = entries[i];
		if (e.tick < fromTick || e.tick >= horizonTick)
			fail("bundle entry tick");
		if (e.seat >= MAX_SEATS)
			fail("bundle entry seat");
		if (e.order.empty() || e.order.size() > MAX_ORDER_BYTES)
			fail("bundle entry order");
		if (i && (entries[i - 1].tick > e.tick || (entries[i - 1].tick == e.tick && entries[i - 1].seat >= e.seat)))
			fail("bundle entry order");
	}
}

std::size_t TurnBundle::payloadBytes() const
{
	std::size_t bytes = 1 + 4 + 4 + 2;
	for (const auto& e : entries)
		bytes += entryWireBytes(e);
	return bytes;
}

void TurnBundle::encodeData(OutputStream* s) const
{
	try { validate(); }
	catch (const std::ios_base::failure& e) { throw std::invalid_argument(e.what()); }
	s->writeUint32(fromTick, "fromTick");
	s->writeUint32(horizonTick, "horizonTick");
	s->writeUint16(static_cast<Uint16>(entries.size()), "count");
	for (const auto& e : entries)
	{
		s->writeUint32(e.tick, "tick");
		s->writeUint8(e.seat, "seat");
		writeBytes16(s, e.order, "order");
	}
}

void TurnBundle::decodeData(InputStream* s)
{
	fromTick = s->readUint32("fromTick");
	horizonTick = s->readUint32("horizonTick");
	const std::size_t count = s->readUint16("count");
	entries.clear();
	// Each entry needs at least 8 bytes, so a frame can never hold more than ~8k; the
	// strict reader stops a lying count long before this reservation matters.
	entries.reserve(count < 1024 ? count : 1024);
	for (std::size_t i = 0; i < count; ++i)
	{
		TurnEntry e;
		e.tick = s->readUint32("tick");
		e.seat = s->readUint8("seat");
		e.order = readBytes16(s, 1, MAX_ORDER_BYTES, "order");
		entries.push_back(std::move(e));
	}
	validate();
}

// ChecksumReport

void ChecksumReport::encodeData(OutputStream* s) const
{
	s->writeUint32(tick, "tick");
	s->writeUint32(checksum, "checksum");
}

void ChecksumReport::decodeData(InputStream* s)
{
	tick = s->readUint32("tick");
	checksum = s->readUint32("checksum");
}

// Presence

void Presence::encodeData(OutputStream* s) const
{
	if (seats.size() > MAX_SEATS)
		throw std::length_error("Too many presence seats");
	s->writeUint8(static_cast<Uint8>(seats.size()), "count");
	for (const auto& p : seats)
	{
		s->writeUint8(p.seat, "seat");
		s->writeUint8(static_cast<Uint8>(p.state), "state");
		s->writeUint32(p.graceRemainingTicks, "graceRemainingTicks");
		s->writeUint32(p.lagTicks, "lagTicks");
	}
}

void Presence::decodeData(InputStream* s)
{
	const std::size_t count = s->readUint8("count");
	if (count > MAX_SEATS)
		fail("presence count");
	seats.clear();
	for (std::size_t i = 0; i < count; ++i)
	{
		SeatPresence p;
		p.seat = s->readUint8("seat");
		const Uint8 state = s->readUint8("state");
		if (p.seat >= MAX_SEATS || state > PRESENCE_STATE_MAX)
			fail("presence entry");
		if (i && seats.back().seat >= p.seat)
			fail("presence order");
		p.state = static_cast<PresenceState>(state);
		p.graceRemainingTicks = s->readUint32("graceRemainingTicks");
		p.lagTicks = s->readUint32("lagTicks");
		seats.push_back(p);
	}
}

// SeatLatency

void SeatLatency::encodeData(OutputStream* s) const
{
	if (seats.size() > MAX_SEATS)
		throw std::length_error("Too many latency seats");
	s->writeUint8(static_cast<Uint8>(seats.size()), "count");
	for (const auto& p : seats)
	{
		s->writeUint8(p.seat, "seat");
		s->writeUint32(p.rttMicros, "rttMicros");
	}
}

void SeatLatency::decodeData(InputStream* s)
{
	const std::size_t count = s->readUint8("count");
	if (count > MAX_SEATS)
		fail("latency count");
	seats.clear();
	for (std::size_t i = 0; i < count; ++i)
	{
		SeatRoundTrip p;
		p.seat = s->readUint8("seat");
		if (p.seat >= MAX_SEATS || (i && seats.back().seat >= p.seat))
			fail("latency entry");
		p.rttMicros = s->readUint32("rttMicros");
		seats.push_back(p);
	}
}

// ResyncRequest

void ResyncRequest::encodeData(OutputStream* s) const { s->writeUint32(fromTick, "fromTick"); }
void ResyncRequest::decodeData(InputStream* s) { fromTick = s->readUint32("fromTick"); }

// DesyncNotice

void DesyncNotice::encodeData(OutputStream* s) const
{
	s->writeUint32(tick, "tick");
	s->writeUint8(static_cast<Uint8>(verdict), "verdict");
	s->writeUint32(divergedSeatMask, "divergedSeatMask");
}

void DesyncNotice::decodeData(InputStream* s)
{
	tick = s->readUint32("tick");
	const Uint8 v = s->readUint8("verdict");
	if (v < 1 || v > DESYNC_VERDICT_MAX)
		fail("desync verdict");
	verdict = static_cast<DesyncVerdict>(v);
	divergedSeatMask = s->readUint32("divergedSeatMask");
}

// Quit

void Quit::encodeData(OutputStream* s) const { s->writeUint8(static_cast<Uint8>(reason), "reason"); }
void Quit::decodeData(InputStream* s)
{
	const Uint8 r = s->readUint8("reason");
	if (r > QUIT_REASON_MAX)
		fail("quit reason");
	reason = static_cast<QuitReason>(r);
}

// Ping / Pong

void Ping::encodeData(OutputStream* s) const
{
	s->writeUint32(nonce, "nonce");
	s->writeUint32(executedTick, "executedTick");
}
void Ping::decodeData(InputStream* s)
{
	nonce = s->readUint32("nonce");
	executedTick = s->readUint32("executedTick");
}

void Pong::encodeData(OutputStream* s) const
{
	s->writeUint32(nonce, "nonce");
	s->writeUint32(relayTick, "relayTick");
	s->writeUint32(lastClientSequence, "lastClientSequence");
}
void Pong::decodeData(InputStream* s)
{
	nonce = s->readUint32("nonce");
	relayTick = s->readUint32("relayTick");
	lastClientSequence = s->readUint32("lastClientSequence");
}

// Codec

namespace TurnCodec
{
	std::shared_ptr<NetMessage> create(std::uint8_t type)
	{
		switch (type)
		{
		case MSG_HELLO: return std::make_shared<Hello>();
		case MSG_WELCOME: return std::make_shared<Welcome>();
		case MSG_REJECT: return std::make_shared<Reject>();
		case MSG_ORDER_SUBMIT: return std::make_shared<OrderSubmit>();
		case MSG_TURN_BUNDLE: return std::make_shared<TurnBundle>();
		case MSG_CHECKSUM_REPORT: return std::make_shared<ChecksumReport>();
		case MSG_PRESENCE: return std::make_shared<Presence>();
		case MSG_RESYNC_REQUEST: return std::make_shared<ResyncRequest>();
		case MSG_DESYNC_NOTICE: return std::make_shared<DesyncNotice>();
		case MSG_QUIT: return std::make_shared<Quit>();
		case MSG_PING: return std::make_shared<Ping>();
		case MSG_PONG: return std::make_shared<Pong>();
		case MSG_SEAT_LATENCY: return std::make_shared<SeatLatency>();
		default: return nullptr;
		}
	}

	std::vector<std::uint8_t> encode(const NetMessage& message)
	{
		auto* backend = new MemoryStreamBackend;
		BinaryOutputStream stream(backend);
		stream.writeUint8(message.getMessageType(), "messageType");
		message.encodeData(&stream);
		const std::size_t length = backend->getPosition();
		if (length > MAX_FRAME_BYTES)
			throw std::length_error("Turn message exceeds the frame limit");
		std::vector<std::uint8_t> bytes(length);
		backend->seekFromStart(0);
		backend->read(bytes.data(), length);
		return bytes;
	}

	std::shared_ptr<NetMessage> decode(const std::uint8_t* data, std::size_t size)
	{
		if (!data || size < 1 || size > MAX_FRAME_BYTES)
			return nullptr;
		auto message = create(data[0]);
		if (!message)
			return nullptr;
		try
		{
			auto* backend = new StrictInput(data + 1, size - 1);
			BinaryInputStream stream(backend);
			message->decodeData(&stream);
			if (backend->getPosition() != size - 1)
				return nullptr;
		}
		catch (const std::exception&)
		{
			return nullptr;
		}
		return message;
	}
}

std::vector<TurnBundle> splitIntoBundles(const std::vector<TurnEntry>& entries, std::uint32_t fromTick,
                                         std::uint32_t horizonTick, std::size_t maxPayloadBytes)
{
	std::vector<TurnBundle> bundles;
	if (fromTick >= horizonTick)
		return bundles;
	TurnBundle current;
	current.fromTick = fromTick;
	std::size_t bytes = 1 + 4 + 4 + 2;
	std::size_t i = 0;
	while (i < entries.size())
	{
		if (entries[i].tick < fromTick || entries[i].tick >= horizonTick)
		{
			++i;
			continue;
		}
		const std::uint32_t tick = entries[i].tick;
		std::size_t j = i, groupBytes = 0;
		while (j < entries.size() && entries[j].tick == tick)
			groupBytes += entryWireBytes(entries[j++]);
		if (!current.entries.empty() && (bytes + groupBytes > maxPayloadBytes || current.entries.size() + (j - i) > 0xFFFF))
		{
			current.horizonTick = tick;
			bundles.push_back(std::move(current));
			current = TurnBundle();
			current.fromTick = tick;
			bytes = 1 + 4 + 4 + 2;
		}
		current.entries.insert(current.entries.end(), entries.begin() + i, entries.begin() + j);
		bytes += groupBytes;
		i = j;
	}
	current.horizonTick = horizonTick;
	bundles.push_back(std::move(current));
	return bundles;
}
}
