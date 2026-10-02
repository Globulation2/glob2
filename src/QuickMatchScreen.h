// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "OnlineResources.h"
#include "ui/FrontendUI.h"

#include <cstdint>
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
class QuickMatch;
}
class MapPreview;

// Quick match (multiplayer mock-ups, screen group 3): the queue cards, and
// while searching the search panel with the timer, rating window, relay
// region and the AI backfill countdown with "Allow an AI opponent".
//
// The hub embeds the same section (quickMatchSection); this screen shows it
// on its own. The search itself lives in Online::quickMatch(), so it keeps
// running while the player opens Profile or Maps, and the match-found prompt
// appears over whichever screen is in front (QuickMatchPresenter).
class QuickMatchScreen : public Glob2UI::Screen
{
  public:
	enum
	{
		BACK = 1
	};
	// The live screen on the shared search and the selected instance.
	explicit QuickMatchScreen(GAGGUI::ScreenStack &screens);
	// Harnesses: a given search model and queue list, no network.
	QuickMatchScreen(GAGGUI::ScreenStack &screens, Online::QuickMatch &model,
					 std::vector<Online::QueueInfo> queues, std::string instance, std::string account);
	~QuickMatchScreen() override;
	Glob2UI::Element build(const Glob2UI::Presentation &p) override;
	void onTimer(Uint32 tick) override;

	// Semantic entry points.
	void find(const std::string &queueId);
	void cancelSearch();
	void setAllowAi(bool allow);
	void back();

  protected:
	void onEscape() override;

  private:
	void loadQueues();
	Glob2UI::Element queueCard(const Online::QueueInfo &queue, const Glob2UI::Presentation &p, bool enabled);
	Glob2UI::Element searchPanel(const Glob2UI::Presentation &p, bool phone);

	GAGGUI::ScreenStack &screens;
	Online::QuickMatch &model;
	bool live;
	std::vector<Online::QueueInfo> queues;
	bool queuesLoaded = false;
	std::string loadError;
	std::string instance, account;
	std::uint64_t seen = 0;
	int shownSecond = -1;
	Uint32 lastLoad = 0;
};

// The prompt when the queue found a match: ranked queues ask both players
// to accept within the countdown; AI-backfilled and casual matches show who
// you will play and start after a short countdown.
class MatchFoundScreen : public Glob2UI::Screen
{
  public:
	enum
	{
		CLOSED = 1,
		STARTED = 2
	};
	explicit MatchFoundScreen(Online::QuickMatch &model);
	~MatchFoundScreen() override;
	Glob2UI::Element build(const Glob2UI::Presentation &p) override;
	void onTimer(Uint32 tick) override;

	void accept();
	void decline();
	void cancel();

  protected:
	void onEscape() override;

  private:
	Glob2UI::Element seatCard(const Online::ProposalSeat &seat, const Glob2UI::Presentation &p, bool showAnswer);

	Online::QuickMatch &model;
	std::uint64_t seen = 0;
	std::int64_t shownTenth = -1;
	std::string mapHash;
	std::unique_ptr<MapPreview> preview;
	bool previewReady = false;
	struct Download;
	std::unique_ptr<Download> download;
};

// Shows MatchFoundScreen over whatever screen is in front when the shared
// search finds a match, and hands the match to the connecting flow
// (Online::beginMatch) once the prompt has closed.
namespace QuickMatchPresenter
{
void attach(GAGGUI::ScreenStack &screens);
}
