// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "LanRoom.h"

#include <iostream>

#include <FileManager.h>
#include <FormatableString.h>
#include <StringTable.h>
#include <Toolkit.h>

#include "Engine.h"
#include "ExperimentalFeatures.h"

namespace Lan
{
namespace
{
std::string text(const char* key)
{
	return GAGCore::Toolkit::getStringTable()->getString(key);
}
}

std::shared_ptr<LanRoom> LanRoom::host(LanHost::Options options)
{
	if (options.recordPath.empty() && options.network)
	{
		auto* files = GAGCore::Toolkit::getFileManager();
		if (files && files->getDirCount())
			options.recordPath = files->getDir(0) + "/replays/lan-last.g2mr";
	}
	std::shared_ptr<LanRoom> room(new LanRoom);
	room->hostRoom = std::make_shared<LanHost>(std::move(options));
	room->hostState = room->hostRoom->state();
	return room;
}

std::shared_ptr<LanRoom> LanRoom::join(LanClient::Options options)
{
	std::shared_ptr<LanRoom> room(new LanRoom);
	room->guest = std::make_unique<LanClient>(std::move(options));
	return room;
}

LanRoom::~LanRoom()
{
	if (transport)
		transport->close();
	if (hostRoom)
		hostRoom->close(launched ? "host-left" : "cancelled");
	else if (guest && !finished)
		guest->leave();
}

const RoomState* LanRoom::room() const
{
	return hostRoom ? &hostState : guest->state();
}

int LanRoom::localSeat() const
{
	return hostRoom ? 0 : guest->localSeat();
}

void LanRoom::push(Event::Kind kind, std::string message, int code)
{
	Event event;
	event.kind = kind;
	event.text = std::move(message);
	event.code = code;
	events.push_back(std::move(event));
}

void LanRoom::finish(int code, std::string message)
{
	if (finished)
		return;
	finished = true;
	ending = std::move(message);
	push(Event::Finished, ending, code);
}

std::string LanRoom::describeClose(const std::string& reason, const std::string& detail) const
{
	if (reason == "kicked")
		return text("[You where kicked from the game]");
	if (reason == "cancelled")
		return text("[The host has cancelled the game]");
	if (reason == "host-left")
		return text("[lan host left]");
	if (reason == "lost")
		return text("[lan host lost]");
	if (reason == "unreachable")
		return detail.empty() ? text("[lan connection unavailable]")
		                      : std::string(GAGCore::FormattableString(text("[lan connection verification failed %0]")).arg(detail));
	if (reason == "version")
		return GAGCore::FormattableString(text("[lan version mismatch %0]")).arg(detail);
	if (reason == "full")
		return text("[Can't join game, game is full]");
	if (reason == "started")
		return text("[Can't join game, game has started]");
	if (reason == "left")
		return {};
	return GAGCore::FormattableString(text("[lan connection verification failed %0]")).arg(detail.empty() ? reason : detail);
}

void LanRoom::update()
{
	if (finished)
		return;
	if (hostRoom)
	{
		if (!hostRoom->threaded())
			hostRoom->update();
		for (auto& line : hostRoom->takeChat())
			push(Event::Chat, std::move(line));
		if (hostRoom->takeChanged())
		{
			hostState = hostRoom->state();
			push(Event::Changed);
		}
		return;
	}
	guest->update();
	for (auto& line : guest->takeChat())
		push(Event::Chat, std::move(line));
	if (guest->takeChanged())
		push(Event::Changed);
	if (guest->phase() == LanClient::Phase::Closed)
	{
		const std::string& reason = guest->closeReason();
		int code = ServerDisconnected;
		if (reason == "kicked")
			code = Kicked;
		else if (reason == "cancelled" || reason == "host-left")
			code = GameCancelled;
		else if (reason == "version" || reason == "full" || reason == "started")
			code = GameRefused;
		else if (reason == "left")
			code = Cancelled;
		finish(code, describeClose(reason, guest->closeDetail()));
		return;
	}
	if (!launched && guest->startReady())
	{
		pending = guest->takeStart();
		launched = true;
		push(Event::Launch);
	}
}

std::optional<RoomBackend::Event> LanRoom::takeEvent()
{
	if (events.empty())
		return std::nullopt;
	Event event = std::move(events.front());
	events.pop_front();
	return event;
}

bool LanRoom::connecting() const
{
	return guest && guest->phase() == LanClient::Phase::Connecting;
}

bool LanRoom::lobbyReady() const
{
	if (hostRoom)
		return true;
	return guest->phase() != LanClient::Phase::Connecting && guest->state();
}

std::string LanRoom::mapName() const
{
	const RoomState* r = room();
	return r ? r->mapName : std::string();
}

int LanRoom::teamCount() const
{
	const RoomState* r = room();
	return r ? static_cast<int>(r->setup.teams.size()) : 0;
}

std::optional<std::array<std::uint8_t, 3>> LanRoom::teamColor(int team) const
{
	const RoomState* r = room();
	if (!r || team < 0 || team >= static_cast<int>(r->teamColors.size()))
		return std::nullopt;
	return r->teamColors[team];
}

std::vector<RoomBackend::Slot> LanRoom::slots() const
{
	std::vector<Slot> result;
	const RoomState* r = room();
	if (!r)
		return result;
	const int local = localSeat();
	for (const auto& seat : r->setup.seats)
	{
		Slot slot;
		slot.index = seat.seat;
		slot.name = seat.name;
		slot.team = seat.team;
		slot.ai = !seat.human;
		if (seat.human)
		{
			const Member* member = r->memberForSeat(seat.seat);
			slot.ready = member && (member->id == 0 || (member->ready && member->hasMap));
		}
		slot.local = seat.seat == local;
		result.push_back(slot);
	}
	for (int open = static_cast<int>(r->setup.seats.size()); open < teamCount(); ++open)
	{
		Slot slot;
		slot.index = open;
		slot.open = true;
		result.push_back(slot);
	}
	return result;
}

void LanRoom::changeTeam(int slot, int team)
{
	if (hostRoom)
		hostRoom->setSeatTeam(slot, team);
	else if (slot == guest->localSeat())
		guest->setTeam(team);
}

void LanRoom::kick(int slot)
{
	if (hostRoom)
		hostRoom->kickSeat(slot);
}

void LanRoom::addAI(AI::ImplementationID ai)
{
	if (hostRoom)
		hostRoom->addAI(ai);
}

void LanRoom::setReady(bool value)
{
	ready = value;
	if (guest)
		guest->setReady(value);
}

bool LanRoom::everyoneReady() const
{
	const RoomState* r = room();
	if (!r)
		return false;
	for (const auto& m : r->members)
		if (m.id != 0 && !(m.ready && m.hasMap))
			return false;
	return true;
}

bool LanRoom::canStart() const
{
	return hostRoom && !launched && hostRoom->canStart();
}

void LanRoom::start()
{
	if (!canStart())
		return;
	auto local = hostRoom->start();
	LanClient::StartInfo info;
	info.setup = std::move(local.setup);
	info.seat = local.seat;
	info.ticket = std::move(local.ticket);
	info.mapFile = std::move(local.mapFile);
	info.transport = std::move(local.transport);
	pending = std::move(info);
	launched = true;
	push(Event::Launch);
}

bool LanRoom::starting() const
{
	const RoomState* r = room();
	return launched || (r && r->started);
}

void LanRoom::sendChat(const std::string& line)
{
	if (hostRoom)
		hostRoom->chat(line);
	else
		guest->chat(line);
}

int LanRoom::downloadPercent() const
{
	if (!guest)
		return -1;
	const int percent = guest->downloadPercent();
	return percent >= 0 && percent < 100 ? percent : -1;
}

std::string LanRoom::experimentsLabel() const
{
	const RoomState* r = room();
	if (!r || r->setup.experiments.empty())
		return {};
	return experimentLabelList(ExperimentSet::fromKeys(r->setup.experiments));
}

std::string LanRoom::shareText() const
{
	return hostRoom ? hostRoom->pairing() : std::string();
}

GameHeader* LanRoom::optionsHeader()
{
	const RoomState* r = room();
	if (!r)
		return nullptr;
	try
	{
		if (hostRoom)
			optionMap = hostRoom->mapHeader();
		else if (guest->hasMap())
			optionMap = Engine::loadMapHeader(guest->mapFile());
		else
			return nullptr;
		options = r->setup.toGameHeader(optionMap);
		return &options;
	}
	catch (const std::exception& error)
	{
		std::cerr << "LAN room: options unavailable (" << error.what() << ")" << std::endl;
		return nullptr;
	}
}

MapHeader* LanRoom::optionsMap()
{
	return &optionMap;
}

void LanRoom::optionsChanged()
{
	if (hostRoom)
		hostRoom->applyOptions(options);
}

void LanRoom::leave()
{
	if (hostRoom)
		hostRoom->close(launched ? "host-left" : "cancelled");
	else
		guest->leave();
	finished = true;
}

GAGCore::CooperativeTask LanRoom::initGame(Engine& engine)
{
	Engine::TurnMatchStart start;
	start.networkKind = "lan";
	if (pending)
	{
		start.setup = pending->setup;
		start.mapFile = pending->mapFile;
		start.localSeat = pending->seat;
		start.transport = pending->transport;
		start.config.ticket = pending->ticket;
		transport = pending->transport;
	}
	return engine.initTurnMatchTask(std::move(start));
}

void LanRoom::gameStarted(bool running)
{
	if (running)
		return;
	if (transport)
		transport->close();
	leave();
	finished = false;
	finish(Cancelled, {});
}

void LanRoom::gameEnded(bool)
{
	if (transport)
		transport->close();
	if (hostRoom)
	{
		hostRoom->close("host-left");
		finish(StartedGame, {});
		return;
	}
	// The guest's game ended: by itself, or because the host left or vanished.
	const std::string reason = guest->endReason();
	finish(reason.empty() ? StartedGame : GameCancelled, reason.empty() ? std::string() : describeClose(reason, {}));
}
}
