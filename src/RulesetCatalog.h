// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "CustomGameRules.h"
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// A premade ruleset: the rules it changes from Standard. Every other rule takes its
// Standard value, so an entry only lists what makes it different.
struct Ruleset
{
	std::string id;
	// Text keys, brackets included ("[Blitz]").
	std::string name, description;
	std::vector<std::pair<const CustomGameRules::Rule *, int>> values;
	// The ruleset's value for a rule (its Standard value when the ruleset leaves the rule
	// alone), within the range the setup's generator allows.
	int value(const CustomGameRules::Rule &rule, const CustomGameSetup &setup) const;
};

// The rulesets players choose from, read from data/rulesets.json; the format is described
// in docs/features/custom-game-setup/README.md. The file only picks values for existing
// rules, so it is not simulation data: two players with different files still play by the
// rules each match header carries.
class RulesetCatalog
{
  public:
	static constexpr const char *filename = "data/rulesets.json";
	std::vector<Ruleset> rulesets;

	const Ruleset *find(std::string_view id) const;
	// Always present and first.
	const Ruleset &standard() const { return rulesets.front(); }

	// Validates against CustomGameRules. A file that does not parse yields the fallback
	// catalog; an invalid entry is skipped. Every problem is described in `errors`.
	static RulesetCatalog parse(std::string_view json, std::vector<std::string> &errors);
	// Reads and parses a file through the file manager; empty when the file is missing.
	static std::optional<RulesetCatalog> load(const std::string &path, std::vector<std::string> &errors);
	// Standard alone, for when the file is missing or unreadable.
	static RulesetCatalog fallback();
	// data/rulesets.json, read once it can be found; Standard alone until then.
	static const RulesetCatalog &shipped();
};
