// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE) && !defined(SERVER_ONLY)
#define GLOB2_DESKTOP_MCP 1
#else
#define GLOB2_DESKTOP_MCP 0
#endif

namespace MCP
{
/// While true the MCP server stays off whatever the setting says: a quick match
/// from a queue for players without MCP is running (the engine sets it), so
/// switching MCP on mid-match cannot bring an AI agent into it. Main thread only.
inline bool &suspendedForUnassistedMatch()
{
	static bool suspended = false;
	return suspended;
}
} // namespace MCP
