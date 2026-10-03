// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// What a multiplayer setup room needs from the network behind it. The room screen
// (RoomScreen) only talks to this interface. Implementations: PlatformRoom (online
// rooms on a platform instance) and LanRoom (LAN games on the turn protocol).
//
// Everything is polled from the UI thread: update() once per timer tick, then
// takeEvent() until it returns nothing.
//
// The first block is the interface every backend implements. The second block
// (below "Room screen") is what the shared Room screen of the multiplayer revamp
// shows beyond it: invite, latency, seat controllers, setup editing. Each of those
// has a default, so a backend that does not offer something simply does not show it.

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
struct CustomGameSetup;
namespace Online
{
class OnlineMatch;
}

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

		// Room screen details; defaults show nothing.
		bool host = false;    ///< the room's host sits here
		bool guest = false;   ///< a guest account (online)
		bool locked = false;  ///< closed by the host; nobody may take it
		int latencyMs = -1;   ///< measured latency to the relay or host, -1 unknown
		std::string detail;   ///< second line: device, address, AI description
		int progress = -1;    ///< map download or loading progress 0..100, -1 none
		std::string aiId;     ///< AI seats: the AI's stable CLI name (AINames)
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
		/// Chat: a notice from the room (joins, setup changes), not a player's line.
		bool system = false;
		/// Chat: who wrote it, when known (text is then only the message).
		std::string author;
	};

	/// Result codes carried by Finished events.
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

	// ---------------------------------------------------------------- Room screen

	enum class Kind
	{
		Online, ///< a room on a platform instance: invite link, rated history
		Lan,    ///< hosted on this network, not rated
	};
	virtual Kind kind() const { return Kind::Lan; }

	/// The colour shown beside a seat (its map team's colour).
	virtual std::optional<std::array<std::uint8_t, 3>> seatColor(const Slot& slot) const { return teamColor(slot.team); }
	/// Name of a slot's team choice for the Team control ("Team 1").
	virtual int teamChoices() const { return teamCount(); }

	/// The room's title ("Bradley's room", "Living-room LAN").
	virtual std::string roomName() const { return mapName(); }
	/// Display name of the host, for guests ("host Bradley").
	virtual std::string hostName() const { return {}; }

	/// Visibility: listed in the hub (public) or reachable by invite only.
	virtual bool canChangeVisibility() const { return false; }
	virtual bool listed() const { return false; }
	virtual void setListed(bool) {}

	/// Online invite: the web link (https://<instance>/j/<code>) and the bare code.
	virtual std::string inviteLink() const { return {}; }
	virtual std::string inviteCode() const { return {}; }
	/// LAN: the address others on this network join (shown with shareText()).
	virtual std::string localAddress() const { return {}; }

	/// One line summarising map, size, format and rules, under the room's title.
	virtual std::string setupSummary() const { return mapName(); }
	/// The current state of the map when it is not simply ready ("Preparing map on
	/// the server…", a failure), else empty.
	virtual std::string mapStatus() const { return {}; }
	/// A local file of the room's map for the preview, once it is available.
	virtual std::optional<std::string> mapFile() const { return {}; }

	/// Taking an open seat (members move themselves), and the host's seat controls.
	virtual bool canTakeSeat(const Slot&) const { return false; }
	virtual void takeSeat(int) {}
	enum class Occupant
	{
		Open,   ///< empty, anyone may take it
		AI,     ///< an AI (aiId)
		Closed, ///< locked: nobody, the colony stays inactive
	};
	virtual bool canSetOccupant(const Slot&) const { return false; }
	virtual void setOccupant(int, Occupant, const std::string& /*aiId*/ = {}) {}

	/// Ready: this client's own state (guests toggle it; hosts start instead).
	virtual bool localReady() const { return false; }
	/// Why Start is not available yet ("Waiting for Ana_M to be ready"), or empty.
	virtual std::string waitingFor() const { return {}; }
	/// Why this client cannot press Ready ("Take an open seat to play"), or empty.
	virtual std::string readyBlocker() const { return {}; }
	/// Members in the room without a seat, as display lines (this client's own is
	/// marked "(you)"). They are listed under the seats.
	virtual std::vector<std::string> unseatedMembers() const { return {}; }
	/// This client is in the room but has no seat.
	virtual bool localUnseated() const { return false; }

	/// The host's setup draft (map, teams, rules) for the custom-game editor, and
	/// applying an edited draft back to the room. Empty when the backend keeps its
	/// setup elsewhere (the options screen above).
	virtual bool canEditSetup() const { return false; }
	/// Fills `draft` with the room's setup; false when there is none to edit.
	virtual bool setupDraft(CustomGameSetup& /*draft*/) const { return false; }
	virtual void applySetup(const CustomGameSetup&) {}

	/// Online rooms start through the match flow (ticket, relay, map download) instead
	/// of initGame(): after a Launch event the screen hands this to the starting screen.
	virtual std::shared_ptr<Online::OnlineMatch> takeMatch() { return {}; }
};
