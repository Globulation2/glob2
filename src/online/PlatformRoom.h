// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// RoomBackend for a room on a platform instance, over the realtime room.* methods
// and events (docs/multiplayer/rooms-and-matches.md). Seat i plays map team i; a
// slot's "team" here is the seat's alliance (teams[i].alliance), which the host
// changes with room.update. The host edits the map and rules through the custom-game
// screen (setupDraft/applySetup); a generated map is produced on the server and
// downloaded by hash for the preview and the match.
//
// match.start for this room turns into a Launch event; the screen then takes the
// OnlineMatch (takeMatch) and shows the starting steps.

#include "PlatformProtocol.h"
#include "RoomBackend.h"

#include <deque>
#include <memory>
#include <set>

namespace Online
{
class PlatformClient;
class OnlineMatch;

class PlatformRoom final : public RoomBackend
{
  public:
	/// Creates a room hosted by the signed-in account.
	static std::shared_ptr<PlatformRoom> create(PlatformClient &client, const std::string &name, bool listed,
												const CustomGameSetup &setup);
	/// Joins the room behind an invite code.
	static std::shared_ptr<PlatformRoom> join(PlatformClient &client, const std::string &code);
	/// A room shown from a fixed state, without a connection (harness, gallery).
	static std::shared_ptr<PlatformRoom> preview(Json room, std::string accountId,
												 std::vector<std::pair<std::string, std::string>> chat = {});
	~PlatformRoom() override;

	void update() override;
	std::optional<Event> takeEvent() override;
	bool lobbyReady() const override { return !state.is_null(); }
	bool isHost() const override;
	std::string mapName() const override;
	int teamCount() const override;
	std::optional<std::array<std::uint8_t, 3>> teamColor(int team) const override;
	std::vector<Slot> slots() const override;
	bool canChangeTeam(const Slot &slot) const override;
	bool canKick(const Slot &slot) const override;
	void changeTeam(int slot, int team) override;
	void kick(int slot) override;
	bool canAddAI() const override;
	void addAI(AI::ImplementationID ai) override;
	void setReady(bool ready) override;
	bool everyoneReady() const override;
	bool canStart() const override;
	void start() override;
	bool starting() const override;
	void sendChat(const std::string &text) override;
	int downloadPercent() const override { return -1; }
	std::string experimentsLabel() const override;
	GameHeader *optionsHeader() override { return nullptr; }
	MapHeader *optionsMap() override { return nullptr; }
	void optionsChanged() override {}
	void leave() override;
	GAGCore::CooperativeTask initGame(Engine &engine) override;
	void gameStarted(bool) override {}
	void gameEnded(bool) override;

	std::optional<std::array<std::uint8_t, 3>> seatColor(const Slot &slot) const override;
	Kind kind() const override { return Kind::Online; }
	std::string roomName() const override;
	std::string hostName() const override;
	bool canChangeVisibility() const override { return isHost() && !starting(); }
	bool listed() const override;
	void setListed(bool value) override;
	std::string inviteLink() const override;
	std::string inviteCode() const override;
	std::string setupSummary() const override;
	std::string mapStatus() const override;
	std::optional<std::string> mapFile() const override;
	bool canTakeSeat(const Slot &slot) const override;
	void takeSeat(int slot) override;
	bool canSetOccupant(const Slot &slot) const override;
	void setOccupant(int slot, Occupant occupant, const std::string &aiId) override;
	bool localReady() const override;
	std::string waitingFor() const override;
	bool canEditSetup() const override { return isHost() && !starting(); }
	bool setupDraft(CustomGameSetup &draft) const override;
	void applySetup(const CustomGameSetup &setup) override;
	std::shared_ptr<OnlineMatch> takeMatch() override;

	/// The current RoomState JSON (null before the first one).
	const Json &roomState() const { return state; }
	/// Why a request failed, for the status line; cleared by the next success.
	const std::string &lastProblem() const { return problem; }

  private:
	explicit PlatformRoom(PlatformClient *client);
	void listen();
	void adopt(const Json &room);
	void call(const std::string &method, Json params, std::function<void(const Json &)> done = {});
	void push(Event event);
	void notice(const std::string &text);
	void systemLinesFor(const Json &previous, const Json &next);
	const Json *seatJson(int seat) const;
	const Json *member(const std::string &accountId) const;
	std::string myAccount() const;
	void fetchMap();

	PlatformClient *client;
	Json state;
	std::string accountId;
	std::deque<Event> events;
	std::vector<std::uint64_t> listeners;
	std::set<std::uint64_t> requests;
	std::string problem;
	bool left = false, finished = false;
	std::shared_ptr<OnlineMatch> match;
	std::string pendingMatchId;
	// Map preview download.
	struct MapFetch;
	std::shared_ptr<MapFetch> mapFetch;
	std::string mapHash, mapPath;
	std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
} // namespace Online
