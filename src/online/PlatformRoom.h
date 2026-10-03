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

#include "PlatformClient.h"
#include "PlatformProtocol.h"
#include "RoomBackend.h"

#include <deque>
#include <memory>
#include <set>

namespace Online
{
class PlatformClient;
class MapCache;
class OnlineStorage;
class OnlineMatch;

class PlatformRoom final : public RoomBackend
{
  public:
	/// Creates a room hosted by the signed-in account.
	/// `automaticMap`: the setup is the default room map (defaultRoomSetup), which grows
	/// from two to four colonies when more people join, until the host picks a map.
	/// Rooms download their maps into `maps` and pass `storage` to match skins.
	/// The client, cache and storage must outlive the room and its matches.
	static std::shared_ptr<PlatformRoom> create(PlatformClient &client, MapCache &maps, OnlineStorage &storage, const std::string &name,
												bool listed, const CustomGameSetup &setup, bool automaticMap = false);
	/// Joins the room behind an invite code.
	static std::shared_ptr<PlatformRoom> join(PlatformClient &client, MapCache &maps, OnlineStorage &storage, const std::string &code);
	/// Opens (or joins) the unrated rematch room of a finished quick match
	/// (match.rematch); the other players are invited by the platform.
	static std::shared_ptr<PlatformRoom> rematch(PlatformClient &client, MapCache &maps, OnlineStorage &storage, const std::string &matchId);
	/// A room shown from a fixed state, without a connection (harness, gallery); its
	/// map is looked up in the user directory's cache (sharedMapCache()).
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
	std::string readyBlocker() const override;
	std::vector<std::string> unseatedMembers() const override;
	bool localUnseated() const override;
	bool canEditSetup() const override { return isHost() && !starting(); }
	bool setupDraft(CustomGameSetup &draft) const override;
	void applySetup(const CustomGameSetup &setup) override;
	std::shared_ptr<OnlineMatch> takeMatch() override;
	/// Host: plays a catalog map ("Use in a room" from the map browser).
	void useCatalogMap(const std::string &hash, const std::string &mapId);
	/// Host: plays a premade or own map file from this device. The decompressed
	/// bytes are uploaded (POST /api/v1/uploads?format=map), then the room switches to
	/// {kind: "upload", hash} with the draft's rules and alliances. `title` names the
	/// map until the server reports the title it read from the file.
	void usePremadeMap(const std::string &path, const std::string &title, const CustomGameSetup &setup);
	/// Host: plays already-read map bytes (usePremadeMap after reading the file).
	void useMapBytes(std::string bytes, const std::string &title, const CustomGameSetup &setup);
	/// Host: a generated map chosen in the map editor, even when the room plays a
	/// premade map and the generator settings were left as they were (applySetup
	/// then keeps the premade map and changes only rules and teams).
	void useGeneratedMap(const CustomGameSetup &setup);
	/// A premade map is being uploaded for the room.
	bool uploadingMap() const { return uploading; }
	/// The room plays a generated map (seed, size and generator apply).
	bool generatedMap() const;

	/// The current RoomState JSON (null before the first one).
	const Json &roomState() const { return state; }
	/// Why a request failed, for the status line; cleared by the next success.
	const std::string &lastProblem() const { return problem; }

  private:
	PlatformRoom(PlatformClient *client, MapCache &maps, OnlineStorage *storage);
	void listen();
	void adopt(const Json &room);
	void call(const std::string &method, Json params, std::function<void(const Json &)> done = {});
	void push(Event event);
	void notice(const std::string &text);
	void systemLinesFor(const Json &previous, const Json &next);
	void applyDraftTeams(const Json &teams);
	void applyDraft(const CustomGameSetup &setup, bool chosenMap);
	const Json *seatJson(int seat) const;
	const Json *member(const std::string &accountId) const;
	std::string myAccount() const;
	void fetchMap();

	PlatformClient *client;
	MapCache &maps;
	OnlineStorage *storage; // Null only for a disconnected room preview.
	Json state;
	std::string accountId;
	std::deque<Event> events;
	// Every request, upload and listener of the room, cancelled with it.
	std::unique_ptr<PlatformScope> calls;
	std::string problem;
	bool left = false, finished = false;
	std::shared_ptr<OnlineMatch> match;
	std::string pendingMatchId;
	// Map preview download.
	struct MapFetch;
	std::shared_ptr<MapFetch> mapFetch;
	std::string mapHash, mapPath;
	// The host has not chosen a map yet: the default grows with the room.
	bool automaticMap = false, automaticMapGrown = false;
	void fitAutomaticMap();
	// Premade map upload (host).
	bool uploading = false;
	std::string uploadedTitle, uploadedHash;
};
} // namespace Online
