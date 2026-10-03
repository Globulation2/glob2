// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors
#pragma once
#include "PlatformProtocol.h"
#include "ui/FrontendUI.h"
#include <ScreenStack.h>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Online
{
class PlatformClient;
}

// The online hub (multiplayer mock-up 1): the "Play online" entry of the main menu,
// replacing YOGLoginScreen and YOGSessionScreen. A guest account is created on first
// contact, so a player can queue or open a room within one tap. The account chip
// signs in through the browser handoff (with the confirmation code), quick-match
// cards come from the instance's queue configuration, Create room / Join by code lead
// into the Room screen, and public rooms and recent matches are listed. Invite links
// opened from outside (glob2://, /j/<code>) land here; one for another instance asks
// first (the trust prompt). The legacy YOG lobby stays reachable through the "Legacy
// server" link in the footer until the YOG cutover.
class OnlineHubScreen : public Glob2UI::Screen
{
  public:
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
		int retryInSeconds = 0;
		std::string displayName, accountKind, accountId;
		std::vector<std::string> linkedProviders;
		Online::Json queues = Online::Json::array();
		Online::Json providers = Online::Json::array();
		Online::Json rooms = Online::Json::array();
		Online::Json recent = Online::Json::array();
		// Top of the main queue's ladder (LeaderboardEntry objects) and its name.
		Online::Json leaderboard = Online::Json::array();
		std::string leaderboardName;
		// Browser sign-in: none, choosing a provider, or waiting with a code.
		enum class SignIn
		{
			Closed,
			Choosing,
			Waiting
		} signIn = SignIn::Closed;
		std::string confirmationCode;
		std::string toast;
	};
	// Harness: shows a fixed model without touching the network.
	void preview(Model model);
	const Model &model() const { return data; }

	// Semantic entry points (harnesses, tests and the phone thumb block).
	void createRoom();
	void joinByCode(const std::string &codeOrLink);
	void findMatch(int queueIndex);
	void openSignIn();
	void signInWith(const std::string &provider);
	void cancelSignIn();
	void signOut();
	void openAccountMenu(bool open);
	void openSettings();
	void openLegacyServer();
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
	int selectedQueue = 0;
	bool accountMenu = false, joinField = false;
	Uint32 lastRefresh = 0, toastAt = 0, now = 0;
	std::uint64_t stateListener = 0, updateListener = 0;
	// An invite to another instance waiting for the player's answer.
	struct Invite
	{
		std::string origin, code, roomName, hostName;
		bool remember = true;
	};
	std::optional<Invite> trustPrompt;
	// An invite that waits for the client to reach its instance.
	std::optional<Invite> pendingInvite;
	bool fetchingInstance = false, fetchingRooms = false, fetchingHistory = false, fetchingLeaderboard = false;
	// This account's latest matches from GET /api/v1/players/{id}/matches; matches
	// seen live (match.updated) are merged over them.
	Online::Json history = Online::Json::array();
	// A rematch room opened from the results screen; entered once the match's
	// screens have closed.
	std::shared_ptr<class RoomBackend> rematchRoom;
	// A match assigned while the quick-match search screen was in front: that
	// screen closes first, so the match (and its results) return to this hub.
	std::optional<Online::Json> deferredMatch;

	Online::PlatformClient &client();
	// After the quick-match search screen closes (QuickMatchScreen result codes).
	void quickMatchClosed(int result);
	void refresh(bool force);
	void syncFromClient();
	void showToast(const std::string &text);
	void enterRoom(std::shared_ptr<class RoomBackend> room);
	// An assigned quick match (Online::beginMatch): the starting screen, then the game.
	void startMatch(const Online::Json &assignment);
	void openProfile();
	void openMaps(bool mine);
	bool canPlay() const;

	Glob2UI::Element accountChip(const Glob2UI::Presentation &p);
	Glob2UI::Element banner(const Glob2UI::Presentation &p);
	Glob2UI::Element quickMatch(const Glob2UI::Presentation &p, bool phone);
	Glob2UI::Element roomList(const Glob2UI::Presentation &p, bool phone);
	Glob2UI::Element recentMatches(const Glob2UI::Presentation &p, bool phone);
	Glob2UI::Element leaderboardTeaser(const Glob2UI::Presentation &p);
	Glob2UI::Element signInPanel(const Glob2UI::Presentation &p);
	Glob2UI::Element trustPanel(const Glob2UI::Presentation &p);
	Glob2UI::Element accountPanel(const Glob2UI::Presentation &p);
	Glob2UI::Element thumbBlock(const Glob2UI::Presentation &p);
};
