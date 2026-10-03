// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "LanProtocol.h"

#include <chrono>
#include <random>
#include <stdexcept>

#include "Sha256.h"

namespace Lan
{
namespace
{
void putU32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
	out.push_back(static_cast<std::uint8_t>(v >> 24));
	out.push_back(static_cast<std::uint8_t>(v >> 16));
	out.push_back(static_cast<std::uint8_t>(v >> 8));
	out.push_back(static_cast<std::uint8_t>(v));
}
std::uint32_t getU32(const std::uint8_t* p)
{
	return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}
}

std::vector<std::uint8_t> encodeJson(const nlohmann::json& message)
{
	const std::string text = message.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
	if (text.size() > MAX_JSON_BYTES)
		throw std::length_error("LAN room message too large");
	std::vector<std::uint8_t> out;
	out.reserve(5 + text.size());
	out.push_back(MSG_ROOM_JSON);
	putU32(out, static_cast<std::uint32_t>(text.size()));
	out.insert(out.end(), text.begin(), text.end());
	return out;
}

std::optional<nlohmann::json> decodeJson(const std::vector<std::uint8_t>& payload)
{
	if (payload.size() < 5 || payload[0] != MSG_ROOM_JSON)
		return std::nullopt;
	const std::uint32_t length = getU32(payload.data() + 1);
	if (length != payload.size() - 5 || length > MAX_JSON_BYTES)
		return std::nullopt;
	try
	{
		auto value = nlohmann::json::parse(payload.begin() + 5, payload.end());
		if (!value.is_object() || !value.contains("type") || !value["type"].is_string())
			return std::nullopt;
		return value;
	}
	catch (const std::exception&)
	{
		return std::nullopt;
	}
}

std::vector<std::uint8_t> encodeMapChunk(const std::string& hash, std::uint32_t offset, std::uint32_t total,
                                         const std::uint8_t* data, std::size_t size)
{
	Online::Sha256::Digest digest;
	if (!Online::parseSha256Hex(hash, digest) || size > MAP_CHUNK_BYTES)
		throw std::invalid_argument("invalid map chunk");
	std::vector<std::uint8_t> out;
	out.reserve(1 + 32 + 8 + size);
	out.push_back(MSG_MAP_CHUNK);
	out.insert(out.end(), digest.begin(), digest.end());
	putU32(out, offset);
	putU32(out, total);
	out.insert(out.end(), data, data + size);
	return out;
}

std::optional<MapChunk> decodeMapChunk(const std::vector<std::uint8_t>& payload)
{
	if (payload.size() < 1 + 32 + 8 || payload[0] != MSG_MAP_CHUNK || payload.size() - 41 > MAP_CHUNK_BYTES)
		return std::nullopt;
	MapChunk chunk;
	chunk.hash = Online::toHex(payload.data() + 1, 32);
	chunk.offset = getU32(payload.data() + 33);
	chunk.total = getU32(payload.data() + 37);
	chunk.bytes.assign(payload.begin() + 41, payload.end());
	if (chunk.total > MAX_MAP_BYTES || chunk.offset > chunk.total || chunk.bytes.size() > chunk.total - chunk.offset)
		return std::nullopt;
	return chunk;
}

LanLink::LanLink(std::unique_ptr<NetTransport> selected) : transport(std::move(selected)) {}

LanLink::~LanLink()
{
	if (transport)
		transport->close();
}

std::unique_ptr<LanLink> LanLink::open(const std::string& endpoint)
{
	auto transport = makeNetTransport();
	transport->open(endpoint);
	return std::make_unique<LanLink>(std::move(transport));
}

NetTransport::State LanLink::state()
{
	if (failed || !transport)
		return NetTransport::State::Closed;
	return transport->state();
}

void LanLink::close()
{
	if (transport)
	{
		if (failure.empty())
			failure = transport->error();
		transport->close();
	}
	failed = true;
	outbox.clear();
	outboxBytes = 0;
	frames.clear();
	pending.clear();
}

std::string LanLink::error() const
{
	if (!failure.empty())
		return failure;
	return transport ? transport->error() : std::string();
}

std::string LanLink::peerAddress() const
{
	return transport ? transport->peerAddress() : std::string();
}

bool LanLink::send(const std::vector<std::uint8_t>& payload)
{
	if (failed)
		return false;
	if (payload.empty() || payload.size() > MAX_FRAME_BYTES || outboxBytes + payload.size() + 2 > MAX_OUTBOX_BYTES)
	{
		failure = payload.size() > MAX_FRAME_BYTES ? "LAN frame too large" : "LAN output queue overflow";
		close();
		return false;
	}
	std::vector<std::uint8_t> frame;
	frame.reserve(payload.size() + 2);
	frame.push_back(static_cast<std::uint8_t>(payload.size() >> 8));
	frame.push_back(static_cast<std::uint8_t>(payload.size() & 255));
	frame.insert(frame.end(), payload.begin(), payload.end());
	outboxBytes += frame.size();
	outbox.push_back(std::move(frame));
	flush();
	return !failed;
}

void LanLink::flush()
{
	if (failed || !transport || transport->state() != NetTransport::State::Connected)
		return;
	// Coalesce small frames so the transport sees fewer, larger writes.
	while (!outbox.empty())
	{
		std::vector<std::uint8_t> batch = std::move(outbox.front());
		outbox.pop_front();
		while (!outbox.empty() && batch.size() + outbox.front().size() <= NetTransport::chunkLimit)
		{
			batch.insert(batch.end(), outbox.front().begin(), outbox.front().end());
			outbox.pop_front();
		}
		const std::size_t size = batch.size();
		if (!transport->send(batch))
		{
			if (transport->state() != NetTransport::State::Connected)
			{
				close();
				return;
			}
			// The transport's queue is full: keep the batch for the next flush.
			outbox.push_front(std::move(batch));
			return;
		}
		outboxBytes -= size;
	}
}

void LanLink::pump()
{
	if (failed || !transport)
		return;
	flush();
	std::vector<std::uint8_t> bytes;
	std::size_t offset = 0;
	while (transport->receive(bytes))
	{
		if (bytes.size() > NetTransport::queueLimit - std::min(pending.size(), NetTransport::queueLimit))
		{
			failure = "LAN input overflow";
			close();
			return;
		}
		pending.insert(pending.end(), bytes.begin(), bytes.end());
		while (pending.size() - offset >= 2)
		{
			const std::size_t length = (std::size_t(pending[offset]) << 8) | pending[offset + 1];
			if (!length)
			{
				failure = "Empty LAN frame";
				close();
				return;
			}
			if (pending.size() - offset - 2 < length)
				break;
			frames.emplace_back(pending.begin() + offset + 2, pending.begin() + offset + 2 + length);
			offset += length + 2;
		}
		pending.erase(pending.begin(), pending.begin() + offset);
		offset = 0;
	}
}

NetWaitStatus LanLink::waitHandles(std::vector<NetWaitHandle>& handles) const
{
	if (failed || !transport)
		return NetWaitStatus::Idle;
	if (!frames.empty())
		return NetWaitStatus::Ready;
	return transport->waitHandles(handles);
}

bool LanLink::receive(std::vector<std::uint8_t>& payload)
{
	if (frames.empty())
		pump();
	if (frames.empty())
		return false;
	payload = std::move(frames.front());
	frames.pop_front();
	return true;
}

nlohmann::json RoomState::toJson() const
{
	nlohmann::json m = nlohmann::json::array();
	for (const auto& member : members)
		m.push_back({{"id", member.id}, {"name", member.name}, {"seat", member.seat}, {"ready", member.ready},
		             {"hasMap", member.hasMap}});
	nlohmann::json colors = nlohmann::json::array();
	for (const auto& c : teamColors)
		colors.push_back({c[0], c[1], c[2]});
	return {{"setup", setup.toJson()}, {"mapName", mapName},     {"hostName", hostName}, {"teamColors", colors},
	        {"mapBytes", mapBytes},    {"members", m},           {"started", started}};
}

RoomState RoomState::fromJson(const nlohmann::json& value)
{
	RoomState s;
	s.setup = Online::MatchSetup::fromJson(value.at("setup"));
	s.mapName = clampUtf8(value.at("mapName").get<std::string>(), 256);
	s.hostName = clampUtf8(value.at("hostName").get<std::string>(), MAX_NAME_BYTES);
	for (const auto& c : value.at("teamColors"))
		s.teamColors.push_back({c.at(0).get<std::uint8_t>(), c.at(1).get<std::uint8_t>(), c.at(2).get<std::uint8_t>()});
	s.mapBytes = value.at("mapBytes").get<std::uint32_t>();
	if (s.mapBytes > MAX_MAP_BYTES)
		throw std::out_of_range("map too large");
	for (const auto& m : value.at("members"))
	{
		Member member;
		member.id = m.at("id").get<std::uint32_t>();
		member.name = clampUtf8(m.at("name").get<std::string>(), MAX_NAME_BYTES);
		member.seat = m.at("seat").get<int>();
		member.ready = m.at("ready").get<bool>();
		member.hasMap = m.at("hasMap").get<bool>();
		s.members.push_back(member);
	}
	s.started = value.at("started").get<bool>();
	return s;
}

const Member* RoomState::member(std::uint32_t id) const
{
	for (const auto& m : members)
		if (m.id == id)
			return &m;
	return nullptr;
}

const Member* RoomState::memberForSeat(int seat) const
{
	for (const auto& m : members)
		if (m.seat == seat)
			return &m;
	return nullptr;
}

std::string randomToken(std::size_t bytes)
{
	static std::random_device device;
	std::string out;
	for (std::size_t i = 0; i < bytes; ++i)
	{
		const unsigned v = device() & 255;
		out += "0123456789abcdef"[v >> 4];
		out += "0123456789abcdef"[v & 15];
	}
	return out;
}

std::uint64_t nowMicros()
{
	return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
		                                  std::chrono::steady_clock::now().time_since_epoch())
	                                      .count());
}

std::string clampUtf8(const std::string& text, std::size_t limit)
{
	if (text.size() <= limit)
		return text;
	std::size_t end = limit;
	while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)
		--end;
	return text.substr(0, end);
}
}
