// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Portal mode: what a web game portal that embeds the browser build lets the
// game offer (platforms/web/README.md#portal-mode). The portal packager writes
//
//   window.GLOB2_PORTAL = { name: "<portal>", externalLinks: <bool>, onlineSignIn: <bool>, ads: <bool> }
//
// into index.html before the game scripts; ApplicationHost::portalDescription()
// hands its JSON text to the game. Without it (our own site, itch, desktop,
// mobile) everything stays allowed and nothing changes.
//
// externalLinks false hides every button that opens a page outside the game
// (Discord, privacy policy, guides, match, map, profile and leaderboard pages)
// and makes ApplicationHost::openUrl refuse links. onlineSignIn false hides
// online play and everything that signs in to our online servers (Play online,
// Settings > Online and Hive Mind, invite links, liking community AIs) and
// keeps the online client from signing in, so only offline play remains.
// ads true marks the packages whose portal requires or expects ads (Poki,
// GameDistribution, Yandex Games, CrazyGames, GamePix): their adapter may run an
// ad break when the game reports a natural pause (ApplicationHost::matchBreak),
// and a match start then waits for it. Our own site, itch, the stores, Game
// Jolt and Newgrounds never carry ads.
// A portal's first visit (the introduction never offered, storage readable)
// starts straight in the introduction lesson, as CrazyGames and Poki ask new
// players to land in gameplay: no first-run questions, no main menu first.
// Leaving the lesson reaches the first-run questions still unanswered, then
// the main menu (Application::mainMenu).
// In any portal the main menu offers no Quit (MainMenuScreen::offersQuit): the
// portal's page hosts the game, and quitting would leave an empty frame. The
// in-game menu offers no AI telemetry, a developer's view (InGameMainScreen).

#include <string>
#include <string_view>

namespace PortalPolicy
{
struct Policy
{
	// The portal's name ("crazygames"), empty when unnamed or not in a portal.
	std::string name;
	// Whether the game runs inside a portal at all.
	bool portal = false;
	bool externalLinks = true;
	bool onlineSignIn = true;
	// Whether the portal package may show ads at natural breaks. Never outside a portal.
	bool ads = false;
};

// Parses portalDescription() text. Empty text means no portal: everything
// allowed. Anything else is a portal, and it fails closed: a permission is
// granted only by a JSON true, so a missing field, a wrong type, a non-object
// or unparsable text turns that permission (or all of them) off.
Policy parse(std::string_view description);

// The policy of this run, read from the host on first use.
const Policy &current();
// Replaces the policy of this run and applies it to ApplicationHost's link
// switch. Startup installs the host's; tests install their own and restore.
void install(const Policy &policy);

inline bool externalLinks()
{
	return current().externalLinks;
}
inline bool onlineSignIn()
{
	return current().onlineSignIn;
}
inline bool ads()
{
	return current().ads;
}
// Whether this start skips the first-run questions and the main menu for the
// introduction lesson: only in a portal, only before the introduction was offered.
inline bool landsInFirstLesson(bool introductionOffered, bool storageRestoreFailed)
{
	return current().portal && !introductionOffered && !storageRestoreFailed;
}
} // namespace PortalPolicy
