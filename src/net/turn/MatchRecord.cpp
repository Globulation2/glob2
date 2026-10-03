// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "MatchRecord.h"

#include <cstring>
#include <fstream>
#include <iterator>

namespace Turn
{
namespace
{
	constexpr char MAGIC[4] = {'G', '2', 'M', 'R'};

	class Writer
	{
	public:
		std::vector<std::uint8_t> out;
		void u8(std::uint8_t v) { out.push_back(v); }
		void u16(std::uint16_t v) { u8(v >> 8); u8(v & 0xFF); }
		void u32(std::uint32_t v) { u16(v >> 16); u16(v & 0xFFFF); }
		void bytes(const void* data, std::size_t size)
		{
			const auto* p = static_cast<const std::uint8_t*>(data);
			out.insert(out.end(), p, p + size);
		}
		void text32(const std::string& s, std::size_t max, const char* field)
		{
			if (s.size() > max)
				throw MatchRecordError(std::string("Match record field too long: ") + field);
			u32(static_cast<std::uint32_t>(s.size()));
			bytes(s.data(), s.size());
		}
	};

	class Reader
	{
	public:
		Reader(const std::uint8_t* data, std::size_t size) : data(data), size(size) {}
		void need(std::size_t n) const
		{
			if (n > size - pos)
				throw MatchRecordError("Match record truncated");
		}
		std::uint8_t u8() { need(1); return data[pos++]; }
		std::uint16_t u16() { const std::uint16_t hi = u8(); return static_cast<std::uint16_t>((hi << 8) | u8()); }
		std::uint32_t u32() { const std::uint32_t hi = u16(); return (hi << 16) | u16(); }
		void bytes(void* dst, std::size_t n) { need(n); std::memcpy(dst, data + pos, n); pos += n; }
		std::string text32(std::size_t max, const char* field)
		{
			const std::size_t n = u32();
			if (n > max)
				throw MatchRecordError(std::string("Match record field too long: ") + field);
			need(n);
			std::string s(reinterpret_cast<const char*>(data + pos), n);
			pos += n;
			return s;
		}
		std::size_t remaining() const { return size - pos; }
		std::size_t position() const { return pos; }
	private:
		const std::uint8_t* data;
		std::size_t size;
		std::size_t pos = 0;
	};

	bool before(std::uint32_t at, std::uint8_t as, std::uint32_t bt, std::uint8_t bs)
	{
		return at < bt || (at == bt && as < bs);
	}
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t size)
{
	static const std::array<std::uint32_t, 256> table = [] {
		std::array<std::uint32_t, 256> t{};
		for (std::uint32_t i = 0; i < 256; ++i)
		{
			std::uint32_t c = i;
			for (int k = 0; k < 8; ++k)
				c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			t[i] = c;
		}
		return t;
	}();
	std::uint32_t crc = 0xFFFFFFFFu;
	for (std::size_t i = 0; i < size; ++i)
		crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
	return crc ^ 0xFFFFFFFFu;
}

bool MatchRecord::operator==(const MatchRecord& o) const
{
	return flags == o.flags && matchId == o.matchId && simVersion == o.simVersion && tickRateMilliHz == o.tickRateMilliHz &&
	       bundleInterval == o.bundleInterval && checksumInterval == o.checksumInterval && humanSeatMask == o.humanSeatMask &&
	       endTick == o.endTick && setupJson == o.setupJson && mapHash == o.mapHash && turns == o.turns &&
	       reports == o.reports && events == o.events;
}

std::vector<std::uint8_t> MatchRecord::serialize() const
{
	Writer w;
	w.bytes(MAGIC, 4);
	w.u16(FORMAT_VERSION);
	w.u32(flags);
	w.text32(matchId, MAX_ID_BYTES, "matchId");
	w.text32(simVersion, MAX_ID_BYTES, "simVersion");
	w.u32(tickRateMilliHz);
	w.u8(bundleInterval);
	w.u16(checksumInterval);
	w.u32(humanSeatMask);
	w.u32(endTick);
	w.text32(setupJson, MAX_SETUP_BYTES, "setupJson");
	w.bytes(mapHash.data(), mapHash.size());
	w.u32(static_cast<std::uint32_t>(turns.size()));
	for (std::size_t i = 0; i < turns.size(); ++i)
	{
		const auto& t = turns[i];
		if (t.seat >= MAX_SEATS || t.order.empty() || t.order.size() > MAX_ORDER_BYTES ||
		    (i && !before(turns[i - 1].tick, turns[i - 1].seat, t.tick, t.seat)) || t.tick >= endTick)
			throw MatchRecordError("Match record turn out of order or range");
		w.u32(t.tick);
		w.u8(t.seat);
		w.u16(static_cast<std::uint16_t>(t.order.size()));
		w.bytes(t.order.data(), t.order.size());
	}
	w.u32(static_cast<std::uint32_t>(reports.size()));
	for (std::size_t i = 0; i < reports.size(); ++i)
	{
		const auto& r = reports[i];
		if (r.seat >= MAX_SEATS || (i && !before(reports[i - 1].tick, reports[i - 1].seat, r.tick, r.seat)))
			throw MatchRecordError("Match record report out of order");
		w.u32(r.tick);
		w.u8(r.seat);
		w.u32(r.checksum);
	}
	w.u32(static_cast<std::uint32_t>(events.size()));
	for (std::size_t i = 0; i < events.size(); ++i)
	{
		const auto& e = events[i];
		if (e.seat >= MAX_SEATS || (i && events[i - 1].tick > e.tick))
			throw MatchRecordError("Match record event out of order");
		w.u32(e.tick);
		w.u8(e.seat);
		w.u8(static_cast<std::uint8_t>(e.kind));
	}
	w.u32(crc32(w.out.data(), w.out.size()));
	return std::move(w.out);
}

MatchRecord MatchRecord::parse(const std::vector<std::uint8_t>& bytes)
{
	if (bytes.size() < 4 + 2 + 4)
		throw MatchRecordError("Match record truncated");
	if (std::memcmp(bytes.data(), MAGIC, 4) != 0)
		throw MatchRecordError("Not a match record");
	const std::size_t body = bytes.size() - 4;
	const std::uint32_t storedCrc = (std::uint32_t(bytes[body]) << 24) | (std::uint32_t(bytes[body + 1]) << 16) |
	                                (std::uint32_t(bytes[body + 2]) << 8) | bytes[body + 3];
	Reader r(bytes.data(), body);
	char magic[4];
	r.bytes(magic, 4);
	const std::uint16_t version = r.u16();
	if (version == 0 || version > FORMAT_VERSION)
		throw MatchRecordError("Unsupported match record version " + std::to_string(version));
	if (crc32(bytes.data(), body) != storedCrc)
		throw MatchRecordError("Match record checksum mismatch");

	MatchRecord m;
	m.flags = r.u32();
	m.matchId = r.text32(MAX_ID_BYTES, "matchId");
	m.simVersion = r.text32(MAX_ID_BYTES, "simVersion");
	m.tickRateMilliHz = r.u32();
	m.bundleInterval = r.u8();
	m.checksumInterval = r.u16();
	m.humanSeatMask = r.u32();
	m.endTick = r.u32();
	m.setupJson = r.text32(MAX_SETUP_BYTES, "setupJson");
	r.bytes(m.mapHash.data(), m.mapHash.size());
	if (!m.tickRateMilliHz || !m.bundleInterval || !m.checksumInterval)
		throw MatchRecordError("Match record timing invalid");

	const std::uint32_t turnCount = r.u32();
	if (turnCount > r.remaining() / 8)
		throw MatchRecordError("Match record turn count too large");
	m.turns.reserve(turnCount);
	for (std::uint32_t i = 0; i < turnCount; ++i)
	{
		TurnEntry t;
		t.tick = r.u32();
		t.seat = r.u8();
		const std::size_t n = r.u16();
		if (t.seat >= MAX_SEATS || n < 1 || n > MAX_ORDER_BYTES || t.tick >= m.endTick)
			throw MatchRecordError("Match record turn invalid");
		if (!m.turns.empty() && !before(m.turns.back().tick, m.turns.back().seat, t.tick, t.seat))
			throw MatchRecordError("Match record turns out of order");
		t.order.resize(n);
		r.bytes(t.order.data(), n);
		m.turns.push_back(std::move(t));
	}
	const std::uint32_t reportCount = r.u32();
	if (reportCount > r.remaining() / 9)
		throw MatchRecordError("Match record report count too large");
	m.reports.reserve(reportCount);
	for (std::uint32_t i = 0; i < reportCount; ++i)
	{
		ChecksumEntry c;
		c.tick = r.u32();
		c.seat = r.u8();
		c.checksum = r.u32();
		if (c.seat >= MAX_SEATS || (!m.reports.empty() && !before(m.reports.back().tick, m.reports.back().seat, c.tick, c.seat)))
			throw MatchRecordError("Match record reports invalid");
		m.reports.push_back(c);
	}
	const std::uint32_t eventCount = r.u32();
	if (eventCount > r.remaining() / 6)
		throw MatchRecordError("Match record event count too large");
	m.events.reserve(eventCount);
	for (std::uint32_t i = 0; i < eventCount; ++i)
	{
		MatchEvent e;
		e.tick = r.u32();
		e.seat = r.u8();
		const std::uint8_t kind = r.u8();
		if (e.seat >= MAX_SEATS || kind < 1 || kind > MATCH_EVENT_KIND_MAX || (!m.events.empty() && m.events.back().tick > e.tick))
			throw MatchRecordError("Match record events invalid");
		e.kind = static_cast<MatchEventKind>(kind);
		m.events.push_back(e);
	}
	if (r.remaining())
		throw MatchRecordError("Match record has trailing bytes");
	return m;
}

void MatchRecord::writeFile(const std::string& path) const
{
	const auto bytes = serialize();
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	if (!out)
		throw MatchRecordError("Cannot write match record " + path);
}

MatchRecord MatchRecord::readFile(const std::string& path)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
		throw MatchRecordError("Cannot open match record " + path);
	std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	return parse(bytes);
}
}
