// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sim/ClientCommandSink.h"
#include "sim/ClientEvents.h"
#include <vector>

// Stable script endpoint for all interpreters, including USL's retained native
// values. Only the simulation owner uses it during a threaded session. Queries
// use its own mirror and never depend on when the graphics owner drains events.
class ScriptClientChannel final : public ClientCommandSink
{
public:
	struct Choice { std::string key, alias; bool enabled; };
	explicit ScriptClientChannel(ClientCommandSink* direct) : direct(direct) {}
	// Lifecycle boundaries only: no script may execute while switching modes.
	void start(ClientEvents& destination, std::vector<Choice> buildingChoices, std::vector<Choice> flagChoices)
	{
		buildings = std::move(buildingChoices);
		flags = std::move(flagChoices);
		events = &destination;
	}
	void stop() { events = nullptr; buildings.clear(); flags.clear(); }

	void enableBuildingsChoice(const std::string& name) override { choice(false, name, true); }
	void disableBuildingsChoice(const std::string& name) override { choice(false, name, false); }
	void enableFlagsChoice(const std::string& name) override { choice(true, name, true); }
	void disableFlagsChoice(const std::string& name) override { choice(true, name, false); }
	bool isBuildingEnabled(const std::string& name) override { return enabled(false, name); }
	bool isFlagEnabled(const std::string& name) override { return enabled(true, name); }
	void enableGUIElement(int id) override { send({ScriptPresentation::Element, id, true}); }
	void disableGUIElement(int id) override { send({ScriptPresentation::Element, id, false}); }
	void setHighlight(int id, bool on) override { send({ScriptPresentation::Highlight, id, on}); }
	void setSwallowSpaceKey(bool on) override { send({ScriptPresentation::SwallowSpace, 0, on}); }
	void showScriptText(const std::string& text) override { send({ScriptPresentation::ShowText, 0, false, text}); }
	void showScriptTextTr(const std::string& text, const std::string& lang) override { send({ScriptPresentation::TranslatedText, 0, false, text, lang}); }
	void hideScriptText() override { send({ScriptPresentation::HideText}); }
	void setScriptPresentationText(std::string text, bool history) override { send({ScriptPresentation::SetText, 0, history, std::move(text)}); }

private:
	ClientCommandSink* direct;
	ClientEvents* events = nullptr;
	std::vector<Choice> buildings, flags;
	void send(ScriptPresentation value)
	{
		if (events) events->push(std::move(value));
		else if (direct) value.apply(*direct);
	}
	void choice(bool flag, const std::string& name, bool value)
	{
		if (events)
			for (auto& item : flag ? flags : buildings)
				if (item.key == name || item.alias == name) item.enabled = value;
		send({flag ? ScriptPresentation::FlagChoice : ScriptPresentation::BuildingChoice, 0, value, name});
	}
	bool enabled(bool flag, const std::string& name)
	{
		if (!events) return direct && (flag ? direct->isFlagEnabled(name) : direct->isBuildingEnabled(name));
		for (const auto& item : flag ? flags : buildings)
			if (item.key == name || item.alias == name) return item.enabled;
		return false;
	}
};
