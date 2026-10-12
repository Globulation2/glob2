// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string>

// Invite links and the "pending join" they leave for the online hub.
//
// Forms accepted (docs/multiplayer/client.md):
//   glob2://join?instance=<origin>&code=<code>   custom scheme, any instance
//   https://<host>/j/<code>                      web link of an instance
//                                                (a former official origin's
//                                                link means the official one)
//   ?join=<code> on the web client's page        the page's own instance
//   online join <link or code> [--instance <origin>]  command line
//
// Links arrive at launch (command line, browser query) or while running
// (macOS/iOS URL events, Android intents). Either way they become the pending
// join; the hub consumes it with takePendingJoin(). A running client is not
// handed links from a second launch in v1: the second launch joins directly.
namespace Cli { struct Request; }
namespace Online
{
struct InviteLink
{
	std::string origin; // normalized instance origin
	std::string code;	// InviteCode: 6-16 ASCII letters and digits
	bool operator==(const InviteLink &other) const
	{
		return origin == other.origin && code == other.code;
	}
};

bool isInviteCode(const std::string &code);
// Parses a glob2:// or https:// invite link. Instance origins must be https
// (http only for loopback development instances).
std::optional<InviteLink> parseInviteLink(const std::string &link);
// A link, or a bare invite code for defaultOrigin.
std::optional<InviteLink> parseInvite(const std::string &linkOrCode,
									  const std::string &defaultOrigin);
// glob2://join?instance=<origin>&code=<code>
std::string formatSchemeLink(const InviteLink &invite);
// <origin>/j/<code>
std::string formatWebLink(const InviteLink &invite);

// The newest invite the client was opened with; replaces an older one.
void setPendingJoin(const InviteLink &invite);
std::optional<InviteLink> pendingJoin();
std::optional<InviteLink> takePendingJoin();
void clearPendingJoin();

// Accepts an invite or catalog-play URL from launch arguments or URL events,
// keeping its destination until the frontend can open it.
bool acceptInviteText(const std::string &text);
// Apply a validated online launch command; invalid destinations throw.
void acceptLaunchRequest(const Cli::Request &request);
// Catalog play links: glob2://play?... and https://<instance>/play/?...
bool acceptMapPlayText(const std::string &text);
} // namespace Online
