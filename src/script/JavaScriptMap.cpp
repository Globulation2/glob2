// SPDX-License-Identifier: GPL-3.0-or-later
#include "JavaScriptMap.h"
#include "ScriptObservations.h"
#include "GameGUI.h"
#include "Game.h"
#include "Stream.h"
#include "GlobalContainer.h"
#include <locale>
#include <array>
#include <sstream>
#include <stdexcept>
namespace Script
{
namespace
{
bool boolean(const Value &v, const char *key)
{
	const auto &b = v.get(key);
	if (b.kind != Value::Boolean)
		throw std::runtime_error("Expected effect boolean");
	return b.number != 0;
}
const std::string buildingNames[] = {"swarm",        "inn",      "hospital", "racetrack",
									 "swimmingpool", "barracks", "school",   "defencetower",
									 "stonewall",    "market"};
const std::string flagNames[] = {"explorationflag", "warflag", "clearingflag"};
bool choice(const std::string &n, bool flag)
{
	if (flag)
	{
		for (const auto &x : flagNames)
			if (n == x)
				return true;
	}
	else
	{
		for (const auto &x : buildingNames)
			if (n == x)
				return true;
	}
	return false;
}
std::string rng(const MersenneTwister &engine)
{
	std::ostringstream s;
	s.imbue(std::locale::classic());
	s << engine;
	return s.str();
}
unsigned mix(unsigned h, const std::string &s)
{
	for (unsigned char c : s)
		h = (h ^ c) * 16777619u;
	return h;
}

enum class Action
{
	HintVisible,
	HintHidden,
	Complete,
	Incomplete,
	Failed,
	Hidden,
	Visible
};
struct WorldEffect
{
	int id;
	Action action;
};
struct PreparedEffects
{
	Value presentation;
	std::vector<WorldEffect> world;
};
PreparedEffects prepareEffects(const Value &effects, const Value &presentation, Game &game)
{
	PreparedEffects prepared{presentation, {}};
	if (effects.kind == Value::Null)
		return PreparedEffects{presentation, {}};
	if (effects.kind != Value::Array || effects.items.size() > 256)
		throw std::runtime_error("Map callback requires at most 256 effects");
	Value &nextPresentation = prepared.presentation;
	for (const auto &effect : effects.items)
	{
		if (effect.kind != Value::Object)
			throw std::runtime_error("Effect requires a record");
		auto type = effect.string("type");
		if (type == "message")
		{
			nextPresentation.set("message", effect.string("text"));
			nextPresentation.set("translations", Value::object());
		}
		else if (type == "messageTranslated")
		{
			auto translations = nextPresentation.get("translations");
			if (translations.kind == Value::Null)
				translations = Value::object();
			translations.set(effect.string("language"), effect.string("text"));
			nextPresentation.set("translations", translations);
		}
		else if (type == "hideMessage")
		{
			nextPresentation.set("message", Value());
			nextPresentation.set("translations", Value::object());
		}
		else if (type == "buildingChoice" || type == "flagChoice")
		{
			bool flag = type == "flagChoice";
			auto name = effect.string("name");
			if (!choice(name, flag))
				throw std::runtime_error("Unknown scenario choice");
			auto rules = nextPresentation.get(flag ? "flags" : "buildings");
			if (rules.kind == Value::Null)
				rules = Value::object();
			rules.set(name, boolean(effect, "enabled"));
			nextPresentation.set(flag ? "flags" : "buildings", rules);
		}
		else if (type == "guiElement")
		{
			int id = effect.integer("id", 0, 4);
			auto elements = nextPresentation.get("elements");
			if (elements.kind == Value::Null)
				elements = Value::object();
			elements.set(std::to_string(id), boolean(effect, "enabled"));
			nextPresentation.set("elements", elements);
		}
		else if (type == "hint")
		{
			const int id = effect.integer("id", 0, game.gameHints.getNumberOfHints() - 1);
			prepared.world.push_back(
				{id, boolean(effect, "visible") ? Action::HintVisible : Action::HintHidden});
		}
		else if (type == "objective")
		{
			const int id = effect.integer("id", 0, game.objectives.getNumberOfObjectives() - 1);
			auto action = effect.string("action");
			if (action != "complete" && action != "incomplete" && action != "failed" &&
				action != "hidden" && action != "visible")
				throw std::runtime_error("Unknown objective action");
			const Action actions[] = {Action::Complete, Action::Incomplete, Action::Failed,
									  Action::Hidden, Action::Visible};
			const char *names[] = {"complete", "incomplete", "failed", "hidden", "visible"};
			for (int i = 0; i < 5; ++i)
				if (action == names[i])
					prepared.world.push_back({id, actions[i]});
		}
		else
			throw std::runtime_error("Unknown scenario effect");
	}

	prepared.presentation.encode();
	return prepared;
}
void commitEffects(const PreparedEffects &prepared, Game &game) noexcept
{
	// All IDs/actions were validated and allocated before this stage. These
	// engine setters only update existing boolean entries and cannot allocate.
	for (const auto &effect : prepared.world)
	{
		switch (effect.action)
		{
		case Action::HintVisible:
			game.gameHints.setHintVisible(effect.id);
			break;
		case Action::HintHidden:
			game.gameHints.setHintHidden(effect.id);
			break;
		case Action::Complete:
			game.objectives.setObjectiveComplete(effect.id);
			break;
		case Action::Incomplete:
			game.objectives.setObjectiveIncomplete(effect.id);
			break;
		case Action::Failed:
			game.objectives.setObjectiveFailed(effect.id);
			break;
		case Action::Hidden:
			game.objectives.setObjectiveHidden(effect.id);
			break;
		case Action::Visible:
			game.objectives.setObjectiveVisible(effect.id);
			break;
		}
	}
}
struct PresentedInterface
{
	std::array<bool, 10> buildings;
	std::array<bool, 3> flags;
	std::array<bool, 5> elements;
	std::string text;
};
bool allowed(const Value &presentation, const std::string &name, bool flag)
{
	const auto &value = presentation.get(flag ? "flags" : "buildings").get(name);
	return value.kind != Value::Boolean || value.number != 0;
}
PresentedInterface prepareInterface(const Value &presentation)
{
	PresentedInterface shown;
	for (int i = 0; i < 10; ++i)
		shown.buildings[i] = allowed(presentation, buildingNames[i], false);
	for (int i = 0; i < 3; ++i)
		shown.flags[i] = allowed(presentation, flagNames[i], true);
	for (int i = 0; i < 5; ++i)
	{
		const auto &value = presentation.get("elements").get(std::to_string(i));
		shown.elements[i] = value.kind != Value::Boolean || value.number != 0;
	}
	const auto &message = presentation.get("message");
	if (message.kind == Value::String)
		shown.text = message.text;
	const auto &translation =
		presentation.get("translations").get(globalContainer->settings.language);
	if (translation.kind == Value::String)
		shown.text = translation.text;
	return shown;
}
void applyInterface(PresentedInterface &&shown, GameGUI &gui, bool publishHistory) noexcept
{
	for (int i = 0; i < 10; ++i)
		if (shown.buildings[i])
			gui.enableBuildingsChoice(buildingNames[i]);
		else
			gui.disableBuildingsChoice(buildingNames[i]);
	for (int i = 0; i < 3; ++i)
		if (shown.flags[i])
			gui.enableFlagsChoice(flagNames[i]);
		else
			gui.disableFlagsChoice(flagNames[i]);
	for (int i = 0; i < 5; ++i)
		if (shown.elements[i])
			gui.enableGUIElement(i);
		else
			gui.disableGUIElement(i);
	gui.setScriptPresentationText(std::move(shown.text), publishHistory);
}

} // namespace
void JavaScriptMap::reset()
{
	runtime->discard();
	state = Value::object();
	presentation = Value::object();
	initialized = false;
	seeded = false;
	random.seed();
}
bool JavaScriptMap::buildingAllowed(const std::string &name, bool flag) const
{
	const auto &value = presentation.get(flag ? "flags" : "buildings").get(name);
	return value.kind != Value::Boolean || value.number != 0;
}
void JavaScriptMap::present(GameGUI &gui, bool publishHistory) const
{
	applyInterface(prepareInterface(presentation), gui, publishHistory);
}
void JavaScriptMap::step(const std::string &source, GameGUI &gui)
{
	auto checkpoint = random;
	const bool wasSeeded = seeded;
	if (!seeded)
	{
		random.seed(gui.game.gameHeader.getRandomSeed() ^ 0x4a534d50u);
		seeded = true;
	}
	try
	{
		Observations observations(gui.game, -1);
		Host host;
		host.tick = gui.game.stepCounter;
		host.team = -1;
		host.width = gui.game.map.getW();
		host.height = gui.game.map.getH();
		host.random = [this] { return random(); };
		host.query = [&](const auto &name, const auto &args, const Script::QueryBudget &budget)
		{
			if (name == "interface")
			{
				budget(1, presentation.encode().size() * NativeValueCost);
				return presentation;
			}
			return observations.query(name, args, budget);
		};
		auto result = runtime->invoke(source, state, !initialized, host);
		auto prepared = prepareEffects(result.effects, presentation, gui.game);
		auto shown = prepareInterface(prepared.presentation);
		// No potentially allocating work follows: publish the entire accepted
		// callback (effects, global snapshot, presentation and private RNG).
		commitEffects(prepared, gui.game);
		state = std::move(result.state);
		presentation = std::move(prepared.presentation);
		initialized = true;
		applyInterface(std::move(shown), gui, true);
	}
	catch (const std::bad_alloc &)
	{
		runtime->discard();
		random = checkpoint;
		seeded = wasSeeded;
		throw HostFailure("Native allocation failed in JavaScript scenario");
	}
	catch (const HostFailure &)
	{
		runtime->discard();
		random = checkpoint;
		seeded = wasSeeded;
		throw;
	}
	catch (const std::exception &failure)
	{
		runtime->discard();
		random = checkpoint;
		seeded = wasSeeded;
		throw ScenarioFailure(failure.what());
	}
}
void JavaScriptMap::save(GAGCore::OutputStream *s) const
{
	s->writeEnterSection("JavaScriptMap");
	s->writeUint32(ProfileVersion, "profile");
	s->writeText(state.encode(), "state");
	s->writeText(presentation.encode(), "presentation");
	s->writeUint8(initialized, "initialized");
	s->writeUint8(seeded, "seeded");
	s->writeText(rng(random), "random");
	s->writeLeaveSection();
}
void JavaScriptMap::load(GAGCore::InputStream *s)
{
	s->readEnterSection("JavaScriptMap");
	if (s->readUint32("profile") != ProfileVersion)
		throw std::runtime_error("Unsupported map JavaScript profile");
	runtime->discard();
	state = Value::decode(s->readText("state"));
	presentation = Value::decode(s->readText("presentation"));
	initialized = s->readUint8("initialized");
	seeded = s->readUint8("seeded");
	std::istringstream input(s->readText("random") + " ");
	input.imbue(std::locale::classic());
	if (!(input >> random))
		throw std::runtime_error("Invalid map script RNG");
	s->readLeaveSection();
}
unsigned JavaScriptMap::checksum(GameGUI *gui) const
{
	unsigned h = mix(2166136261u, state.encode());
	h = mix(h, presentation.encode());
	h = mix(h, rng(random));
	h = (h ^ initialized) * 16777619u;
	h = (h ^ seeded) * 16777619u;
	if (gui)
	{
		auto &o = gui->game.objectives;
		for (int i = 0; i < o.getNumberOfObjectives(); ++i)
		{
			h = (h ^ o.isObjectiveVisible(i)) * 16777619u;
			h = (h ^ o.isObjectiveComplete(i)) * 16777619u;
			h = (h ^ o.isObjectiveFailed(i)) * 16777619u;
		}
		auto &hints = gui->game.gameHints;
		for (int i = 0; i < hints.getNumberOfHints(); ++i)
			h = (h ^ hints.isHintVisible(i)) * 16777619u;
	}
	return h;
}
} // namespace Script
