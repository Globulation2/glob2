// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

// Self-description for agents that know nothing about the game: the initialize and
// server/discover instructions, and the topics served by the game_guide tool.
namespace MCP
{
// Canonical public locations; every guide surface links to them.
inline constexpr const char *RepositoryUrl = "https://github.com/Globulation2/glob2";
inline constexpr const char *ServerGuideUrl =
	"https://github.com/Globulation2/glob2/blob/master/docs/development/mcp-ui-server.md";
// Short orientation returned by initialize and server/discover.
const std::string &serverInstructions();
std::vector<std::string> guideTopics();
// Throws std::invalid_argument for an unknown topic.
nlohmann::json guide(const std::string &topic);
} // namespace MCP
