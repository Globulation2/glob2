// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Portal mode: what a web game portal that embeds the browser build lets the
// game offer (platforms/web/README.md#portal-mode). The portal packager writes
//
//   window.GLOB2_PORTAL = { name: "<portal>", externalLinks: <bool>, onlineSignIn: <bool> }
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
} // namespace PortalPolicy
