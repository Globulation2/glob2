// SPDX-License-Identifier: GPL-3.0-or-later
#include "CustomGameScreen.h"
#include "GlobalContainer.h"
#include "LobbyControls.h"
#include "LobbyMapPreview.h"
#include "AINames.h"
#include "GenerationService.h"
#include "gui/MobileSafeArea.h"
#include "gui/FrontendLayout.h"
#include <InterfacePresentation.h>
#include <StringTable.h>
#include <FormatableString.h>

namespace
{
std::string tr(const std::string &text)
{
	return Toolkit::getStringTable()->getString("[" + text + "]");
}
std::vector<std::string> translated(std::vector<std::string> labels)
{
	for (auto &s : labels)
		s = tr(s);
	return labels;
}
} // namespace

bool CustomGameScreen::usesResponsiveViewport() const
{
	return GAGCore::phonePresentationRequested();
}
void CustomGameScreen::cancelExecutionInput()
{
	controls->cancelTouch();
}

void CustomGameScreen::renderPhoneLobby()
{
	auto &ui = *controls;
	const auto safe = GAGCore::mobileDialogSafe(globalContainer->gfx);
	const int x = int(safe.x) + 12, w = int(safe.w) - 24, top = int(safe.y) + 8;
	const int bottom = int(safe.y + safe.h) - 8;
	ui.box({x - 4, top - 4, w + 8, bottom - top + 8}, Color(232, 237, 218), 8);
	const auto tabs = translated({"Map", "Opponents", "Review"});
	for (int i = 0; i < 3; ++i)
		ui.button(
			"tab/" + std::to_string(i), {x + i * w / 3, top, w / 3 - 4, 48}, tabs[i],
			[this, i]
			{
				phonePage = PhonePage::Main;
				activateGroup(groups[i]);
				controls->resetFocus();
			},
			currentTab == groups[i], true, false, "little");
	for (auto &entry : ui.regions)
		entry.second.box = {0, 0, 0, 0};
	const int y = top + 56, h = std::max(1, bottom - y - 72);
	if (phonePage == PhonePage::Rules)
		renderRules(x, y, w, h);
	else
	{
		const int region = phonePage == PhonePage::MapSettings ? 103
						   : phonePage == PhonePage::Maps      ? 104
						   : currentTab == groups[1]           ? 101
						   : currentTab == groups[2]           ? 102
															   : 100;
		ui.beginRegion(region, {x, y, w, h});
		int row = y - ui.regions[region].offset;
		const int start = row;
		auto label = [&](const std::string &text)
		{ row += ui.paragraph(x + 4, row, w - 20, text, "standard", false, false) + 8; };
		auto button = [&](const std::string &id, const std::string &text, auto callback,
						  bool selected = false)
		{
			ui.button(id, {x, row, w - 12, 48}, text, callback, selected);
			row += 56;
		};
		if (currentTab == groups[2])
		{
			// The review is a visual receipt of the exact draft, not another
			// configuration form. Keep the map and colony colors recognizable.
			const auto mapName = setup.random
									 ? tr(GenerationRequest::methodName(setup.generator.method))
									 : mapHeader.getMapName();
			const int cardH = std::max(
				112, ui.paragraph(0, 0, w - 136, mapName, "standard", false, false, false) + 68);
			ui.box({x, row, w - 12, cardH}, ui.panel, 8);
			if (preview->isThumbnailLoaded())
			{
				preview->setScreenPosition(x + 8 - (gfx->getW() - 640) / 2,
										   row + 8 - (gfx->getH() - 480) / 2);
				preview->setDimensions(96, 96);
				preview->paint();
			}
			ui.paragraph(x + 112, row + 8, w - 136, mapName, "standard", false, false);
			ui.button("review/map", {x + 112, row + cardH - 56, w - 132, 48}, tr("Edit map"),
					  [this] { activateGroup(groups[0]); });
			row += cardH + 16;
			label(GAGCore::FormattableString(tr("%0 colonies · %1"))
					  .arg(setup.activeColonies())
					  .arg(tr(setup.format)));
			const auto controllers = translated({"You", "AI", "You + AI", "Closed"});
			for (int i = 0; i < setup.capacity; ++i)
			{
				const auto &colony = setup.colonies[i];
				if (colony.controller == CustomGameSetup::Closed)
					continue;
				const auto color = i < int(preview->starts.size()) ? preview->starts[i].color
																   : Color(160, 172, 149);
				const auto name = (colony.controller == CustomGameSetup::Computer ||
								   colony.controller == CustomGameSetup::Shared)
									  ? GAGCore::FormattableString(tr("%0 · %1"))
											.arg(controllers[colony.controller])
											.arg(AINames::getAIText(colony.ai))
									  : controllers[colony.controller];
				const std::string summary = GAGCore::FormattableString(tr("%0 · Team %1"))
												.arg(name)
												.arg(colony.alliance + 1);
				const int rh = std::max(
					44, ui.paragraph(0, 0, w - 64, summary, "standard", false, false, false) + 16);
				ui.box({x, row, w - 12, rh}, ui.panel, 6);
				ui.box({x + 8, row + 10, 24, 24}, color, 4);
				ui.paragraph(x + 40, row + 8, w - 64, summary, "standard", false, false);
				row += rh + 4;
			}
			button("review/players", tr("Edit opponents"), [this] { activateGroup(groups[1]); });
			auto speed = globalContainer->settings;
			speed.gameSpeed = setup.speed;
			label(
				GAGCore::FormattableString(tr("%0 · %1")).arg(tr("Rules")).arg(tr(setup.ruleset)));
			label(GAGCore::FormattableString(tr("%0 · %1"))
					  .arg(tr(setup.prestige ? "Conquest or prestige" : "Conquest only"))
					  .arg(speed.getGameSpeedText()));
			button("review/rules", tr("Rules"), [this] { phonePage = PhonePage::Rules; });
			if (setup.random && validMap)
				button("review/quality", tr("Start quality"), [this] { showStartQuality(); });
		}
		else if (currentTab == groups[1])
		{
			int aiCount = 0;
			for (int i = 0; i < setup.capacity; ++i)
				if (setup.colonies[i].controller == CustomGameSetup::Computer ||
					setup.colonies[i].controller == CustomGameSetup::Shared)
					++aiCount;

			if (setup.random)
			{
				const auto count = GenerationRequest::control(setup.generator.method, "teams");
				std::vector<std::string> counts;
				for (int value : count.values())
					counts.push_back(GAGCore::FormattableString(tr("Colonies: %0")).arg(value));
				ui.dropdown(
					"players/capacity", {x, row, (w - 20) / 2, 48}, counts,
					count.indexOf(setup.capacity),
					[this, count](int index)
					{
						setup.setCapacity(count.valueAt(index));
						setup.generator.nbTeams = setup.capacity;
						++setup.mapRevision;
						invalidate();
					},
					{}, "",
					GAGCore::FormattableString(tr("%0 colonies · %1 AI"))
						.arg(setup.capacity)
						.arg(aiCount));
			}
			ui.dropdown("format",
						{setup.random ? x + (w - 20) / 2 + 8 : x, row,
						 setup.random ? (w - 20) / 2 : w - 12, 48},
						translated({"FFA", "2 vs 2", "You vs all"}),
						setup.format == "FFA"      ? 0
						: setup.format == "2 vs 2" ? 1
												   : 2,
						[this](int v) { setup.presetTeams(v); },
						{true, setup.activeColonies() == 4,
						 bool(setup.humanColony()) && setup.activeColonies() > 1});
			row += 56;
			for (int i = 0; i < setup.capacity; ++i)
			{
				auto &colony = setup.colonies[i];
				const bool hasAI = colony.controller == CustomGameSetup::Computer ||
								   colony.controller == CustomGameSetup::Shared;
				const bool inlineRow = w >= 480;
				const int fieldY = row + (hasAI && !inlineRow ? 56 : 0);
				const auto color = i < int(preview->starts.size()) ? preview->starts[i].color
																   : Color(160, 172, 149);
				ui.box({x, row + 10, 28, 28}, color, 4);
				ui.text(x + 36, row + 14, colonyLabel(i), "standard",
						inlineRow ? 76 : w - (hasAI ? 172 : 220));

				std::vector<bool> enabled;
				for (int j = 0; j < 4; ++j)
				{
					auto draft = setup;
					enabled.push_back(draft.setController(i, (CustomGameSetup::Controller)j));
				}
				const auto id = "colony/" + std::to_string(i);
				const char *controllerSymbols[] = {"●", "◆", "●◆", "×"};
				auto controllerChoices = translated({"You", "AI", "You + AI", "Closed"});
				for (size_t j = 0; j < controllerChoices.size(); ++j)
					controllerChoices[j] =
						std::string(controllerSymbols[j]) + "  " + controllerChoices[j];
				ui.dropdown(
					id + "/controller", {x + w - 68, row, 56, 48}, controllerChoices,
					colony.controller, [this, i](int v)
					{ setup.setController(i, (CustomGameSetup::Controller)v); }, enabled,
					tr("Choose who controls this colony"), controllerSymbols[colony.controller]);
				if (hasAI)
				{
					std::vector<std::string> names;
					for (int ai : AINames::selectionOrder())
						names.push_back(AINames::getAISelectorText(ai));
					ui.dropdown(
						id + "/ai",
						{inlineRow ? x + 120 : x, fieldY, inlineRow ? w - 364 : w - 124, 48}, names,
						AINames::selectionIndex(colony.ai),
						[this, i](int v)
						{
							setup.colonies[i].ai =
								(AI::ImplementationID)AINames::selectionOrder()[v];
						},
						{}, "", AINames::getAIText(colony.ai));
					ui.button(id + "/info", {x + w - 124, row, 48, 48}, "(i)",
							  [this, i] { showAIProfile(i); });
				}
				std::vector<std::string> teams;
				for (int j = 0; j < setup.capacity; ++j)
					teams.push_back(GAGCore::FormattableString(tr("Team %0")).arg(j + 1));
				ui.dropdown(id + "/team",
							{x + w - (hasAI ? (inlineRow ? 236 : 116) : 180), fieldY, 104, 48},
							teams, colony.alliance,
							[this, i](int v)
							{
								setup.colonies[i].alliance = v;
								setup.format = "Custom teams";
							});
				row = fieldY + 56;
			}
		}
		else
		{
			if (phonePage == PhonePage::Main)
			{
				ui.segments("map/mode", {x, row, w - 12, 48},
							translated({"Premade maps", "Random map"}), setup.random,
							[this](int v) { setMapMode(v); });
				row += 60;
			}
			// Short landscape phones need to see the whole map at once. Put
			// its actions beside a fitted thumbnail instead of clipping a tall square.
			const bool shortLandscape = safe.w > safe.h && safe.h < 480;
			const int size =
				shortLandscape ? std::max(48, std::min(160, h - 60)) : std::min(w - 24, 240);
			const int previewBottom = row + size + 8;
			if (phonePage == PhonePage::Main && preview->isThumbnailLoaded())
			{
				ui.button("map/preview", {x, row, shortLandscape ? size : w - 12, size}, "",
						  [this]
						  {
							  if (setup.random)
								  chooseLandscape();
							  else
								  phonePage = PhonePage::Maps;
						  });
				preview->setScreenPosition(x + (shortLandscape ? 0 : (w - size) / 2) -
											   (gfx->getW() - 640) / 2,
										   row - (gfx->getH() - 480) / 2);
				preview->setDimensions(size, size);
				preview->paint();
				if (!shortLandscape)
					row += size + 12;
			}
			if (phonePage == PhonePage::Main)
			{
				auto mapAction =
					[&](const std::string &id, const std::string &caption, auto callback)
				{
					const int actionX = shortLandscape ? x + size + 12 : x;
					const int actionW = shortLandscape ? w - size - 24 : w - 12;
					ui.button(id, {actionX, row, actionW, 48}, caption, callback);
					row += 56;
				};
				mapAction("landscape",
						  setup.random ? tr(GenerationRequest::methodName(setup.generator.method))
									   : mapHeader.getMapName(),
						  [this]
						  {
							  if (setup.random)
								  chooseLandscape();
							  else
								  phonePage = PhonePage::Maps;
						  });
				if (setup.random)
				{
					mapAction("map/randomize", tr("Randomize"),
							  [this]
							  {
								  invalidate();
								  previewDue = SDL_GetTicks();
							  });
					mapAction("map/settings", tr("Map settings"),
							  [this] { phonePage = PhonePage::MapSettings; });
				}
				if (shortLandscape)
					row = std::max(row, previewBottom);
			}
			else if (!setup.random)
			{
				if (separateMapLibraries)
				{
					ui.segments("map/library", {x, row, w - 12, 48},
								translated({"Built-in maps", "Your maps"}), userMaps,
								[this](int v)
								{
									userMaps = v;
									listMaps();
								});
					row += 56;
				}
				for (size_t i = 0; i < mapPaths.size(); ++i)
					button(
						"map/entry/" + std::to_string(i), mapNames[i],
						[this, i]
						{
							if (loadMap(mapPaths[i]))
								phonePage = PhonePage::Main;
						},
						librarySelection[userMaps] == mapPaths[i]);
			}
			else
			{
				auto control = [&](const GenerationRequest::Control &c)
				{
					label(tr(c.label));
					const auto id = "generator/" + c.id;
					auto apply = [this, c](int value)
					{
						c.set(setup.generator, value);
						if (c.id == "teams")
							setup.setCapacity(setup.generator.nbTeams);
						++setup.mapRevision;
						invalidate();
					};
					if (c.isToggle())
						ui.checkbox(id, {x, row, w - 12, 48}, tr(c.label),
									c.get(setup.generator) != 0,
									[apply](bool v) { apply(v ? 1 : 0); });
					else if (c.powerOfTwo || !c.allowedValues.empty())
					{
						std::vector<std::string> values;
						for (int v : c.values())
							values.push_back(c.isChoice() ? tr(c.valueLabel(v))
														  : std::to_string(c.displayValue(v)));
						ui.dropdown(id, {x, row, w - 12, 48}, values,
									c.indexOf(c.get(setup.generator)),
									[c, apply](int v) { apply(c.valueAt(v)); });
					}
					else
						ui.stepper(id, {x, row, w - 12, 48}, c.get(setup.generator), c.minimum,
								   c.maximum, apply, c.step);
					row += 64;
				};
				for (const auto &c : GenerationRequest::sharedControls())
					if (c.id != "workers")
						control(c);
				for (const auto &c : GenerationRequest::controls(setup.generator.method))
					control(c);
			}
		}
		ui.endRegion(row - start);
	}
	gfx->setClipRect();
	std::string error = setup.validation();
	if (!setup.random && !validMap)
		error = tr("Select a valid map.");
	if (!error.empty())
		ui.text(x, bottom - 76, tr(error), "standard", w, true);
	ui.button("back", {x, bottom - 52, 88, 48}, tr("Back"),
			  [this]
			  {
				  if (phonePage != PhonePage::Main)
					  phonePage = PhonePage::Main;
				  else if (currentTab == groups[2])
					  activateGroup(groups[1]);
				  else if (currentTab == groups[1])
					  activateGroup(groups[0]);
				  else
					  endExecute(CANCEL);
			  });
	if (phonePage != PhonePage::Main)
	{
		ui.button(
			"page/done", {x + 96, bottom - 52, w - 96, 48}, tr("Done"),
			[this] { phonePage = PhonePage::Main; }, true);
	}
	else if (currentTab != groups[2])
	{
		ui.button(
			"next", {x + 96, bottom - 52, w - 96, 48}, tr("Next"),
			[this] { activateGroup(currentTab == groups[0] ? groups[1] : groups[2]); }, true);
	}
	else
	{
		const bool ready =
			validMap && !previewBusy() && (!setup.random || previewRevision == setup.mapRevision);
		ui.button(
			"start", {x + 96, bottom - 52, w - 96, 48},
			tr(setup.humanColony() ? "Play this map" : "Watch game"),
			[this] { onAction(nullptr, BUTTON_SHORTCUT, OK, 0); }, true, error.empty() && ready);
	}
}

bool CustomGameChoiceScreen::usesResponsiveViewport() const
{
	return GAGCore::phonePresentationRequested();
}
void CustomGameChoiceScreen::cancelExecutionInput()
{
	controls->cancelTouch();
}
