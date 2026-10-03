// SPDX-License-Identifier: GPL-3.0-or-later
#include "CustomGameRules.h"
#include "CustomGameSetup.h"
#include "RulesetCatalog.h"
#include "Settings.h"
#include <FormatableString.h>
#include <StringTable.h>
#include <Toolkit.h>
#include <algorithm>

namespace CustomGameRules
{
namespace
{
std::string text(const std::string &key)
{
	return GAGCore::Toolkit::getStringTable()->getString("[" + key + "]");
}

int minuteIndex(int minutes)
{
	const auto &choices = CustomGameSetup::suddenDeathMinuteChoices;
	const auto found = std::find(choices.begin(), choices.end(), minutes);
	return found == choices.end() ? 0 : int(found - choices.begin());
}

const GeneratorControl &workerControl(const CustomGameSetup &setup)
{
	return GenerationRequest::control(setup.generator.method, "workers");
}

std::vector<Rule> makeRules()
{
	using S = CustomGameSetup;
	return {
		{"victory", Group::Match, Kind::Segments, "Victory", "Conquest only removes prestige victory; map scripts still apply.",
		 {"prestige", "conquest"}, {"Conquest or prestige", "Conquest only"}, false, true, false, false,
		 [](const S &s) { return s.prestige ? 0 : 1; }, [](S &s, int v) { s.prestige = v == 0; }},
		{"timeLimit", Group::Match, Kind::Choice, "Time limit", "Match ends at the timer; highest prestige at that instant wins.",
		 {"off", "30", "45", "60", "90"}, {"Off (no timer)", "30 minutes", "45 minutes", "60 minutes", "90 minutes"}, false, true, false, false,
		 [](const S &s) { return minuteIndex(s.suddenDeathMinutes); },
		 [](S &s, int v) { s.suddenDeathMinutes = S::suddenDeathMinuteChoices[std::size_t(v)]; }},
		// Rooms do not carry it (MatchRules has no field for it yet), so the room editor hides it.
		{"winProbability", Group::Match, Kind::Choice, "Probability victory",
		 "Ends the match when the model reaches the selected confidence. Spectators can see the predicted win chances in statistics.",
		 {"off", "95", "97", "99"}, {"Off (play it out)", "95% sure", "97% sure", "99% sure"}, false, false, true, false,
		 [](const S &s)
		 {
			 const auto &choices = S::winProbabilityChoices;
			 const auto found = std::find(choices.begin(), choices.end(), s.winProbabilityPermille);
			 return found == choices.end() ? 0 : int(found - choices.begin());
		 },
		 [](S &s, int v) { s.winProbabilityPermille = S::winProbabilityChoices[std::size_t(v)]; }},
		{"alliancesChange", Group::Match, Kind::Toggle, "Alliances can change", "Choose whether teams can change during the match.",
		 {"off", "on"}, {"Locked teams", "Can change in game"}, false, true, false, false,
		 [](const S &s) { return int(!s.locked); }, [](S &s, int v) { s.locked = !v; }},
		{"speed", Group::Match, Kind::Choice, "Game speed", "Changes the pace of the whole simulation.",
		 {"1x", "1.25x", "1.6x", "2x", "2.5x", "4x", "5x", "8x", "13x", "40x", "max"}, {}, false, false, true, false,
		 [](const S &s) { return s.speed; }, [](S &s, int v) { s.speed = v; }},
		{"revealTerrain", Group::Start, Kind::Toggle, "Reveal terrain", "Revealed terrain does not reveal all enemy activity.",
		 {"off", "on"}, {"Explore as you play", "Terrain revealed"}, false, true, false, false,
		 [](const S &s) { return int(s.revealed); }, [](S &s, int v) { s.revealed = v; }},
		{"workers", Group::Start, Kind::Stepper, "Starting workers", "More workers jump-start colony growth. Changes the generated map.",
		 {}, {}, true, false, false, false,
		 [](const S &s) { return s.generator.nbWorkers; }, [](S &s, int v) { s.generator.nbWorkers = v; }},
		{"unitLevel", Group::Start, Kind::Choice, "Starting unit level", "Starting units spawn already leveled up. Changes the generated map.",
		 {"standard", "veteran", "elite", "legendary"}, {"Standard", "Veteran", "Elite", "Legendary"}, true, false, true, false,
		 [](const S &s) { return s.startingUnitLevel; }, [](S &s, int v) { s.startingUnitLevel = v; }},
		{"stockpile", Group::Start, Kind::Choice, "Starting stockpile", "Seeds each team's shared market/exchange resource pool at game start.",
		 {"none", "50", "150", "300"}, {"No stockpile", "Small (+50 each)", "Medium (+150 each)", "Large (+300 each)"}, false, true, false, false,
		 [](const S &s) { return s.stockpileStart; }, [](S &s, int v) { s.stockpileStart = v; }},
		// One axis over two stored fields: resources regrow at a scarcity divisor, or never.
		{"regrowth", Group::Economy, Kind::Choice, "Regrowth", "How quickly harvested resources grow back and spread.",
		 {"normal", "slow", "very-slow", "rare", "none"},
		 {"Normal", "Scarce (2x slower)", "Very scarce (4x slower)", "Extremely scarce (8x slower)", "No growth"}, false, true, false, false,
		 [](const S &s) { return s.noResourceGrowth ? 4 : s.resourceScarcity; },
		 [](S &s, int v)
		 {
			 s.noResourceGrowth = v == 4;
			 s.resourceScarcity = v == 4 ? 0 : v;
		 }},
		{"instantConstruction", Group::Economy, Kind::Toggle, "Instant construction", "Building sites complete immediately, skipping delivery.",
		 {"off", "on"}, {"Normal construction", "Instant"}, false, true, false, false,
		 [](const S &s) { return int(s.instantConstruction); }, [](S &s, int v) { s.instantConstruction = v; }},
		{"hunger", Group::Economy, Kind::Toggle, "Units get hungry", "Units must eat to keep working.",
		 {"off", "on"}, {"No hunger", "Units get hungry"}, false, true, false, false,
		 [](const S &s) { return int(!s.noHunger); }, [](S &s, int v) { s.noHunger = !v; }},
		{"combat", Group::Combat, Kind::Toggle, "Combat", "Off: a peaceful match with no fighting between teams.",
		 {"off", "on"}, {"Peaceful mode", "Normal combat"}, false, true, false, false,
		 [](const S &s) { return int(!s.peacefulMode); }, [](S &s, int v) { s.peacefulMode = !v; }},
		{"glassCannon", Group::Combat, Kind::Choice, "Glass cannon", "Higher tiers deal more damage but have less HP and armor.",
		 {"off", "x2", "x3"}, {"No glass cannon", "Glass cannon x2", "Glass cannon x3"}, false, true, false, true,
		 [](const S &s) { return s.glassCannonLevel; }, [](S &s, int v) { s.glassCannonLevel = v; }},
		{"buildingStrength", Group::Combat, Kind::Choice, "Building strength", "Higher tiers give every building much more HP.",
		 {"normal", "x5", "x10"}, {"Normal", "Fortress x5", "Fortress x10"}, false, true, false, true,
		 [](const S &s) { return s.buildingHpLevel; }, [](S &s, int v) { s.buildingHpLevel = v; }},
		{"woundedRetreat", Group::Combat, Kind::Toggle, "Wounded units retreat", "Off: units fight to the death instead of retreating to heal.",
		 {"off", "on"}, {"Fearless", "Retreats when damaged"}, false, true, false, true,
		 [](const S &s) { return int(!s.unitsFearless); }, [](S &s, int v) { s.unitsFearless = !v; }},
		{"unitsCanDie", Group::Combat, Kind::Toggle, "Units can die", "Off: units are never permanently lost; HP just stops at 1.",
		 {"off", "on"}, {"No permadeath", "Can die permanently"}, false, true, false, true,
		 [](const S &s) { return int(!s.permadeathDisabled); }, [](S &s, int v) { s.permadeathDisabled = !v; }},
		{"unitTraining", Group::Combat, Kind::Toggle, "Unit training", "Off: units still visit schools but never gain a level.",
		 {"off", "on"}, {"No upgrades", "Trains normally"}, false, true, false, true,
		 [](const S &s) { return int(!s.unitUpgradesDisabled); }, [](S &s, int v) { s.unitUpgradesDisabled = !v; }},
	};
}
} // namespace

const std::vector<Rule> &rules()
{
	static const std::vector<Rule> table = makeRules();
	return table;
}

const Rule *find(std::string_view id)
{
	for (const auto &rule : rules())
		if (id == rule.id)
			return &rule;
	return nullptr;
}

const char *groupLabel(Group group)
{
	switch (group)
	{
	case Group::Match:
		return "Match";
	case Group::Start:
		return "Start";
	case Group::Economy:
		return "Economy";
	case Group::Combat:
		return "Combat";
	}
	return "";
}

int minimum(const Rule &rule, const CustomGameSetup &setup)
{
	return rule.kind == Kind::Stepper ? workerControl(setup).minimum : 0;
}

int maximum(const Rule &rule, const CustomGameSetup &setup)
{
	return rule.kind == Kind::Stepper ? workerControl(setup).maximum : int(rule.optionIds.size()) - 1;
}

int standardValue(const Rule &rule, const CustomGameSetup &setup)
{
	if (rule.kind == Kind::Stepper)
		return workerControl(setup).defaultValue;
	// Standard is the setup's own defaults; only the generator differs between drafts.
	static const CustomGameSetup defaults;
	return rule.get(defaults);
}

std::string optionText(const Rule &rule, int value)
{
	if (rule.kind == Kind::Stepper)
		return std::to_string(value);
	// Game speed ids are the multipliers the game shows (Settings::getGameSpeedText).
	if (std::string_view(rule.id) == "speed")
		return value >= Settings::GAME_SPEED_MAXIMUM ? text("maximum game speed")
			   : value >= 0 && value < int(rule.optionIds.size()) ? rule.optionIds[std::size_t(value)]
																   : std::string();
	if (value < 0 || value >= int(rule.optionLabels.size()))
		return {};
	return text(rule.optionLabels[std::size_t(value)]);
}

bool appliesTo(const Rule &rule, const CustomGameSetup &setup)
{
	return setup.random || !rule.affectsMap;
}
} // namespace CustomGameRules

// Ruleset state on the setup ----------------------------------------------------------

using CustomGameRules::Rule;

int CustomGameSetup::ruleValue(const Rule &rule) const
{
	return rule.get(*this);
}

bool CustomGameSetup::setRule(const Rule &rule, int value)
{
	value = std::clamp(value, CustomGameRules::minimum(rule, *this), CustomGameRules::maximum(rule, *this));
	if (rule.get(*this) == value)
		return false;
	rule.set(*this, value);
	if (rule.affectsMap)
		++mapRevision;
	return rule.affectsMap;
}

bool CustomGameSetup::applyRuleset(const std::string &id)
{
	const auto &catalog = RulesetCatalog::shipped();
	const Ruleset *ruleset = catalog.find(id);
	if (!ruleset)
		ruleset = &catalog.standard();
	rulesetId = ruleset->id;
	bool mapChanged = false;
	for (const auto &rule : CustomGameRules::rules())
		mapChanged = setRule(rule, ruleset->value(rule, *this)) || mapChanged;
	return mapChanged;
}

const Ruleset &CustomGameSetup::baseRuleset() const
{
	const auto &catalog = RulesetCatalog::shipped();
	const Ruleset *ruleset = catalog.find(rulesetId);
	return ruleset ? *ruleset : catalog.standard();
}

bool CustomGameSetup::ruleCounts(const Rule &rule, bool room) const
{
	if (!CustomGameRules::appliesTo(rule, *this))
		return false;
	return !room || (rule.carriedInRooms && !rule.hiddenInRooms);
}

bool CustomGameSetup::ruleChanged(const Rule &rule, bool room) const
{
	return ruleCounts(rule, room) && rule.get(*this) != baseRuleset().value(rule, *this);
}

bool CustomGameSetup::ruleNonStandard(const Rule &rule, bool room) const
{
	return ruleCounts(rule, room) && rule.get(*this) != CustomGameRules::standardValue(rule, *this);
}

std::vector<const Rule *> CustomGameSetup::rulesetDiff(bool room) const
{
	std::vector<const Rule *> changed;
	for (const auto &rule : CustomGameRules::rules())
		if (ruleChanged(rule, room))
			changed.push_back(&rule);
	return changed;
}

std::string CustomGameSetup::rulesetTitle(bool room) const
{
	const std::string name = GAGCore::Toolkit::getStringTable()->getString(baseRuleset().name.c_str());
	const auto changes = rulesetDiff(room).size();
	if (changes == 0)
		return name;
	const char *key = changes == 1 ? "[ruleset one change %0]" : "[ruleset changes %0 %1]";
	GAGCore::FormattableString title(GAGCore::Toolkit::getStringTable()->getString(key));
	title.arg(name);
	if (changes > 1)
		title.arg(int(changes));
	return title;
}
