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
// Registry text keys are stored without brackets.
std::string translate(const std::string &key)
{
	return GAGCore::Toolkit::getStringTable()->getString("[" + key + "]");
}

// Position of `value` in a choices table, or -1 when the table does not offer it.
template <std::size_t N> int indexOf(const std::array<int, N> &choices, int value)
{
	const auto found = std::find(choices.begin(), choices.end(), value);
	return found == choices.end() ? -1 : int(found - choices.begin());
}

const GeneratorControl &workerControl(const CustomGameSetup &setup)
{
	return GenerationRequest::control(setup.generator.method, "workers");
}

std::vector<Rule> makeRules()
{
	using S = CustomGameSetup;
	return {
		// Match: how the game is won, how long it lasts and how fast it runs.
		{.id = "victory",
		 .group = Group::Match,
		 .kind = Kind::Segments,
		 .label = "Victory",
		 .help = "Conquest only removes prestige victory; map scripts still apply.",
		 .optionIds = {"prestige", "conquest"},
		 .optionLabels = {"Conquest or prestige", "Conquest only"},
		 .get = [](const S &s) { return s.prestige ? 0 : 1; },
		 .set = [](S &s, int v) { s.prestige = v == 0; }},
		{.id = "timeLimit",
		 .group = Group::Match,
		 .kind = Kind::Choice,
		 .label = "Time limit",
		 .help = "Match ends at the timer; highest prestige at that instant wins.",
		 .optionIds = {"off", "30", "45", "60", "90"},
		 .optionLabels = {"Off (no timer)", "30 minutes", "45 minutes", "60 minutes", "90 minutes"},
		 .get = [](const S &s) { return indexOf(S::suddenDeathMinuteChoices, s.suddenDeathMinutes); },
		 .set = [](S &s, int v) { s.suddenDeathMinutes = S::suddenDeathMinuteChoices[std::size_t(v)]; }},
		{.id = "winProbability",
		 .group = Group::Match,
		 .kind = Kind::Choice,
		 .label = "Probability victory",
		 .help = "Ends the match when the model reaches the selected confidence. Spectators can see the predicted "
				 "win chances in statistics.",
		 .optionIds = {"off", "95", "97", "99"},
		 .optionLabels = {"Off (play it out)", "95% sure", "97% sure", "99% sure"},
		 .inRooms = InRooms::Hidden,
		 .get = [](const S &s) { return indexOf(S::winProbabilityChoices, s.winProbabilityPermille); },
		 .set = [](S &s, int v) { s.winProbabilityPermille = S::winProbabilityChoices[std::size_t(v)]; }},
		{.id = "alliancesChange",
		 .group = Group::Match,
		 .kind = Kind::Toggle,
		 .label = "Alliances can change",
		 .help = "Choose whether teams can change during the match.",
		 .get = [](const S &s) { return int(!s.locked); },
		 .set = [](S &s, int v) { s.locked = !v; }},
		// Game speed ids are the multipliers Settings::getGameSpeedText shows, so they need no
		// labels of their own; the last one is "maximum".
		{.id = "speed",
		 .group = Group::Match,
		 .kind = Kind::Choice,
		 .label = "Game speed",
		 .help = "Changes the pace of the whole simulation.",
		 .optionIds = {"1x", "1.25x", "1.6x", "2x", "2.5x", "4x", "5x", "8x", "13x", "40x", "max"},
		 .inRooms = InRooms::Hidden,
		 .get = [](const S &s) { return s.speed; },
		 .set = [](S &s, int v) { s.speed = v; }},
		// Start: what every colony begins with.
		{.id = "revealTerrain",
		 .group = Group::Start,
		 .kind = Kind::Toggle,
		 .label = "Reveal terrain",
		 .help = "Players see the map's terrain from the start; enemy units stay hidden.",
		 .get = [](const S &s) { return int(s.revealed); },
		 .set = [](S &s, int v) { s.revealed = v; }},
		{.id = "workers",
		 .group = Group::Start,
		 .kind = Kind::Stepper,
		 .label = "Starting workers",
		 .help = "More workers jump-start colony growth. Changes the generated map.",
		 .affectsMap = true,
		 .inRooms = InRooms::Shown,
		 .get = [](const S &s) { return s.generator.nbWorkers; },
		 .set = [](S &s, int v) { s.generator.nbWorkers = v; }},
		{.id = "unitLevel",
		 .group = Group::Start,
		 .kind = Kind::Choice,
		 .label = "Starting unit level",
		 .help = "Starting units spawn already leveled up. Changes the generated map.",
		 .optionIds = {"standard", "veteran", "elite", "legendary"},
		 .optionLabels = {"Standard", "Veteran", "Elite", "Legendary"},
		 .affectsMap = true,
		 .inRooms = InRooms::Hidden,
		 .get = [](const S &s) { return s.startingUnitLevel; },
		 .set = [](S &s, int v) { s.startingUnitLevel = v; }},
		{.id = "stockpile",
		 .group = Group::Start,
		 .kind = Kind::Choice,
		 .label = "Starting stockpile",
		 .help = "Each team starts with extra resources in its shared pool.",
		 .optionIds = {"none", "50", "150", "300"},
		 .optionLabels = {"No stockpile", "Small (+50 each)", "Medium (+150 each)", "Large (+300 each)"},
		 .get = [](const S &s) { return s.stockpileStart; },
		 .set = [](S &s, int v) { s.stockpileStart = v; }},
		// Economy. Regrowth is one axis over two stored fields: resources regrow at a
		// scarcity divisor, or never (scarcity has no effect then, so it reads as None).
		{.id = "regrowth",
		 .group = Group::Economy,
		 .kind = Kind::Choice,
		 .label = "Regrowth",
		 .help = "How quickly harvested resources grow back and spread.",
		 .optionIds = {"normal", "slow", "very-slow", "rare", "none"},
		 .optionLabels = {"Normal", "Scarce (2x slower)", "Very scarce (4x slower)", "Extremely scarce (8x slower)", "No growth"},
		 .get = [](const S &s) { return s.noResourceGrowth ? 4 : s.resourceScarcity; },
		 .set =
			 [](S &s, int v)
		 {
			 s.noResourceGrowth = v == 4;
			 s.resourceScarcity = v == 4 ? 0 : v;
		 }},
		{.id = "instantConstruction",
		 .group = Group::Economy,
		 .kind = Kind::Toggle,
		 .label = "Instant construction",
		 .help = "Building sites complete immediately, skipping delivery.",
		 .get = [](const S &s) { return int(s.instantConstruction); },
		 .set = [](S &s, int v) { s.instantConstruction = v; }},
		{.id = "hunger",
		 .group = Group::Economy,
		 .kind = Kind::Toggle,
		 .label = "Units get hungry",
		 .help = "Units must eat to keep working.",
		 .get = [](const S &s) { return int(!s.noHunger); },
		 .set = [](S &s, int v) { s.noHunger = !v; }},
		// Combat. Turning combat off makes the rest of the group moot (needsCombat).
		{.id = "combat",
		 .group = Group::Combat,
		 .kind = Kind::Toggle,
		 .label = "Combat",
		 .help = "Teams can attack each other. Turn off for a peaceful match.",
		 .get = [](const S &s) { return int(!s.peacefulMode); },
		 .set = [](S &s, int v) { s.peacefulMode = !v; }},
		{.id = "glassCannon",
		 .group = Group::Combat,
		 .kind = Kind::Choice,
		 .label = "Glass cannon",
		 .help = "Units hit harder but have less HP and armor.",
		 .optionIds = {"off", "x2", "x3"},
		 .optionLabels = {"No glass cannon", "Glass cannon x2", "Glass cannon x3"},
		 .needsCombat = true,
		 .get = [](const S &s) { return s.glassCannonLevel; },
		 .set = [](S &s, int v) { s.glassCannonLevel = v; }},
		{.id = "buildingStrength",
		 .group = Group::Combat,
		 .kind = Kind::Choice,
		 .label = "Building strength",
		 .help = "Higher tiers give every building much more HP.",
		 .optionIds = {"normal", "x5", "x10"},
		 .optionLabels = {"Normal", "Fortress x5", "Fortress x10"},
		 .needsCombat = true,
		 .get = [](const S &s) { return s.buildingHpLevel; },
		 .set = [](S &s, int v) { s.buildingHpLevel = v; }},
		{.id = "woundedRetreat",
		 .group = Group::Combat,
		 .kind = Kind::Toggle,
		 .label = "Wounded units retreat",
		 .help = "Hurt units go back to heal instead of fighting to the death.",
		 .needsCombat = true,
		 .get = [](const S &s) { return int(!s.unitsFearless); },
		 .set = [](S &s, int v) { s.unitsFearless = !v; }},
		{.id = "unitsCanDie",
		 .group = Group::Combat,
		 .kind = Kind::Toggle,
		 .label = "Units can die",
		 .help = "Units die when they run out of HP. Turn off to stop them at 1 HP.",
		 .needsCombat = true,
		 .get = [](const S &s) { return int(!s.permadeathDisabled); },
		 .set = [](S &s, int v) { s.permadeathDisabled = !v; }},
		{.id = "unitTraining",
		 .group = Group::Combat,
		 .kind = Kind::Toggle,
		 .label = "Unit training",
		 .help = "Units gain levels at schools.",
		 .needsCombat = true,
		 .get = [](const S &s) { return int(!s.unitUpgradesDisabled); },
		 .set = [](S &s, int v) { s.unitUpgradesDisabled = !v; }},
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
	if (rule.kind == Kind::Stepper)
		return workerControl(setup).maximum;
	return rule.kind == Kind::Toggle ? 1 : int(rule.optionIds.size()) - 1;
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
	switch (rule.kind)
	{
	case Kind::Stepper:
		return std::to_string(value);
	case Kind::Toggle:
		return translate(value ? "room rule on" : "room rule off");
	default:
		break;
	}
	if (std::string_view(rule.id) == "speed")
		return value >= Settings::GAME_SPEED_MAXIMUM ? translate("maximum game speed")
			   : value >= 0 && value < int(rule.optionIds.size()) ? rule.optionIds[std::size_t(value)]
																   : std::string();
	if (value < 0 || value >= int(rule.optionLabels.size()))
		return {};
	return translate(rule.optionLabels[std::size_t(value)]);
}

std::string valueText(const Rule &rule, const CustomGameSetup &setup)
{
	const int value = rule.get(setup);
	// A room can set any time limit, not only the lobby's menu choices.
	if (value < 0 && std::string_view(rule.id) == "timeLimit")
		return GAGCore::FormattableString(translate("results minutes %0")).arg(setup.suddenDeathMinutes);
	return optionText(rule, value);
}

bool appliesTo(const Rule &rule, const CustomGameSetup &setup)
{
	return setup.random || !rule.affectsMap;
}
} // namespace CustomGameRules

// Ruleset state on the setup (declared in CustomGameSetup.h) ---------------------------

using CustomGameRules::InRooms;
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
	return CustomGameRules::appliesTo(rule, *this) && (!room || rule.inRooms == InRooms::Carried);
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
	auto *strings = GAGCore::Toolkit::getStringTable();
	const std::string name = strings->getString(baseRuleset().name.c_str());
	const auto changes = rulesetDiff(room).size();
	if (changes == 0)
		return name;
	GAGCore::FormattableString title(strings->getString(changes == 1 ? "[ruleset one change %0]" : "[ruleset changes %0 %1]"));
	title.arg(name);
	if (changes > 1)
		title.arg(int(changes));
	return title;
}
