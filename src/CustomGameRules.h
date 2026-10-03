// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <string_view>
#include <vector>

struct CustomGameSetup;

// The custom-game rules, described once. The Game Rules tab, rulesets (data/rulesets.json,
// see RulesetCatalog), change markers and room summaries all read this table instead of
// listing rules by hand; adding a rule here makes it appear everywhere.
//
// Each rule's value is an integer: an option index, 0/1 for a toggle, or a count for the
// stepper. Accessors map that value onto CustomGameSetup's stored fields, so the fields,
// GameHeader and saves keep their existing meaning: "Units get hungry" reads noHunger
// inverted, and Regrowth spans noResourceGrowth and resourceScarcity.
//
// Text: `label`, `help` and `optionLabels` are text keys without their brackets, translated
// by optionText()/the tab. Ruleset names in data/rulesets.json are written with brackets,
// as they appear in the string tables.
namespace CustomGameRules
{
enum class Group
{
	Match,
	Start,
	Economy,
	Combat
};
inline constexpr Group groups[] = {Group::Match, Group::Start, Group::Economy, Group::Combat};

enum class Kind
{
	Toggle,
	Segments,
	Choice,
	Stepper
};

// What an online room does with a rule. Rooms carry a fixed set of rules (MatchRules);
// rulesets are matched on the carried ones alone.
enum class InRooms
{
	// Carried by the room and editable in the room editor.
	Carried,
	// Shown in the room editor but set elsewhere (starting workers travel with the map).
	Shown,
	// Not carried yet: the room editor hides it.
	Hidden
};

struct Rule
{
	// Stable id used by data/rulesets.json and the tab's control keys ("rule/<id>").
	const char *id;
	Group group;
	Kind kind;
	const char *label;
	const char *help;
	// Value ids that rulesets name (index = value) and, for choices and segments, their
	// text keys. Toggles and the stepper have none: toggles read On/Off.
	std::vector<const char *> optionIds, optionLabels;
	// Changing the value regenerates a generated map's preview.
	bool affectsMap = false;
	InRooms inRooms = InRooms::Carried;
	// Has no effect while combat is off.
	bool needsCombat = false;
	// The value, or -1 when the stored field holds something the options do not offer
	// (a room's time limit can be any number of minutes).
	int (*get)(const CustomGameSetup &);
	void (*set)(CustomGameSetup &, int);
};

// Every rule, grouped and in display order.
const std::vector<Rule> &rules();
const Rule *find(std::string_view id);
// Text key of a group's heading, without brackets.
const char *groupLabel(Group group);

int minimum(const Rule &rule, const CustomGameSetup &setup);
int maximum(const Rule &rule, const CustomGameSetup &setup);
// The value under the Standard ruleset's defaults (workers depend on the generator).
int standardValue(const Rule &rule, const CustomGameSetup &setup);
// A value's translated text: the option label, On/Off, the multiplier for game speed, or
// the count for the stepper.
std::string optionText(const Rule &rule, int value);
// The setup's current value as text, including values the options do not offer.
std::string valueText(const Rule &rule, const CustomGameSetup &setup);
// Only generated maps take starting workers and unit levels from the rules; premade maps
// keep the units their author placed.
bool appliesTo(const Rule &rule, const CustomGameSetup &setup);
} // namespace CustomGameRules
