// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "OnlineResources.h"
#include "ui/FrontendUI.h"

#include <cstdint>
#include <functional>
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
class PlatformScope;
}
class MapPreview;

// Quick match (multiplayer mock-ups, screen group 3): the search panel with
// the timer, rating window, relay region and the AI backfill countdown with
// "Allow an AI opponent".
//
// The Online hub owns the queue cards and shows a running search as a strip
// (SearchStrip); its Details opens this screen. Back returns to the hub and
// keeps the search running; only Cancel ends it. The screen closes by itself
// when the search ends or hands off to a match, so the results lead back to the
// hub, not to a second Online screen. The search lives in Online::quickMatch(),
// so it keeps running while the player opens Profile or Maps, and the
// match-found prompt appears over whichever screen is in front
// (QuickMatchPresenter).
class QuickMatchScreen : public Glob2UI::Screen
{
  public:
	enum
	{
		BACK = 1,
		// The search handed off to a match; the hub starts it (handOff()).
		MATCH_STARTED = 2,
		// The search ended without a match (declined, timed out, cancelled
		// elsewhere); noticeText() says why.
		SEARCH_ENDED = 3,
		// The account chip: the hub opens sign-in (guests) or the account menu.
		ACCOUNT = 4
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
	// Closes without cancelling: the search has become a match.
	void handOff();
	void openProfile();
	void openMaps();
	void openAccount();
	// The toast for the model's current notice; empty for none.
	static std::string noticeText(const Online::QuickMatch &model);
	// Why a failed search failed, for a toast.
	static std::string failureText(const Online::QuickMatch &model);

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
	// A live search has run on this screen; once it ends the screen closes.
	bool searched = false;
	// This screen's platform calls, cancelled when it closes.
	std::unique_ptr<Online::PlatformScope> calls;
};

// The running search as a strip at the top of the online screens (hub, maps,
// profile), so it stays visible and cancellable while the player browses:
// the queue, the timer, when an AI joins, Cancel and (on the hub) Details,
// which opens QuickMatchScreen. Null when no search is running.
namespace SearchStrip
{
Glob2UI::Element build(Online::QuickMatch &model, const Glob2UI::Presentation &p, std::function<void()> details = {});
// Whether a screen showing the strip should rebuild: the search changed, or
// its clock moved to the next second.
class Ticker
{
  public:
	bool due(const Online::QuickMatch &model);

  private:
	std::uint64_t seen = ~std::uint64_t(0);
	int second = -1;
};
}

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
// Stops presenting on this stack (a test's stack about to go away). The
// application's stack lives as long as the process and is never detached.
void detach(GAGGUI::ScreenStack &screens);
}
