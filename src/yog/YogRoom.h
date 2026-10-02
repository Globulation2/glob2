// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// The legacy YOG lobby's game room behind RoomBackend, so the room screen no longer
// talks to YOGClient and MultiplayerGame directly. It keeps the online YOG flow
// unchanged until YOG is removed (multiplayer plan M9).

#include <deque>
#include <memory>

#include "IRCTextMessageHandler.h"
#include "MultiplayerGame.h"
#include "MultiplayerGameEventListener.h"
#include "RoomBackend.h"
#include "YOGClientChatChannel.h"
#include "YOGClientChatListener.h"

class YogRoom final : public RoomBackend, private YOGClientChatListener, private MultiplayerGameEventListener
{
public:
	YogRoom(std::shared_ptr<MultiplayerGame> game, std::shared_ptr<YOGClient> client,
	        std::shared_ptr<IRCTextMessageHandler> ircChat = {});
	~YogRoom() override;

	void update() override;
	std::optional<Event> takeEvent() override;
	bool lobbyReady() const override;
	bool isHost() const override;
	std::string mapName() const override;
	int teamCount() const override;
	std::optional<std::array<std::uint8_t, 3>> teamColor(int team) const override;
	std::vector<Slot> slots() const override;
	bool canKick(const Slot& slot) const override;
	void changeTeam(int slot, int team) override;
	void kick(int slot) override;
	void addAI(AI::ImplementationID ai) override;
	void setReady(bool ready) override;
	bool everyoneReady() const override;
	bool canStart() const override;
	void start() override;
	bool starting() const override;
	void sendChat(const std::string& text) override;
	int downloadPercent() const override;
	std::string experimentsLabel() const override;
	std::string shareText() const override;
	GameHeader* optionsHeader() override;
	MapHeader* optionsMap() override;
	void optionsChanged() override;
	void leave() override;
	GAGCore::CooperativeTask initGame(Engine& engine) override;
	void gameStarted(bool running) override;
	void gameEnded(bool quitApplication) override;

private:
	void receiveTextMessage(std::shared_ptr<YOGMessage> message) override;
	void handleMultiplayerGameEvent(std::shared_ptr<MultiplayerGameEvent> event) override;
	void push(Event::Kind kind, std::string text = {}, int code = 0);

	std::shared_ptr<MultiplayerGame> game;
	std::shared_ptr<YOGClient> client;
	std::shared_ptr<IRCTextMessageHandler> ircChat;
	std::shared_ptr<YOGClientChatChannel> gameChat;
	std::deque<Event> events;
};
