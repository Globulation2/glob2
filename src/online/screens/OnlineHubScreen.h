// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors
#pragma once
#include "PlatformProtocol.h"

namespace Online
{
class PlatformScope;
}
namespace Glob2UI
{
class PreviewImages;
}
#include "QuickMatchScreen.h"
#include "ui/FrontendUI.h"
#include "ui/MapPictures.h"
#include <ScreenStack.h>
#include <functional>
#include <map>
#include <set>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Online
{
class PlatformClient;
}

// The online hub (multiplayer mock-up 1): the "Play online" entry of the main menu,
// replacing the YOG lobby. A guest account is created on first
// contact, so a player can queue or open a room within one tap. The account chip
// signs in through the browser handoff (with the confirmation code), quick-match
// cards come from the instance's queue configuration, Create room / Join by code lead
// into the Room screen, and public rooms and recent matches are listed. Invite links
// opened from outside (glob2://, /j/<code>) land here; one for another instance asks
// first (the trust prompt).
class OnlineHubScreen : public Glob2UI::Screen
{
  public:
	const char *recordingId() const override { return "online_hub"; }
	// What "Find match" does: the queue screens set this (see OnlineHub::setQuickMatch).
	using QuickMatch = std::function<void(GAGGUI::ScreenStack &, const Online::Json &queue, bool allowAiOpponent)>;
	static void setQuickMatch(QuickMatch start);

	/// connect: start the shared platform client (harness fixtures pass false).
	explicit OnlineHubScreen(GAGGUI::ScreenStack &screens, bool connect = true);
	~OnlineHubScreen() override;
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	void onTimer(Uint32 tick) override;

	// Everything the screen shows, refreshed from the platform client in onTimer.
	struct Model
	{
		std::string origin, instanceName;
		enum class Link
		{
			Connecting,
			Online,
			Offline,
			UpdateRequired
		} link = Link::Connecting;
		// With UpdateRequired: which side is out of date, when the server says.
		enum class Outdated
		{
			Unknown,
			Client,
			Server
		} outdated = Outdated::Unknown;
		int retryInSeconds = 0;
		std::string displayName, accountKind, accountId;
		std::vector<std::string> linkedProviders;
		Online::Json queues = Online::Json::array();
		Online::Json providers = Online::Json::array();
		Online::Json rooms = Online::Json::array();
		Online::Json recent = Online::Json::array();
		// The main queue's ladder (LeaderboardEntry objects), its name, and where
		// this account stands on it (myRank 0: not placed yet).
		Online::Json leaderboard = Online::Json::array();
		std::string leaderboardName;
		int myRank = 0;
		double myRating = 0;
		// GET /api/v1/stats: players online (-1 until known) and players searching
		// each queue (servers without per-queue counts leave it empty).
		// The instance takes one search in several queues (InstanceInfo.features 'queue.multi').
		bool multiQueue = false;
		int playersOnline = -1;
		std::map<std::string, int> searching;
		// Browser sign-in: none, choosing a provider, or waiting for the browser.
		enum class SignIn
		{
			Closed,
			Choosing,
			Waiting
		} signIn = SignIn::Closed;
		std::string confirmationCode;
		// Whether the game managed to open the sign-in page itself.
		bool browserOpened = true;
		std::string toast;
	};
	// Harness: shows a fixed model without touching the network.
	void preview(Model model);
	const Model &model() const { return data; }
	// Harnesses: show this search (a model with presentSearching) instead of the shared one.
	void previewSearching(Online::QuickMatch &search) { previewSearch = &search; invalidate(); }

	// The hub's sections. Maps and Profile & history are screens of their own.
	enum class Section
	{
		Play,
		Rooms,
		Leaderboard
	};
	// Semantic entry points (harnesses, tests and the phone thumb block).
	void showSection(Section next);
	Section currentSection() const { return section; }
	void createRoom();
	void createRoom(bool listed);
	void joinByCode(const std::string &codeOrLink);
	void findMatch(int queueIndex);
	void openSignIn();
	void signInWith(const std::string &provider);
	void cancelSignIn();
	void copyCode();
	void signOut();
	void openAccountMenu(bool open);
	void openSettings();
	// Invite links (Online::takePendingJoin) and the trust prompt for other instances.
	void acceptInvite(const std::string &origin, const std::string &code);
	void answerTrust(bool join);

  protected:
	void onEscape() override;

  private:
	GAGGUI::ScreenStack &screens;
	Model data;
	bool previewing = false;
	std::string joinDraft;
	int roomFilter = 0;
	int selectedQueue = -1; // -1: defaultQueue()
	// Other queues searched with the chosen one ("Also search …"), by id.
	std::set<std::string> alsoQueues;
	Section section = Section::Play;
	std::unique_ptr<Glob2UI::MapPictures> pictures;
	bool accountMenu = false, joinField = false;
	Uint32 lastRefresh = 0, toastAt = 0, now = 0;
	// Every platform call and listener of this screen: destroying it with the
	// screen cancels them, so no callback reaches a closed hub.
	std::unique_ptr<Online::PlatformScope> calls;
	// An invite to another instance waiting for the player's answer.
	struct Invite
	{
		std::string origin, code, roomName, hostName;
		bool remember = true;
	};
	std::optional<Invite> trustPrompt;
	// An invite that waits for the client to reach its instance.
	std::optional<Invite> pendingInvite;
	std::string mapLaunchOrigin;
	bool fetchingStats = false;
	// New rooms are listed in Open rooms (Play with friends' toggle); invite-only by default.
	bool listRoom = false;
	std::unique_ptr<Glob2UI::PreviewImages> previews;
	bool fetchingInstance = false, fetchingRooms = false, fetchingHistory = false, fetchingLeaderboard = false, fetchingStanding = false;
	// This account's latest matches from GET /api/v1/players/{id}/matches; matches
	// seen live (match.updated) are merged over them.
	Online::Json history = Online::Json::array();
	// A rematch room opened from the results screen; entered once the match's
	// screens have closed.
	std::shared_ptr<class RoomBackend> rematchRoom;
	// A match assigned while the quick-match search screen was in front: that
	// screen closes first, so the match (and its results) return to this hub.
	std::optional<Online::Json> deferredMatch;
	// The queue of the quick match in progress, and whether its results asked to
	// search that queue again ("Find another match") once its screens close.
	std::string matchQueueId;
	bool queueAgain = false;
	SearchStrip::Ticker searchTicker;
	Online::QuickMatch *previewSearch = nullptr;
	Online::QuickMatch &searchModel();

	Online::PlatformClient &client();
	// The running search's screen (the strip's Details).
	void openSearch();
	// After the quick-match search screen closes (QuickMatchScreen result codes).
	void quickMatchClosed(int result);
	// Rebuilds for the search strip and turns search notices and failures into toasts.
	void watchSearch();
	void refresh(bool force);
	void syncFromClient();
	void showToast(const std::string &text);
	void enterRoom(std::shared_ptr<class RoomBackend> room);
	// An assigned quick match (Online::beginMatch): the starting screen, then the game.
	void startMatch(const Online::Json &assignment);
	void openProfile();
	void openMaps(bool mine);
	bool canPlay() const;
	// Guests cannot enter rated queues: the server refuses them.
	bool canQueue(const Online::Json &queue) const;
	// The queue offered first: the first one this account can enter.
	int defaultQueue() const;
	int queueIndex(const std::string &queueId) const;
	// The also-searched queues that apply with `chosen` (multi-queue servers only).
	std::vector<std::string> searchAlso(int chosen) const;
	Glob2UI::Element alsoToggles(int chosen, const Glob2UI::Presentation &p);

	Glob2UI::Element accountChip(const Glob2UI::Presentation &p);
	Glob2UI::Element banner(const Glob2UI::Presentation &p);
	// The sections: the sidebar (desktop) or tabs (phones), and each section's body.
	Glob2UI::Element sectionNav(const Glob2UI::Presentation &p, bool phone);
	Glob2UI::Element sectionBody(const Glob2UI::Presentation &p, bool phone);
	Glob2UI::Element playSection(const Glob2UI::Presentation &p, bool phone);
	Glob2UI::Element roomsSection(const Glob2UI::Presentation &p, bool phone);
	Glob2UI::Element leaderboardSection(const Glob2UI::Presentation &p, bool phone);
	// Play: the one quick-match card (queue choice, map pool, Find match), rooms with
	// friends, and the last match.
	Glob2UI::Element quickMatchCard(const Glob2UI::Presentation &p, bool phone);
	Glob2UI::Element friendsCard(const Glob2UI::Presentation &p);
	// Players online and searching this queue, from /stats; null when unknown.
	Glob2UI::Element liveLine(const Online::Json &queue, const Glob2UI::Presentation &p);
	// The queue's map pool as pictures with their names; null without a pool.
	Glob2UI::Element mapPool(const Online::Json &queue, const Glob2UI::Presentation &p, bool phone);
	Glob2UI::Element lastMatchCard(const Glob2UI::Presentation &p, bool phone);
	Glob2UI::Element outcomeBadge(const std::string &letter, const Glob2UI::Presentation &p);
	Glob2UI::Element signInPanel(const Glob2UI::Presentation &p);
	Glob2UI::Element trustPanel(const Glob2UI::Presentation &p);
	Glob2UI::Element accountPanel(const Glob2UI::Presentation &p);
	// The phone's thumb-reach controls; withQueues false leaves out the queue
	// picker and Find match (queuePicker), which a crowded portrait page scrolls.
	Glob2UI::Element thumbBlock(const Glob2UI::Presentation &p, bool withQueues = true);
	Glob2UI::Element queuePicker(const Glob2UI::Presentation &p, bool stacked = false);
};
