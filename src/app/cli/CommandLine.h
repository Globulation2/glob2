// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace Cli
{
inline constexpr int Version = 2;
inline constexpr int DefaultTicks = 90000;
inline constexpr int DefaultDiagnosticInterval = 2500;
inline constexpr int DefaultGradientDelay = 8;
inline constexpr int DefaultPreviewScale = 2;
struct Option
{
	std::string name, type, description, defaultValue;
	std::vector<std::string> choices;
	bool repeatable = false, required = false;
	std::int64_t minimum = 0, maximum = 2147483647;
	std::string group = "General", units;
};
struct Command
{
	std::string path, description;
	std::vector<std::string> positionals;
	unsigned minimumPositionals = 0;
	std::vector<Option> options;
	std::vector<std::string> constraints, examples, environment;
	std::string outputs, platform = "all";
	bool available = true;
	std::vector<std::vector<std::string>> conflicts;
	std::map<std::string, std::vector<std::string>> requirements;
};
// Parsing validates syntax and constraints once, before handlers touch assets or outputs.
// Only explicitly supplied options are stored; get() resolves defaults from the registry.
struct Request
{
	std::string command;
	std::vector<std::string> positionals;
	std::map<std::string, std::vector<std::string>> options;
	// Ordered occurrences preserve config override and repeated argument semantics.
	std::vector<std::pair<std::string, std::string>> occurrences;
	bool help = false;
	bool has(const std::string &key) const;
	std::string get(const std::string &key, const std::string &fallback = "") const;
	std::vector<std::string> all(const std::string &key) const;
};
const std::vector<Command> &commands();
const Command &definition(const std::string &path);
Request parse(const std::vector<std::string> &arguments);
Request parse(int argc, char **argv);
nlohmann::json describe(const std::string &path = "");
std::string help(const std::string &path = "");
std::string completion(const std::string &shell);
// Returns -1 for commands which execute domain work, 0 for help/completion.
int runStatic(const Request &request);
} // namespace Cli
