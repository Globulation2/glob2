// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// What a multiplayer setup room needs from the network behind it. The room screen
// (MultiplayerGameScreen today, the Room screen of the multiplayer revamp later) only
// talks to this interface. Implementations: LanRoom (LAN games on the turn protocol),
// YogRoom (the legacy YOG lobby, until YOG is removed) and, later, PlatformRoom.
//
// Everything is polled from the UI thread: update() once per timer tick, then
// takeEvent() until it returns nothing.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "AI.h"
#include "CooperativeTask.h"

class Engine;
class GameHeader;
class MapHeader;

class RoomBackend
{
public:
	/// One seat as the room shows it.
	struct Slot
	{
		int index = 0;      ///< what changeTeam/kick take
		std::string name;
		int team = 0;
		bool ai = false;
		bool ready = true;  ///< humans: ready to start
		bool local = false; ///< this client's own seat
		bool open = false;  ///< an empty seat a player may still take
	};

	struct Event
	{
		enum Kind
		{
			/// Something the room shows changed.
			Changed,
			/// A chat or notice line (text).
			Chat,
			/// Start the game now: initGame(), then gameStarted()/gameEnded().
			Launch,
			/// Show a message (text) over the room; the room stays open.
			Message,
			/// The room is over; `code` is one of the screen's result codes and `text`
			/// an optional explanation.
			Finished,
		};
		Kind kind = Changed;
		std::string text;
		int code = 0;
	};

	/// Result codes carried by Finished events (MultiplayerGameScreen's values).
	enum FinishCode
	{
		Cancelled,
		StartedGame,
		GameRefused,
		Kicked,
		GameCancelled,
		ServerDisconnected,
		GameExited = -1,
	};

	virtual ~RoomBackend() = default;

	virtual void update() = 0;
	virtual std::optional<Event> takeEvent() = 0;

	/// The room has its map and seats (joiners wait for the host's first state).
	virtual bool lobbyReady() const = 0;
	virtual bool isHost() const = 0;
	virtual std::string mapName() const = 0;
	virtual int teamCount() const = 0;
	/// The map team's colour, or nullopt when not known yet.
	virtual std::optional<std::array<std::uint8_t, 3>> teamColor(int team) const = 0;
	/// Seats in order, including open ones.
	virtual std::vector<Slot> slots() const = 0;
	virtual bool canChangeTeam(const Slot& slot) const { return isHost() || slot.local; }
	virtual bool canKick(const Slot& slot) const { return isHost() && !slot.local; }
	virtual void changeTeam(int slot, int team) = 0;
	virtual void kick(int slot) = 0;
	virtual bool canAddAI() const { return isHost(); }
	virtual void addAI(AI::ImplementationID ai) = 0;
	virtual void setReady(bool ready) = 0;
	/// Every player is ready (the room then lets the host start).
	virtual bool everyoneReady() const = 0;
	virtual bool canStart() const = 0;
	virtual void start() = 0;
	virtual bool starting() const = 0;
	virtual void sendChat(const std::string& text) = 0;
	/// Map download progress, 0..99, or -1.
	virtual int downloadPercent() const = 0;
	/// The enabled experiments' labels, comma-separated; empty when none.
	virtual std::string experimentsLabel() const = 0;
	/// Text a host shares so guests can join (LAN pairing string), or empty.
	virtual std::string shareText() const { return {}; }

	/// The rules, alliances and experiments as a GameHeader on the room's map, for the
	/// options screen; null while unavailable. optionsChanged() applies host edits.
	virtual GameHeader* optionsHeader() = 0;
	virtual MapHeader* optionsMap() = 0;
	virtual bool optionsReadOnly() const { return !isHost(); }
	virtual void optionsChanged() = 0;

	/// Leaving the room (Cancel / Leave Game / Escape).
	virtual void leave() = 0;

	/// After a Launch event: initializes the engine for this game.
	virtual GAGCore::CooperativeTask initGame(Engine& engine) = 0;
	/// The game is running (true) or could not start (false).
	virtual void gameStarted(bool running) = 0;
	/// The game screen closed.
	virtual void gameEnded(bool quitApplication) = 0;
};
