// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "YogRoom.h"

#include "Engine.h"
#include "ExperimentalFeatures.h"
#include "MultiplayerGameEvent.h"
#include "Team.h"
#include "YOGMessage.h"
#include "YOGServer.h"
#include "ui/FrontendUI.h"

YogRoom::YogRoom(std::shared_ptr<MultiplayerGame> game, std::shared_ptr<YOGClient> client,
                 std::shared_ptr<IRCTextMessageHandler> ircChat)
	: game(std::move(game)), client(std::move(client)), ircChat(std::move(ircChat)),
	  gameChat(new YOGClientChatChannel(YOG_CHAT_CHANNEL_NONE, this->client))
{
	this->game->addEventListener(this);
	gameChat->addListener(this);
}

YogRoom::~YogRoom()
{
	game->removeEventListener(this);
	gameChat->removeListener(this);
}

void YogRoom::push(Event::Kind kind, std::string text, int code)
{
	Event event;
	event.kind = kind;
	event.text = std::move(text);
	event.code = code;
	events.push_back(std::move(event));
}

void YogRoom::update()
{
	game->update();
	gameChat->setChannelID(game->getChatChannel());
	if (game->takeStartRequest())
		push(Event::Launch);
	if (ircChat)
		ircChat->update();
}

std::optional<RoomBackend::Event> YogRoom::takeEvent()
{
	if (events.empty())
		return std::nullopt;
	Event event = std::move(events.front());
	events.pop_front();
	return event;
}

bool YogRoom::lobbyReady() const
{
	return game->getGameJoinCreationState() == MultiplayerGame::ReadyToGo;
}

bool YogRoom::isHost() const
{
	return game->getMultiplayerMode() == MultiplayerGame::HostingGame;
}

std::string YogRoom::mapName() const
{
	return game->getMapHeader().getMapName();
}

int YogRoom::teamCount() const
{
	return game->getMapHeader().getNumberOfTeams();
}

std::optional<std::array<std::uint8_t, 3>> YogRoom::teamColor(int team) const
{
	MapHeader& map = game->getMapHeader();
	if (team < 0 || team >= map.getNumberOfTeams())
		return std::nullopt;
	const auto& color = map.getBaseTeam(team).color;
	return std::array<std::uint8_t, 3>{color.r, color.g, color.b};
}

std::vector<RoomBackend::Slot> YogRoom::slots() const
{
	GameHeader& header = game->getGameHeader();
	const int teams = teamCount();
	std::vector<Slot> result;
	for (int i = 0; i < Team::MAX_COUNT; ++i)
	{
		const BasePlayer& bp = header.getBasePlayer(i);
		Slot slot;
		slot.index = i;
		if (bp.type != BasePlayer::P_NONE)
		{
			slot.name = bp.name;
			slot.team = bp.teamNumber;
			slot.ai = bp.type >= BasePlayer::P_AI;
			slot.ready = game->isReadyToStart(bp.playerID);
			slot.local = bp.number == game->getLocalPlayerNumber();
			result.push_back(slot);
		}
		else if (i < teams)
		{
			slot.open = true;
			result.push_back(slot);
		}
	}
	return result;
}

bool YogRoom::canKick(const Slot& slot) const
{
	return isHost() && !slot.local && !slot.open;
}

void YogRoom::changeTeam(int slot, int team)
{
	if (isHost())
		game->changeTeam(slot, team);
}

void YogRoom::kick(int slot)
{
	game->kickPlayer(slot);
}

void YogRoom::addAI(AI::ImplementationID ai)
{
	game->addAIPlayer(ai);
}

void YogRoom::setReady(bool ready)
{
	game->setHumanReady(ready);
}

bool YogRoom::everyoneReady() const
{
	return game->isGameReadyToStart();
}

bool YogRoom::canStart() const
{
	return game->isGameReadyToStart() && isHost() && !game->isGameStarting();
}

void YogRoom::start()
{
	// MultiplayerGame sends an event when the game is over.
	game->startGame();
}

bool YogRoom::starting() const
{
	return game->isGameStarting();
}

void YogRoom::sendChat(const std::string& text)
{
	std::shared_ptr<YOGMessage> message(new YOGMessage(text, game->getUsername(), YOGNormalMessage));
	gameChat->sendMessage(message);
}

int YogRoom::downloadPercent() const
{
	const int percent = game->percentageDownloadFinished();
	return percent >= 0 && percent < 100 ? percent : -1;
}

std::string YogRoom::experimentsLabel() const
{
	const auto& experiments = game->getGameHeader().getExperiments();
	return experiments.empty() ? std::string() : experimentLabelList(experiments);
}

std::string YogRoom::shareText() const
{
	if (auto server = client->getGameServer(); server && server->networkConfig().lan)
		return server->networkConfig().lobbyEndpoint;
	return {};
}

GameHeader* YogRoom::optionsHeader()
{
	return &game->getGameHeader();
}

MapHeader* YogRoom::optionsMap()
{
	return &game->getMapHeader();
}

void YogRoom::optionsChanged()
{
	game->updateGameHeader();
}

void YogRoom::leave()
{
	game->leaveGame();
}

GAGCore::CooperativeTask YogRoom::initGame(Engine& engine)
{
	return engine.initMultiplayerTask(game, client, game->getLocalPlayer());
}

void YogRoom::gameStarted(bool running)
{
	if (running)
		game->sessionStarted();
	else
		game->sessionEnded(false);
}

void YogRoom::gameEnded(bool quitApplication)
{
	game->sessionEnded(quitApplication);
}

void YogRoom::receiveTextMessage(std::shared_ptr<YOGMessage> message)
{
	push(Event::Chat, message->formatForReading());
}

void YogRoom::handleMultiplayerGameEvent(std::shared_ptr<MultiplayerGameEvent> event)
{
	const Uint8 type = event->getEventType();
	if (type == MGEGameStarted)
	{
		if (ircChat)
			ircChat->stopIRC();
	}
	else if (type == MGEGameExit)
	{
		if (ircChat)
			ircChat->startIRC(game->getUsername());
		push(Event::Finished, {}, GameExited);
		game->leaveGame();
	}
	else if (type == MGEGameEndedNormally)
	{
		if (ircChat)
			ircChat->startIRC(game->getUsername());
		push(Event::Finished, {}, StartedGame);
		game->leaveGame();
	}
	else if (type == MGEGameStartRefused)
		push(Event::Message, Glob2UI::tr("[network start refused]"));
	else if (type == MGEGameRefused)
		push(Event::Finished, {}, GameRefused);
	else if (type == MGEKickedByHost)
		push(Event::Finished, {}, Kicked);
	else if (type == MGEHostCancelledGame)
		push(Event::Finished, {}, GameCancelled);
	else if (type == MGEServerDisconnected)
		push(Event::Finished, {}, ServerDisconnected);
	// Every other event changes something the room shows (players, readiness, download).
	push(Event::Changed);
}
