// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <string_view>
#include <vector>

struct CustomGameSetup;

// The custom-game rules, described once. The Game Rules tab, rulesets (data/rulesets.json),
// change markers and room summaries all read this table instead of listing rules by hand.
// Each rule is an integer value: an option index, 0/1 for a toggle, or a count for the
// stepper. Accessors map that value onto CustomGameSetup's stored fields, so the fields,
// GameHeader and saves keep their existing meaning (a toggle such as "Units get hungry"
// reads noHunger inverted).
namespace CustomGameRules
{
enum class Group
{
	Match,
	Start,
	Economy,
	Combat
};
enum class Kind
{
	Toggle,
	Segments,
	Choice,
	Stepper
};

struct Rule
{
	// Stable id used by data/rulesets.json and the tab's control keys ("rule/<id>").
	const char *id;
	Group group;
	Kind kind;
	// Text keys without brackets.
	const char *label;
	const char *help;
	// Value ids used by rulesets (index = value), and their text keys. A toggle lists its
	// off then on text, used by room summaries; the stepper has none.
	std::vector<const char *> optionIds, optionLabels;
	// Changing the value regenerates a generated map's preview.
	bool affectsMap = false;
	// Online rooms carry the value (MatchRules); rulesets are matched on these alone.
	bool carriedInRooms = true;
	// The room editor hides the rule: rooms do not carry it yet.
	bool hiddenInRooms = false;
	// Has no effect while combat is off.
	bool needsCombat = false;
	int (*get)(const CustomGameSetup &);
	void (*set)(CustomGameSetup &, int);
};

// Every rule, grouped and in display order.
const std::vector<Rule> &rules();
const Rule *find(std::string_view id);
const char *groupLabel(Group group);
constexpr Group groups[] = {Group::Match, Group::Start, Group::Economy, Group::Combat};

int minimum(const Rule &rule, const CustomGameSetup &setup);
int maximum(const Rule &rule, const CustomGameSetup &setup);
// The value under the Standard ruleset's defaults (workers depend on the generator).
int standardValue(const Rule &rule, const CustomGameSetup &setup);
// The value's translated text; game speed uses its multiplier text.
std::string optionText(const Rule &rule, int value);
// Only generated maps take starting workers and unit levels from the rules.
bool appliesTo(const Rule &rule, const CustomGameSetup &setup);
} // namespace CustomGameRules
