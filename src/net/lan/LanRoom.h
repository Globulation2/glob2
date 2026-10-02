// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// RoomBackend for LAN games on the turn protocol: the host side wraps a LanHost (room
// plus in-process relay), the guest side a LanClient. Starting the game hands the
// engine an Engine::TurnMatchStart; the room keeps the turn transport and closes it
// after the game.

#include <deque>
#include <memory>

#include "GameHeader.h"
#include "LanClient.h"
#include "LanHost.h"
#include "MapHeader.h"
#include "RoomBackend.h"

namespace Lan
{
	class LanRoom final : public RoomBackend
	{
	public:
		/// Hosts a game; throws std::exception when the listener cannot start.
		static std::shared_ptr<LanRoom> host(LanHost::Options options);
		/// Joins the host at `endpoint` (its pairing string).
		static std::shared_ptr<LanRoom> join(LanClient::Options options);
		~LanRoom() override;

		void update() override;
		std::optional<Event> takeEvent() override;
		bool lobbyReady() const override;
		bool isHost() const override { return static_cast<bool>(hostRoom); }
		std::string mapName() const override;
		int teamCount() const override;
		std::optional<std::array<std::uint8_t, 3>> teamColor(int team) const override;
		std::vector<Slot> slots() const override;
		bool canKick(const Slot& slot) const override { return isHost() && !slot.local && !slot.open && !starting(); }
		bool canChangeTeam(const Slot& slot) const override
		{
			return !slot.open && !starting() && (isHost() || slot.local);
		}
		void changeTeam(int slot, int team) override;
		void kick(int slot) override;
		bool canAddAI() const override { return isHost() && !starting(); }
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

		/// Guests: still connecting to the host.
		bool connecting() const;
		/// Why the room ended, for the screen that owns it (empty when the player left).
		const std::string& endMessage() const { return ending; }
		LanHost* hostSide() { return hostRoom.get(); }
		LanClient* guestSide() { return guest.get(); }

	private:
		LanRoom() = default;
		const RoomState* room() const;
		int localSeat() const;
		void push(Event::Kind kind, std::string text = {}, int code = 0);
		void finish(int code, std::string message);
		std::string describeClose(const std::string& reason, const std::string& detail) const;

		std::shared_ptr<LanHost> hostRoom;
		RoomState hostState; ///< the host's room as of the last update()
		std::unique_ptr<LanClient> guest;
		std::deque<Event> events;
		bool launched = false;
		bool finished = false;
		bool ready = false;
		std::string ending;

		// The game this client is about to play or plays.
		std::optional<LanClient::StartInfo> pending;
		std::shared_ptr<Turn::TurnTransport> transport;

		// Options screen
		GameHeader options;
		MapHeader optionMap;
	};
}
