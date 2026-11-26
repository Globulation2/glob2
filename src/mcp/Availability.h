// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#include <BuildFeatures.h>
// Desktop clients serve MCP unless their edition leaves it out (GLOB2_HAS_MCP).
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE) && !defined(SERVER_ONLY) \
	&& GLOB2_HAS_MCP
#define GLOB2_DESKTOP_MCP 1
#else
#define GLOB2_DESKTOP_MCP 0
#endif

namespace MCP
{
/// While any hold lives the MCP server stays off whatever the setting says: a
/// quick match from a queue for players without MCP is starting, running or showing
/// its results, so switching MCP on then cannot bring an AI agent into it.
/// Main thread only.
inline int &unassistedMatchHolds()
{
	static int holds = 0;
	return holds;
}
inline bool suspendedForUnassistedMatch()
{
	return unassistedMatchHolds() > 0;
}
struct UnassistedMatchHold
{
	UnassistedMatchHold() { ++unassistedMatchHolds(); }
	~UnassistedMatchHold() { --unassistedMatchHolds(); }
	UnassistedMatchHold(const UnassistedMatchHold &) = delete;
	UnassistedMatchHold &operator=(const UnassistedMatchHold &) = delete;
};
} // namespace MCP
