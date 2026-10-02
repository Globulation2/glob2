// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "LanClient.h"

#include <algorithm>
#include <chrono>
#include <thread>
#include <iostream>

#include <GzipUtil.h>

#include "MapCache.h"
#include "OnlineServices.h"
#include "OnlineStorage.h"
#include "SimVersion.h"
#include "TurnMessages.h"

namespace Lan
{
struct LanClient::Connection
{
	std::string endpoint;
	std::unique_ptr<LanLink> link;
	std::deque<std::vector<std::uint8_t>> turnFrames;
	std::deque<std::vector<std::uint8_t>> roomFrames;
	/// Set when the host ends the game ("host-left", "cancelled") or cannot be reached.
	std::string endReason;
	std::uint64_t lastHeard = 0;

	void pump()
	{
		if (!link)
			return;
		std::vector<std::uint8_t> payload;
		while (link->receive(payload))
		{
			lastHeard = nowMicros();
			if (payload.empty())
				continue;
			if (isTurnMessage(payload[0]))
				turnFrames.push_back(std::move(payload));
			else if (isRoomMessage(payload[0]))
				roomFrames.push_back(std::move(payload));
			else
			{
				link->close();
				return;
			}
		}
	}
};

namespace
{
/// The guest's connection to the relay in the host. It shares the room connection,
/// reconnects to the same pinned endpoint after a loss, and turns the host leaving
/// (or never coming back) into a Reject, which ends the session cleanly.
class GuestTurnTransport final : public Turn::TurnTransport
{
public:
	GuestTurnTransport(std::shared_ptr<LanClient::Connection> connection, std::uint64_t giveUp, std::uint64_t silence)
		: connection(std::move(connection)), giveUp(giveUp), silence(silence)
	{
	}

	State state() override
	{
		auto& c = *connection;
		if (rejectQueued && !rejectDelivered)
			return State::Connected;
		c.pump();
		route();
		if (!c.endReason.empty() && !rejectDelivered)
		{
			rejectQueued = true;
			return State::Connected;
		}
		if (!c.link || c.link->closed())
			return State::Disconnected;
		const auto s = c.link->state();
		if (s == NetTransport::State::Connecting)
			return State::Connecting;
		lostSince = 0;
		// A host that vanished without closing the socket: nothing arrives at all.
		if (heard && nowMicros() - c.lastHeard > silence)
		{
			std::cerr << "LAN guest: the host has been silent; reconnecting" << std::endl;
			c.link->close();
			return State::Disconnected;
		}
		return State::Connected;
	}

	void connect() override
	{
		auto& c = *connection;
		if (c.link && !c.link->closed())
			return;
		const std::uint64_t now = nowMicros();
		if (!lostSince)
			lostSince = now;
		else if (now - lostSince > giveUp)
		{
			c.endReason = "lost";
			return;
		}
		c.link = LanLink::open(c.endpoint);
		c.lastHeard = now;
		heard = false;
	}

	void close() override
	{
		auto& link = connection->link;
		if (!link)
			return;
		// Give the session's last frames (its Quit) a moment to leave, so the host
		// marks the seat left at once instead of waiting out the grace period.
		const std::uint64_t deadline = nowMicros() + 200000;
		while (link->connected() && !link->outboxEmpty() && nowMicros() < deadline)
			link->flush();
		for (int i = 0; i < 20 && link->connected(); ++i)
		{
			link->state();
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		link->close();
	}

	bool send(const std::vector<std::uint8_t>& payload) override
	{
		auto& c = *connection;
		return c.link && !rejectQueued && c.link->send(payload);
	}

	void flush() override
	{
		auto& c = *connection;
		if (c.link && !rejectQueued)
			c.link->flush();
	}

	bool receive(std::vector<std::uint8_t>& payload) override
	{
		auto& c = *connection;
		if (rejectQueued)
		{
			if (rejectDelivered)
				return false;
			// Deliver what the host sent before it left, then the end of the game.
			if (!c.turnFrames.empty())
			{
				payload = std::move(c.turnFrames.front());
				c.turnFrames.pop_front();
				return true;
			}
			Turn::Reject reject;
			reject.reason = Turn::RejectReason::MatchOver;
			reject.detail = c.endReason;
			payload = Turn::TurnCodec::encode(reject);
			rejectDelivered = true;
			return true;
		}
		if (c.turnFrames.empty())
		{
			c.pump();
			route();
		}
		if (c.turnFrames.empty())
			return false;
		heard = true;
		payload = std::move(c.turnFrames.front());
		c.turnFrames.pop_front();
		return true;
	}

private:
	/// Room messages during play: only the host's "closed" matters.
	void route()
	{
		auto& c = *connection;
		while (!c.roomFrames.empty())
		{
			auto message = decodeJson(c.roomFrames.front());
			c.roomFrames.pop_front();
			if (message && (*message)["type"] == "closed")
			{
				const auto& why = (*message)["reason"];
				c.endReason = why.is_string() ? why.get<std::string>() : "host-left";
			}
		}
	}

	std::shared_ptr<LanClient::Connection> connection;
	std::uint64_t giveUp, silence;
	std::uint64_t lostSince = 0;
	bool heard = false;
	bool rejectQueued = false;
	bool rejectDelivered = false;
};
}

LanClient::LanClient(Options selected) : options(std::move(selected)), connection(std::make_shared<Connection>())
{
	options.name = clampUtf8(options.name, MAX_NAME_BYTES);
	if (options.name.empty())
		options.name = "Guest";
	connection->endpoint = options.endpoint;
	connection->link = LanLink::open(options.endpoint);
}

LanClient::~LanClient() = default;

void LanClient::close(const std::string& why, const std::string& text)
{
	if (currentPhase == Phase::Closed)
		return;
	currentPhase = Phase::Closed;
	reason = why;
	detail = text;
	changed = true;
}

void LanClient::send(const nlohmann::json& message)
{
	if (connection->link)
		connection->link->send(encodeJson(message));
}

void LanClient::update()
{
	if (currentPhase == Phase::Closed)
		return;
	if (currentPhase == Phase::Started && !start)
		return; // the engine owns the connection now
	auto& c = *connection;
	if (c.link && c.link->connected() && !helloSent)
	{
		send({{"type", "hello"},
		      {"protocol", ROOM_PROTOCOL_VERSION},
		      {"turnProtocol", Turn::PROTOCOL_VERSION},
		      {"simVersion", Online::currentSimVersion().key()},
		      {"name", options.name}});
		helloSent = true;
	}
	c.pump();
	while (!c.roomFrames.empty() && currentPhase != Phase::Closed)
	{
		auto payload = std::move(c.roomFrames.front());
		c.roomFrames.pop_front();
		if (payload[0] == MSG_MAP_CHUNK)
		{
			if (auto chunk = decodeMapChunk(payload))
				handleChunk(*chunk);
			else
				close("malformed");
		}
		else if (auto message = decodeJson(payload))
		{
			try
			{
				handleRoom(*message);
			}
			catch (const std::exception& error)
			{
				close("malformed", error.what());
			}
		}
		else
			close("malformed");
	}
	if (currentPhase != Phase::Closed && (!c.link || c.link->closed()))
		close(helloSent ? "lost" : "unreachable", c.link ? c.link->error() : std::string());
	if (currentPhase == Phase::Closed && c.link)
		c.link->close();
}

void LanClient::handleRoom(const nlohmann::json& message)
{
	const std::string type = message.at("type").get<std::string>();
	if (type == "welcome")
	{
		member = message.at("member").get<std::uint32_t>();
		options.name = message.at("name").get<std::string>();
		currentPhase = Phase::Lobby;
		changed = true;
	}
	else if (type == "refuse")
		close(message.at("reason").get<std::string>(), message.value("detail", std::string()));
	else if (type == "closed")
		close(message.at("reason").get<std::string>());
	else if (type == "chat")
		chatLines.push_back(message.at("from").get<std::string>() + ": " + message.at("text").get<std::string>());
	else if (type == "state")
	{
		room = RoomState::fromJson(message);
		const auto& map = room->setup.map;
		ensureMap(map.hash, room->mapBytes, map.format == Online::MapSource::Format::Save);
		changed = true;
	}
	else if (type == "start")
	{
		StartInfo info;
		info.setup = Online::MatchSetup::fromJson(message.at("setup"));
		info.seat = message.at("seat").get<int>();
		info.ticket = message.at("ticket").get<std::string>();
		if (info.seat < 0 || info.seat >= static_cast<int>(info.setup.seats.size()) || !info.setup.seats[info.seat].human)
			throw std::runtime_error("invalid seat in start");
		const auto& map = info.setup.map;
		ensureMap(map.hash, message.at("mapBytes").get<std::uint32_t>(), map.format == Online::MapSource::Format::Save);
		start = std::move(info);
		currentPhase = Phase::Started;
		changed = true;
	}
}

Online::MapCache& LanClient::maps()
{
	if (options.cacheDirectory.empty())
		return Online::services().maps;
	if (!privateMaps)
	{
		privateStorage = Online::makeDirectoryStorage(options.cacheDirectory);
		privateMaps = std::make_unique<Online::MapCache>(
			*privateStorage, [](HttpFetch::Request) -> std::unique_ptr<HttpFetch::Fetch> { return nullptr; });
	}
	return *privateMaps;
}

void LanClient::ensureMap(const std::string& hash, std::uint32_t bytes, bool savedGame)
{
	if (hash == cachedHash || hash == downloadHash)
		return;
	cachedMap.clear();
	cachedHash.clear();
	const std::string found = maps().path(hash).value_or("");
	if (!found.empty())
	{
		try
		{
			Online::MatchSetup probe;
			probe.map.hash = hash;
			probe.map.kind = savedGame ? Online::MapSource::Kind::Upload : Online::MapSource::Kind::Catalog;
			probe.map.format = savedGame ? Online::MapSource::Format::Save : Online::MapSource::Format::Map;
			cachedMap = Online::resolveMatchMap(probe, found);
			cachedHash = hash;
			send({{"type", "mapReady"}, {"hash", hash}});
			downloadHash.clear();
			return;
		}
		catch (const std::exception& error)
		{
			std::cerr << "LAN guest: ignoring cached map " << found << " (" << error.what() << ")" << std::endl;
		}
	}
	if (!bytes || bytes > MAX_MAP_BYTES)
	{
		close("malformed", "invalid map size");
		return;
	}
	downloadHash = hash;
	downloadSave = savedGame;
	downloadTotal = bytes;
	downloadBytes.assign(bytes, '\0');
	chunkDone.assign((bytes + MAP_CHUNK_BYTES - 1) / MAP_CHUNK_BYTES, false);
	nextRequest = 0;
	received = 0;
	requestChunks();
}

void LanClient::requestChunks()
{
	const std::uint32_t window = static_cast<std::uint32_t>(MAP_WINDOW_CHUNKS * MAP_CHUNK_BYTES);
	while (nextRequest < downloadTotal && nextRequest < received + window)
	{
		send({{"type", "mapRequest"}, {"offset", nextRequest}});
		nextRequest += static_cast<std::uint32_t>(MAP_CHUNK_BYTES);
	}
}

void LanClient::handleChunk(const MapChunk& chunk)
{
	if (chunk.hash != downloadHash || chunk.total != downloadTotal || chunk.offset % MAP_CHUNK_BYTES)
		return;
	const std::size_t index = chunk.offset / MAP_CHUNK_BYTES;
	const std::size_t expected = std::min<std::size_t>(MAP_CHUNK_BYTES, downloadTotal - chunk.offset);
	if (index >= chunkDone.size() || chunkDone[index] || chunk.bytes.size() != expected)
	{
		close("malformed", "unexpected map chunk");
		return;
	}
	std::copy(chunk.bytes.begin(), chunk.bytes.end(), downloadBytes.begin() + chunk.offset);
	chunkDone[index] = true;
	received += static_cast<std::uint32_t>(chunk.bytes.size());
	changed = true;
	if (received < downloadTotal)
	{
		requestChunks();
		return;
	}
	std::string contents, error;
	if (!GAGCore::gzipDecompress(downloadBytes, contents, MAX_MAP_INFLATED_BYTES))
	{
		close("malformed", "the map transfer is corrupt");
		return;
	}
	std::string path;
	if (maps().insert(downloadHash, contents, &error, downloadSave))
		path = maps().path(downloadHash).value_or("");
	if (path.empty())
	{
		close("malformed", error.empty() ? "cannot store the map" : error);
		return;
	}
	cachedMap = path;
	cachedHash = downloadHash;
	send({{"type", "mapReady"}, {"hash", downloadHash}});
	if (wantReady && currentPhase == Phase::Lobby)
		send({{"type", "ready"}, {"ready", true}});
	downloadHash.clear();
	downloadBytes.clear();
	downloadBytes.shrink_to_fit();
}

int LanClient::downloadPercent() const
{
	if (downloadHash.empty() || !downloadTotal)
		return -1;
	return static_cast<int>(std::uint64_t(received) * 100 / downloadTotal);
}

int LanClient::localSeat() const
{
	if (start)
		return start->seat;
	if (!room)
		return -1;
	const Member* m = room->member(member);
	return m ? m->seat : -1;
}

void LanClient::setTeam(int team)
{
	const int seat = localSeat();
	if (currentPhase == Phase::Lobby && seat >= 0)
		send({{"type", "setTeam"}, {"seat", seat}, {"team", team}});
}

void LanClient::setReady(bool ready)
{
	// The host accepts readiness only once the map is here; ask again when it is.
	wantReady = ready;
	if (currentPhase == Phase::Lobby)
		send({{"type", "ready"}, {"ready", ready}});
}

void LanClient::chat(const std::string& text)
{
	const std::string line = clampUtf8(text, MAX_CHAT_BYTES);
	if (currentPhase == Phase::Lobby && !line.empty())
		send({{"type", "chat"}, {"text", line}});
}

void LanClient::leave()
{
	if (connection->link && currentPhase != Phase::Closed)
	{
		send({{"type", "leave"}});
		connection->link->flush();
	}
	close("left");
	if (connection->link)
		connection->link->close();
}

std::optional<LanClient::StartInfo> LanClient::takeStart()
{
	if (!startReady())
		return std::nullopt;
	StartInfo info = std::move(*start);
	start.reset();
	info.mapFile = cachedMap;
	info.transport = std::make_shared<GuestTurnTransport>(connection, options.hostLossGiveUpMicros, options.silenceMicros);
	if (options.wrapTransport)
		info.transport = options.wrapTransport(info.transport);
	return info;
}

std::string LanClient::endReason() const
{
	return connection->endReason;
}

void LanClient::dropConnection()
{
	if (connection->link)
		connection->link->close();
}
}
