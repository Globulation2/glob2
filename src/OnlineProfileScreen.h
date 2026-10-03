// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "OnlineResources.h"
#include "SinglePlayerFlow.h"
#include "ui/FrontendUI.h"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace GAGGUI
{
class ScreenStack;
}
namespace Online
{
class PlatformScope;
}

// Profile and match history (multiplayer mock-ups, screen group 7 A): the
// rating of each queue with its trend and provisional flag, a few aggregates
// over recent games, and the match list with Replay and the match page.
// The figures come from the player's profile (GET /api/v1/players/{id}:
// ratings with rank, rating history, aggregates) and the list from their
// history (GET /api/v1/players/{id}/matches); deep analysis is one link away
// on the web (<origin>/players/<id>, <origin>/matches/<id>).
class OnlineProfileScreen : public Glob2UI::Screen
{
  public:
	const char *recordingId() const override { return "online_profile"; }
	enum
	{
		BACK = 1
	};
	enum class Filter
	{
		All,
		Ranked,
		Rooms,
		VersusAi
	};
	// The signed-in player's profile on the shared client.
	explicit OnlineProfileScreen(GAGGUI::ScreenStack &screens);
	// Another account's profile.
	OnlineProfileScreen(GAGGUI::ScreenStack &screens, std::string accountId);
	// Harnesses: fixed data, no network.
	struct Data
	{
		std::string instance, accountId, displayName, kind, since;
		std::vector<Online::MatchSummary> matches;
		std::map<std::string, std::string> ladderNames;
		std::int64_t now = 0;
		// GET /api/v1/players/{id}: ratings, rating history and aggregates.
		std::optional<Online::PlayerProfile> profile;
	};
	OnlineProfileScreen(GAGGUI::ScreenStack &screens, Data data);
	~OnlineProfileScreen() override;
	Glob2UI::Element build(const Glob2UI::Presentation &p) override;
	void onTimer(Uint32 tick) override;

	// Semantic entry points.
	void setFilter(Filter filter);
	void select(int index); // phones: opens the match sheet; -1 closes it
	void replay(const std::string &matchId);
	void openMatchPage(const std::string &matchId);
	void openWebProfile();
	void loadMore();

  protected:
	void onEscape() override;

  private:
	void load(bool more);
	void summarize();
	std::vector<int> visible() const;
	std::string ladderName(const std::string &ladder) const;
	std::string matchTitle(const Online::MatchSummary &match) const;
	std::string matchKind(const Online::MatchSummary &match) const;
	Glob2UI::Element ratingCard(const Online::LadderSummary &ladder, const Glob2UI::Presentation &p, bool phone);
	Glob2UI::Element matchRow(int index, const Glob2UI::Presentation &p, bool phone);

	GAGGUI::ScreenStack &screens;
	SinglePlayerFlow flow;
	bool live;
	Data data;
	Online::ProfileSummary summary;
	std::string cursor;
	bool loading = false;
	std::string problem;
	std::string status; // replay download progress
	Filter filter = Filter::All;
	int selected = -1;
	bool started = false;
	// This screen's platform calls, cancelled when it closes (made on first use:
	// fixture screens never touch the platform).
	std::unique_ptr<Online::PlatformScope> scope;
	Online::PlatformScope &calls();
};
