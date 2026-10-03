// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "LanHost.h"
#include "NetWait.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <utility>

#include <FileManager.h>
#include <GzipUtil.h>
#include <Toolkit.h>

#include "AINames.h"
#include "Engine.h"
#include "GameHeader.h"
#include "MapCache.h"
#include "NetBroadcaster.h"
#include "NetworkConfig.h"
#include "Sha256.h"
#include "SimVersion.h"
#include "TurnMessages.h"

namespace Lan
{
namespace
{
constexpr std::size_t MAX_PEERS = 32;
constexpr unsigned FRAMES_PER_PEER_UPDATE = 512;
/// The longest the worker sleeps with nothing due: bounds the transports' own
/// timers (handshake, write and ping deadlines) and the discovery beacon.
constexpr std::uint64_t IDLE_WAIT_MICROS = 100000;

int seatCapacity(const RoomState& room)
{
	return static_cast<int>(room.setup.teams.size());
}
}

/// The host player's connection to the in-process relay: frames go straight into the
/// sequencer, and polling for frames pumps the host.
class LanHost::LocalTransport final : public Turn::TurnTransport
{
public:
	explicit LocalTransport(std::weak_ptr<LanHost> host) : host(std::move(host)) {}
	State state() override
	{
		auto h = host.lock();
		if (!h)
			return State::Disconnected;
		std::lock_guard<std::recursive_mutex> guard(h->mutex);
		if (!open || (h->closed && h->toLocal.empty()))
			return State::Disconnected;
		return State::Connected;
	}
	void connect() override
	{
		auto h = host.lock();
		if (!h)
			return;
		std::lock_guard<std::recursive_mutex> guard(h->mutex);
		if (h->closed)
			return;
		open = true;
		h->localConnected = true;
		if (h->relay && !h->localInRelay)
		{
			h->relay->onConnect(0, nowMicros());
			h->localInRelay = true;
		}
		// The relay has a presence change to broadcast.
		h->wakeWorker();
	}
	void close() override
	{
		open = false;
		auto h = host.lock();
		if (!h)
			return;
		std::lock_guard<std::recursive_mutex> guard(h->mutex);
		if (h->relay && h->localInRelay)
			h->relay->onDisconnect(0, nowMicros());
		h->localInRelay = false;
		h->localConnected = false;
		h->toLocal.clear();
		h->wakeWorker();
	}
	bool send(const std::vector<std::uint8_t>& payload) override
	{
		auto h = host.lock();
		if (!h || !open)
			return false;
		std::lock_guard<std::recursive_mutex> guard(h->mutex);
		h->localFrame(payload);
		// The relay may owe the guests a reply or a presence update now.
		h->wakeWorker();
		return true;
	}
	bool receive(std::vector<std::uint8_t>& payload) override
	{
		auto h = host.lock();
		if (!h || !open)
			return false;
		std::lock_guard<std::recursive_mutex> guard(h->mutex);
		if (h->toLocal.empty() && !h->threaded())
		{
			// The host's engine loop is the only loop while it plays, so polling the
			// relay for frames is what keeps the guests' connections serviced.
			const std::uint64_t now = nowMicros();
			if (now - lastPump >= 500)
			{
				lastPump = now;
				h->updateLocked();
			}
		}
		if (h->toLocal.empty())
			return false;
		payload = std::move(h->toLocal.front());
		h->toLocal.pop_front();
		return true;
	}

private:
	std::weak_ptr<LanHost> host;
	bool open = false;
	std::uint64_t lastPump = 0;
};

LanHost::LanHost(Options selected) : options(std::move(selected))
{
	auto& files = *GAGCore::Toolkit::getFileManager();
	if (options.mapFile.empty())
		options.mapFile = glob2PreferGzipReadPath(files, options.map.getFileName());
	std::string bytes;
	if (!Online::readMapBytes(options.mapFile, bytes))
		throw std::runtime_error("cannot read " + options.mapFile);
	mapHash = Online::toHex(Online::Sha256::of(bytes));
	if (!GAGCore::gzipCompress(bytes, 6, mapTransfer) || mapTransfer.size() > MAX_MAP_BYTES)
		throw std::runtime_error("cannot prepare " + options.mapFile + " for transfer");

	const bool save = options.map.getIsSavedGame();
	Online::MatchSetup& setup = room.setup;
	setup.simVersion = Online::currentSimVersion();
	setup.map.kind = Online::MapSource::Kind::Upload;
	setup.map.format = save ? Online::MapSource::Format::Save : Online::MapSource::Format::Map;
	setup.map.hash = mapHash;
	const int teams = options.map.getNumberOfTeams();
	if (teams < 1)
		throw std::runtime_error("the map has no teams");
	for (int t = 0; t < teams; ++t)
	{
		setup.teams.push_back({t, t});
		const auto& color = options.map.getBaseTeam(t).color;
		room.teamColors.push_back({color.r, color.g, color.b});
	}
	// The experiments, as the legacy lobby chose them: a save keeps its own, a map
	// takes the host's settings. A save also keeps its rules and alliances.
	GameHeader header;
	if (save)
	{
		const GameHeader saved = Engine::loadGameHeader(options.map.getFileName());
		try
		{
			const auto fromSave = Online::MatchSetup::fromGameHeader(saved, options.map, setup.map, setup.simVersion);
			setup.teams = fromSave.teams;
			setup.rules = fromSave.rules;
		}
		catch (const std::exception& error)
		{
			std::cerr << "LAN host: keeping default rules for this save (" << error.what() << ")" << std::endl;
		}
		header.setExperiments(saved.getExperiments());
	}
	else
		Engine::applyLocalExperiments(header, options.map);
	setup.experiments = header.getExperiments().keys();
	room.hostName = clampUtf8(options.hostName.empty() ? "Host" : options.hostName, MAX_NAME_BYTES);
	room.mapName = options.map.getMapName();
	room.mapBytes = static_cast<std::uint32_t>(mapTransfer.size());
	Online::SetupSeat seat;
	seat.seat = 0;
	seat.team = 0;
	seat.name = room.hostName;
	setup.seats.push_back(seat);
	Member host;
	host.id = 0;
	host.name = room.hostName;
	host.seat = 0;
	host.ready = true;
	host.hasMap = true;
	room.members.push_back(host);
	setup.validateSemantics();

	if (options.network)
	{
		NetworkConfig config = makeNetworkConfig(true);
		pairingEndpoint = config.lobbyEndpoint;
		if (options.port && options.port != config.lobby.port)
		{
			const std::string from = ":" + std::to_string(config.lobby.port) + "/";
			const auto at = pairingEndpoint.find(from);
			if (at == std::string::npos)
				throw std::runtime_error("unexpected LAN endpoint " + pairingEndpoint);
			pairingEndpoint.replace(at, from.size(), ":" + std::to_string(options.port) + "/");
			config.lobby.port = options.port;
		}
		listener = makeNetTransportListener(config.lobby);
		if (!listener->listening())
			throw std::runtime_error("cannot listen on port " + std::to_string(config.lobby.port));
		if (options.broadcast)
		{
			try
			{
				broadcaster = std::make_unique<NetBroadcaster>(config.discoveryId, pairingEndpoint);
			}
			catch (const std::exception& error)
			{
				std::cerr << "LAN host: discovery disabled (" << error.what() << ")" << std::endl;
			}
		}
		if (options.thread)
		{
			waker = std::make_unique<NetWaker>();
			worker = std::thread([this] { serve(); });
		}
	}
}

LanHost::~LanHost()
{
	stopThread();
	if (!closed)
		close("cancelled");
}

void LanHost::serve()
{
	// Service the network when there is something to do: a socket is ready, the
	// relay's next bundle or timer is due (TurnSequencer::nextWakeMicros), another
	// thread queued work (wakeWorker), or IDLE_WAIT_MICROS passed. Where the
	// transports cannot report readiness (Windows), netWait polls every millisecond.
	std::vector<NetWaitHandle> handles;
	while (!stopping)
	{
		std::uint64_t timeout = 0;
		bool supported = true;
		{
			std::lock_guard<std::recursive_mutex> guard(mutex);
			++wakeups;
			if (!updateLocked())
			{
				handles.clear();
				const std::uint64_t now = nowMicros();
				const std::uint64_t until = nextWakeLocked(now, handles, supported);
				timeout = until > now ? until - now : 0;
			}
		}
		if (timeout > 0)
			netWait(handles, waker.get(), timeout, supported);
		else
			std::this_thread::yield(); // let the host's own threads take the lock
	}
}

std::uint64_t LanHost::nextWakeLocked(std::uint64_t now, std::vector<NetWaitHandle>& handles, bool& supported)
{
	std::uint64_t wake = now + IDLE_WAIT_MICROS;
	auto watch = [&](NetWaitStatus status) {
		if (status == NetWaitStatus::Ready)
			wake = now;
		else if (status == NetWaitStatus::Unsupported)
			supported = false;
	};
	if (listener && !closed)
		watch(listener->waitHandles(handles));
	for (auto& entry : peers)
		watch(entry.second.link->waitHandles(handles));
	if (room.started && !relay && !closed)
		wake = std::min(wake, startRequestedAt + options.loadWaitMicros);
	if (relay)
		wake = std::min(wake, relay->nextWakeMicros());
	return wake;
}

void LanHost::wakeWorker()
{
	if (waker && worker.joinable() && worker.get_id() != std::this_thread::get_id())
		waker->wake();
}

void LanHost::stopThread()
{
	if (!worker.joinable() || worker.get_id() == std::this_thread::get_id())
		return;
	stopping = true;
	waker->wake();
	worker.join();
}

void LanHost::update()
{
	std::lock_guard<std::recursive_mutex> guard(mutex);
	updateLocked();
}

RoomState LanHost::state() const
{
	std::lock_guard<std::recursive_mutex> guard(mutex);
	return room;
}

bool LanHost::relayRunning() const
{
	std::lock_guard<std::recursive_mutex> guard(mutex);
	return static_cast<bool>(relay);
}

Turn::PresenceState LanHost::presence(int seat) const
{
	std::lock_guard<std::recursive_mutex> guard(mutex);
	return relay && seat >= 0 && seat < static_cast<int>(Turn::MAX_SEATS) ? relay->presence(static_cast<std::uint8_t>(seat))
	                                                                       : Turn::PresenceState::NotConnected;
}

std::uint32_t LanHost::horizon() const
{
	std::lock_guard<std::recursive_mutex> guard(mutex);
	return relay ? relay->horizon() : 0;
}

std::optional<std::uint32_t> LanHost::agreedChecksum(std::uint32_t tick) const
{
	std::lock_guard<std::recursive_mutex> guard(mutex);
	return relay ? relay->agreedChecksum(tick) : std::nullopt;
}

bool LanHost::updateLocked()
{
	const std::uint64_t now = nowMicros();
	bool busy = false;
	if (!closed)
	{
		busy = accept(now);
		if (broadcaster)
			broadcaster->update();
	}
	std::vector<Turn::PeerId> ids;
	for (const auto& entry : peers)
		ids.push_back(entry.first);
	std::vector<std::uint8_t> payload;
	for (Turn::PeerId id : ids)
	{
		auto it = peers.find(id);
		if (it == peers.end())
			continue;
		for (unsigned n = 0; n < FRAMES_PER_PEER_UPDATE && !it->second.closing && it->second.link->receive(payload); ++n)
		{
			// More may already be buffered (TLS records, queued completions): look again
			// before waiting.
			busy = true;
			handle(id, it->second, payload, now);
			it = peers.find(id);
			if (it == peers.end())
				break;
		}
		if (it != peers.end() && !it->second.closing && it->second.link->closed())
			dropPeer(id, true, now);
	}
	if (room.started && !relay && !closed)
	{
		const std::uint32_t humans = room.setup.humanSeatMask();
		if ((helloSeen & humans) == humans || now - startRequestedAt >= options.loadWaitMicros)
			startRelay(now);
	}
	if (relay)
	{
		relay->update(now);
		if (relay->matchOver())
			writeRecord();
	}
	for (auto it = peers.begin(); it != peers.end();)
	{
		it->second.link->flush();
		if (it->second.closing && (it->second.link->outboxEmpty() || it->second.link->closed()))
		{
			it->second.link->close();
			it = peers.erase(it);
		}
		else
			++it;
	}
	return busy;
}

bool LanHost::accept(std::uint64_t now)
{
	bool accepted = false;
	if (!listener)
		return accepted;
	while (auto transport = listener->accept())
	{
		accepted = true;
		if (peers.size() >= MAX_PEERS)
		{
			transport->close();
			continue;
		}
		const Turn::PeerId id = nextPeer++;
		Peer& peer = peers[id];
		peer.link = std::make_unique<LanLink>(std::move(transport));
		if (relay)
		{
			relay->onConnect(id, now);
			peer.inRelay = true;
		}
	}
	return accepted;
}

void LanHost::handle(Turn::PeerId id, Peer& peer, const std::vector<std::uint8_t>& payload, std::uint64_t now)
{
	if (payload.empty())
		return;
	const std::uint8_t type = payload[0];
	if (isTurnMessage(type))
	{
		handleTurn(id, peer, payload, now);
		return;
	}
	if (type == MSG_ROOM_JSON)
	{
		if (auto message = decodeJson(payload))
		{
			handleRoom(id, peer, *message, now);
			return;
		}
	}
	// A malformed frame, a map chunk sent to the host, or a legacy YOG client.
	dropPeer(id, true, now);
}

void LanHost::handleTurn(Turn::PeerId id, Peer& peer, const std::vector<std::uint8_t>& payload, std::uint64_t now)
{
	if (relay)
	{
		if (peer.inRelay)
			relay->onReceive(id, payload, now);
		return;
	}
	if (!room.started)
	{
		dropPeer(id, true, now);
		return;
	}
	if (payload[0] == Turn::MSG_HELLO)
		if (auto message = Turn::TurnCodec::decode(payload))
		{
			auto found = tickets.find(static_cast<const Turn::Hello&>(*message).ticket);
			if (found != tickets.end())
				helloSeen |= 1u << found->second;
		}
	early.emplace_back(id, payload);
}

void LanHost::handleRoom(Turn::PeerId id, Peer& peer, const nlohmann::json& message, std::uint64_t now)
{
	const std::string type = message["type"].get<std::string>();
	if (type == "hello")
	{
		if (peer.greeted)
			dropPeer(id, true, now);
		else
			handleHello(id, peer, message);
		return;
	}
	if (!peer.greeted)
	{
		dropPeer(id, true, now);
		return;
	}
	try
	{
		if (type == "mapRequest")
		{
			sendMapChunk(peer, message.at("offset").get<std::uint32_t>());
			return;
		}
		if (type == "leave")
		{
			dropPeer(id, true, now);
			return;
		}
		Member* member = nullptr;
		for (auto& m : room.members)
			if (peer.member && m.id == peer.member)
				member = &m;
		if (!member || room.started)
			return; // a rejoining guest after the start, or a late lobby message
		if (type == "setTeam")
		{
			const int seat = message.at("seat").get<int>();
			if (seat == member->seat)
				setSeatTeam(seat, message.at("team").get<int>());
		}
		else if (type == "ready")
		{
			member->ready = message.at("ready").get<bool>() && member->hasMap;
			changed = true;
			broadcastState();
		}
		else if (type == "mapReady")
		{
			if (message.at("hash").get<std::string>() == mapHash)
			{
				member->hasMap = true;
				changed = true;
				broadcastState();
			}
		}
		else if (type == "chat")
		{
			const std::string text = clampUtf8(message.at("text").get<std::string>(), MAX_CHAT_BYTES);
			if (!text.empty())
			{
				chatLines.push_back(member->name + ": " + text);
				broadcast({{"type", "chat"}, {"from", member->name}, {"text", text}});
			}
		}
	}
	catch (const std::exception&)
	{
		dropPeer(id, true, now);
	}
}

void LanHost::handleHello(Turn::PeerId id, Peer& peer, const nlohmann::json& message)
{
	std::string guestVersion, wanted;
	int roomProtocol = 0, turnProtocol = 0;
	try
	{
		roomProtocol = message.at("protocol").get<int>();
		turnProtocol = message.at("turnProtocol").get<int>();
		guestVersion = message.at("simVersion").get<std::string>();
		wanted = clampUtf8(message.at("name").get<std::string>(), MAX_NAME_BYTES);
	}
	catch (const std::exception&)
	{
		refuse(peer, "malformed");
		return;
	}
	const std::string hostVersion = room.setup.simVersion.key();
	if (roomProtocol != ROOM_PROTOCOL_VERSION || turnProtocol != Turn::PROTOCOL_VERSION || guestVersion != hostVersion)
	{
		refuse(peer, "version", "host " + hostVersion + ", guest " + guestVersion);
		return;
	}
	for (unsigned char c : wanted)
		if (c < 0x20 || c == 0x7f)
			wanted.clear();
	if (wanted.empty())
		wanted = "Guest";
	if (room.started)
	{
		// A guest whose game restarted takes its seat back by name, then reloads and
		// fast-forwards from the relay's turn log.
		for (const auto& seat : room.setup.seats)
		{
			if (!seat.human || seat.name != wanted || seat.seat == 0)
				continue;
			const auto presence = relay ? relay->presence(static_cast<std::uint8_t>(seat.seat))
			                            : Turn::PresenceState::NotConnected;
			if (presence != Turn::PresenceState::Reconnecting && presence != Turn::PresenceState::NotConnected)
				continue;
			peer.greeted = true;
			sendTo(peer, encodeJson({{"type", "start"},
			                         {"setup", room.setup.toJson()},
			                         {"seat", seat.seat},
			                         {"ticket", seatTickets[seat.seat]},
			                         {"mapName", room.mapName},
			                         {"mapBytes", room.mapBytes}}));
			chatLines.push_back(wanted + " is rejoining");
			return;
		}
		refuse(peer, "started");
		return;
	}
	if (static_cast<int>(room.setup.seats.size()) >= seatCapacity(room))
	{
		refuse(peer, "full");
		return;
	}
	Member member;
	member.id = nextMember++;
	member.name = uniqueName(wanted);
	Online::SetupSeat seat;
	seat.seat = static_cast<int>(room.setup.seats.size());
	seat.team = freeTeam();
	seat.name = member.name;
	room.setup.seats.push_back(seat);
	member.seat = seat.seat;
	room.members.push_back(member);
	peer.member = member.id;
	peer.greeted = true;
	sendTo(peer, encodeJson({{"type", "welcome"}, {"member", member.id}, {"name", member.name}}));
	chatLines.push_back(member.name + " joined");
	changed = true;
	broadcastState();
}

void LanHost::sendMapChunk(Peer& peer, std::uint32_t offset)
{
	if (offset >= mapTransfer.size())
		return;
	const std::size_t size = std::min(MAP_CHUNK_BYTES, mapTransfer.size() - offset);
	sendTo(peer, encodeMapChunk(mapHash, offset, static_cast<std::uint32_t>(mapTransfer.size()),
	                            reinterpret_cast<const std::uint8_t*>(mapTransfer.data()) + offset, size));
}

void LanHost::refuse(Peer& peer, const std::string& reason, const std::string& detail)
{
	sendTo(peer, encodeJson({{"type", "refuse"}, {"reason", reason}, {"detail", detail}}));
	peer.closing = true;
}

void LanHost::dropPeer(Turn::PeerId id, bool notifyRelay, std::uint64_t now)
{
	auto it = peers.find(id);
	if (it == peers.end())
		return;
	if (it->second.inRelay && notifyRelay && relay)
		relay->onDisconnect(id, now);
	const std::uint32_t member = it->second.member;
	it->second.link->close();
	peers.erase(it);
	if (member && !room.started)
		removeMemberSeat(member);
}

void LanHost::removeMemberSeat(std::uint32_t id)
{
	auto it = std::find_if(room.members.begin(), room.members.end(), [id](const Member& m) { return m.id == id; });
	if (it == room.members.end())
		return;
	const int seat = it->seat;
	chatLines.push_back(it->name + " left");
	room.members.erase(it);
	if (seat >= 0 && seat < static_cast<int>(room.setup.seats.size()))
	{
		room.setup.seats.erase(room.setup.seats.begin() + seat);
		for (auto& m : room.members)
			if (m.seat > seat)
				--m.seat;
	}
	renumberSeats();
	changed = true;
	broadcastState();
}

void LanHost::renumberSeats()
{
	for (std::size_t i = 0; i < room.setup.seats.size(); ++i)
		room.setup.seats[i].seat = static_cast<int>(i);
}

int LanHost::freeTeam() const
{
	const int teams = seatCapacity(room);
	for (int t = 0; t < teams; ++t)
		if (std::none_of(room.setup.seats.begin(), room.setup.seats.end(),
		                 [t](const Online::SetupSeat& s) { return s.team == t; }))
			return t;
	return static_cast<int>(room.setup.seats.size()) % teams;
}

std::string LanHost::uniqueName(const std::string& wanted) const
{
	auto taken = [this](const std::string& name) {
		return std::any_of(room.setup.seats.begin(), room.setup.seats.end(),
		                   [&](const Online::SetupSeat& s) { return s.name == name; });
	};
	if (!taken(wanted))
		return wanted;
	for (int n = 2;; ++n)
	{
		const std::string suffix = " (" + std::to_string(n) + ")";
		const std::string name = clampUtf8(wanted, MAX_NAME_BYTES - suffix.size()) + suffix;
		if (!taken(name))
			return name;
	}
}

void LanHost::setSeatTeam(int seat, int team)
{
	std::lock_guard<std::recursive_mutex> guard(mutex);
	if (room.started || seat < 0 || seat >= static_cast<int>(room.setup.seats.size()) || team < 0 ||
	    team >= seatCapacity(room))
		return;
	room.setup.seats[seat].team = team;
	changed = true;
	broadcastState();
}

void LanHost::kickSeat(int seat)
{
	std::lock_guard<std::recursive_mutex> guard(mutex);
	if (room.started || seat <= 0 || seat >= static_cast<int>(room.setup.seats.size()))
		return;
	if (!room.setup.seats[seat].human)
	{
		room.setup.seats.erase(room.setup.seats.begin() + seat);
		for (auto& m : room.members)
			if (m.seat > seat)
				--m.seat;
		renumberSeats();
		changed = true;
		broadcastState();
		return;
	}
	const Member* member = room.memberForSeat(seat);
	if (!member)
		return;
	const std::uint32_t id = member->id;
	for (auto& [peerId, peer] : peers)
		if (peer.member == id)
		{
			refuse(peer, "kicked");
			peer.member = 0;
		}
	removeMemberSeat(id);
}

void LanHost::addAI(AI::ImplementationID ai)
{
	std::lock_guard<std::recursive_mutex> guard(mutex);
	if (room.started || ai == AI::JAVASCRIPT || static_cast<int>(room.setup.seats.size()) >= seatCapacity(room))
		return;
	Online::SetupSeat seat;
	seat.seat = static_cast<int>(room.setup.seats.size());
	seat.human = false;
	seat.ai = ai == AI::NONE ? "none" : AINames::getCLIName(ai);
	seat.team = freeTeam();
	seat.name = clampUtf8(AINames::getAIText(ai), MAX_NAME_BYTES);
	if (seat.name.empty())
		seat.name = "AI";
	room.setup.seats.push_back(seat);
	changed = true;
	broadcastState();
}

void LanHost::applyOptions(const GameHeader& header)
{
	std::lock_guard<std::recursive_mutex> guard(mutex);
	if (room.started)
		return;
	try
	{
		const auto edited =
			Online::MatchSetup::fromGameHeader(header, options.map, room.setup.map, room.setup.simVersion);
		room.setup.rules = edited.rules;
		room.setup.teams = edited.teams;
		room.setup.experiments = edited.experiments;
		changed = true;
		broadcastState();
	}
	catch (const std::exception& error)
	{
		std::cerr << "LAN host: options not applied (" << error.what() << ")" << std::endl;
	}
}

void LanHost::chat(const std::string& text)
{
	std::lock_guard<std::recursive_mutex> guard(mutex);
	const std::string line = clampUtf8(text, MAX_CHAT_BYTES);
	if (line.empty())
		return;
	chatLines.push_back(room.hostName + ": " + line);
	broadcast({{"type", "chat"}, {"from", room.hostName}, {"text", line}});
}

bool LanHost::canStart() const
{
	std::lock_guard<std::recursive_mutex> guard(mutex);
	if (room.started || closed)
		return false;
	return std::all_of(room.members.begin(), room.members.end(),
	                   [](const Member& m) { return m.id == 0 || (m.ready && m.hasMap); });
}

LanHost::LocalStart LanHost::start()
{
	std::lock_guard<std::recursive_mutex> guard(mutex);
	if (!canStart())
		throw std::logic_error("the LAN room is not ready to start");
	std::random_device device;
	do
		room.setup.seed = device();
	while (room.setup.seed == 0);
	for (const auto& seat : room.setup.seats)
		if (seat.human)
		{
			const std::string ticket = randomToken();
			tickets[ticket] = seat.seat;
			seatTickets[seat.seat] = ticket;
		}
	room.setup.validateSemantics();
	room.started = true;
	startRequestedAt = nowMicros();
	for (auto& [id, peer] : peers)
	{
		const Member* member = peer.member ? room.member(peer.member) : nullptr;
		if (!member || peer.closing)
			continue;
		sendTo(peer, encodeJson({{"type", "start"},
		                         {"setup", room.setup.toJson()},
		                         {"seat", member->seat},
		                         {"ticket", seatTickets[member->seat]},
		                         {"mapName", room.mapName},
		                         {"mapBytes", room.mapBytes}}));
	}
	changed = true;
	broadcastState();
	LocalStart local;
	local.setup = room.setup;
	local.seat = room.members.front().seat;
	local.ticket = seatTickets[local.seat];
	local.mapFile = options.mapFile;
	local.transport = std::make_shared<LocalTransport>(weak_from_this());
	return local;
}

void LanHost::startRelay(std::uint64_t now)
{
	relay = std::make_unique<Turn::TurnSequencer>(
		options.sequencer, room.setup.humanSeatMask(),
		[this](const std::string& ticket) {
			auto found = tickets.find(ticket);
			return found == tickets.end() ? -1 : found->second;
		},
		static_cast<Turn::SequencerOutput&>(*this), now);
	if (localConnected)
	{
		relay->onConnect(0, now);
		localInRelay = true;
	}
	for (auto& [id, peer] : peers)
		if (!peer.closing)
		{
			relay->onConnect(id, now);
			peer.inRelay = true;
		}
	auto frames = std::move(early);
	early.clear();
	for (auto& [id, payload] : frames)
	{
		if (id == 0)
		{
			if (localInRelay)
				relay->onReceive(0, payload, now);
		}
		else if (peers.count(id))
			relay->onReceive(id, payload, now);
	}
}

void LanHost::localFrame(const std::vector<std::uint8_t>& payload)
{
	const std::uint64_t now = nowMicros();
	if (relay)
	{
		if (localInRelay)
			relay->onReceive(0, payload, now);
		return;
	}
	if (!room.started || payload.empty())
		return;
	if (payload[0] == Turn::MSG_HELLO)
		if (auto message = Turn::TurnCodec::decode(payload))
		{
			auto found = tickets.find(static_cast<const Turn::Hello&>(*message).ticket);
			if (found != tickets.end())
				helloSeen |= 1u << found->second;
		}
	early.emplace_back(0, payload);
}

void LanHost::send(Turn::PeerId peer, const std::vector<std::uint8_t>& payload)
{
	if (peer == 0)
	{
		toLocal.push_back(payload);
		return;
	}
	auto it = peers.find(peer);
	if (it != peers.end() && !it->second.closing)
	{
		it->second.link->send(payload);
		wakeWorker();
	}
}

void LanHost::close(Turn::PeerId peer)
{
	if (peer == 0)
	{
		localInRelay = false;
		return;
	}
	auto it = peers.find(peer);
	if (it != peers.end())
	{
		it->second.inRelay = false;
		it->second.closing = true;
		wakeWorker();
	}
}

void LanHost::sendTo(Peer& peer, const std::vector<std::uint8_t>& payload)
{
	if (!peer.closing)
	{
		peer.link->send(payload);
		// Writes progress when the worker polls the connection.
		wakeWorker();
	}
}

void LanHost::broadcast(const nlohmann::json& message)
{
	const auto payload = encodeJson(message);
	for (auto& [id, peer] : peers)
		if (peer.greeted)
			sendTo(peer, payload);
}

void LanHost::broadcastState()
{
	nlohmann::json message = room.toJson();
	message["type"] = "state";
	broadcast(message);
}

void LanHost::close(const std::string& reason)
{
	stopThread();
	std::lock_guard<std::recursive_mutex> guard(mutex);
	if (closed)
		return;
	closed = true;
	const std::uint64_t now = nowMicros();
	if (relay)
	{
		relay->finish(now);
		writeRecord();
	}
	// Every connection hears it, including guests that reconnected during play
	// (their new connection carries only turn messages before this one).
	const auto notice = encodeJson({{"type", "closed"}, {"reason", reason}});
	for (auto& [id, peer] : peers)
	{
		sendTo(peer, notice);
		peer.closing = true;
	}
	if (listener)
		listener->close();
	broadcaster.reset();
	// Give the final frames a moment to leave; guests treat a lost host the same way.
	const std::uint64_t deadline = now + 300000;
	while (!peers.empty() && nowMicros() < deadline)
	{
		for (auto it = peers.begin(); it != peers.end();)
		{
			it->second.link->flush();
			if (it->second.link->outboxEmpty() || it->second.link->closed())
			{
				it->second.link->close();
				it = peers.erase(it);
			}
			else
				++it;
		}
	}
	for (auto& [id, peer] : peers)
		peer.link->close();
	peers.clear();
	changed = true;
}

std::vector<std::string> LanHost::takeChat()
{
	std::lock_guard<std::recursive_mutex> guard(mutex);
	return std::exchange(chatLines, {});
}

bool LanHost::takeChanged()
{
	std::lock_guard<std::recursive_mutex> guard(mutex);
	return std::exchange(changed, false);
}

std::size_t LanHost::guestCount() const
{
	std::lock_guard<std::recursive_mutex> guard(mutex);
	return room.members.size() - 1;
}

std::string networkSummaryPath(const std::string& recordPath)
{
	const std::string suffix = ".g2mr";
	std::string base = recordPath;
	if (base.size() > suffix.size() && base.compare(base.size() - suffix.size(), suffix.size(), suffix) == 0)
		base.resize(base.size() - suffix.size());
	return base + ".network.json";
}

nlohmann::json LanHost::networkSummary() const
{
	std::lock_guard<std::recursive_mutex> guard(mutex);
	return relay ? relay->networkSummary() : nlohmann::json();
}

void LanHost::writeRecord()
{
	if (recordWritten || !relay || options.recordPath.empty())
		return;
	recordWritten = true;
	try
	{
		Online::Sha256::Digest digest;
		Online::parseSha256Hex(mapHash, digest);
		std::array<std::uint8_t, 32> hash{};
		std::copy(digest.begin(), digest.end(), hash.begin());
		relay->buildRecord("lan-" + randomToken(8), room.setup.simVersion.key(), room.setup.dump(), hash)
			.writeFile(options.recordPath);
	}
	catch (const std::exception& error)
	{
		std::cerr << "LAN host: cannot write the match record (" << error.what() << ")" << std::endl;
	}
	// The relay's per-seat network summary (RelayNetworkSummary), next to the record.
	const std::string path = networkSummaryPath(options.recordPath);
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out << relay->networkSummary().dump() << '\n';
	if (!out)
		std::cerr << "LAN host: cannot write the network summary " << path << std::endl;
}
}
