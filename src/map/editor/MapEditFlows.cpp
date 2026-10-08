#include "MapAssetBundle.h"
// SPDX-License-Identifier: GPL-3.0-or-later
// Editor flows that must not lose work or leave the editor: quit and window
// close, replacing or sharing the map, rerolling the terrain look, fertility
// progress and definition imports from the device.

#include "MapEdit.h"
#include "GlobalContainer.h"
#include "PhoneEditor.h"
#include <FileManager.h>
#include <GraphicContext.h>
#include <StringTable.h>
#include <Toolkit.h>
#include <SDL3/SDL.h>
#include <sstream>

namespace
{
std::string text(const char *key)
{
	return GAGCore::Toolkit::getStringTable()->getString(key);
}
} // namespace

void MapEdit::openConfirm(ConfirmPurpose purpose)
{
	using Choice = EditorConfirmDialog::Choice;
	std::string title = text("[editor unsaved title]"), message;
	std::vector<Choice> choices;
	confirmChoices.clear();
	auto add = [&](const char *label, FlowChoice code, bool primary = false)
	{
		choices.push_back({text(label), primary});
		confirmChoices.push_back(code);
	};
	switch (purpose)
	{
		case ConfirmPurpose::Quit:
		case ConfirmPurpose::QuitApplication:
		case ConfirmPurpose::LoadUnsaved:
			message = text(purpose == ConfirmPurpose::LoadUnsaved ? "[editor load unsaved]" : "[editor quit unsaved]");
			// The keys choice/0..2 keep the order of the former quit prompt.
			add("[editor save]", ChoiceSave, true);
			add("[editor discard]", ChoiceDiscard);
			add("[Cancel]", ChoiceCancel);
			break;
		case ConfirmPurpose::ShareSaveFirst:
			title = text("[maps share online]");
			message = text(savedFilename.empty() ? "[editor share never saved]" : "[editor share unsaved]");
			add("[editor save and share]", ChoiceSave, true);
			if (!savedFilename.empty())
				add("[editor share saved version]", ChoiceShareSaved);
			add("[Cancel]", ChoiceCancel);
			break;
		case ConfirmPurpose::RerollTerrain:
			title = text("[Reroll terrain look]");
			message = text("[editor reroll confirm]");
			add("[editor reroll]", ChoiceConfirm);
			add("[Cancel]", ChoiceCancel, true);
			break;
		case ConfirmPurpose::None:
			return;
	}
	performAction("scroll horizontal stop");
	performAction("scroll vertical stop");
	confirmPurpose = purpose;
	confirmDialog = std::make_unique<EditorConfirmDialog>(title, message, std::move(choices), int(confirmChoices.size()) - 1);
	attachDialog(*confirmDialog);
}

void MapEdit::resolveConfirm(int index)
{
	const auto purpose = confirmPurpose;
	const FlowChoice choice = index >= 0 && index < int(confirmChoices.size()) ? confirmChoices[std::size_t(index)] : ChoiceCancel;
	confirmDialog.reset();
	confirmPurpose = ConfirmPurpose::None;
	confirmChoices.clear();
	if (choice == ChoiceCancel)
		return;
	switch (purpose)
	{
		case ConfirmPurpose::Quit:
		case ConfirmPurpose::QuitApplication:
			if (choice == ChoiceSave)
			{
				closeDialogsForFlow();
				doQuitAfterLoadSave = true;
				fullQuitAfterSave = purpose == ConfirmPurpose::QuitApplication;
				performAction("open save screen");
			}
			else if (purpose == ConfirmPurpose::QuitApplication)
				doFullQuit = true;
			else
				editing = false;
			break;
		case ConfirmPurpose::LoadUnsaved:
			if (choice == ChoiceSave)
			{
				closeDialogsForFlow();
				loadAfterSave = true;
				performAction("open save screen");
			}
			else
				openLoadDialog();
			break;
		case ConfirmPurpose::ShareSaveFirst:
			if (choice == ChoiceSave)
			{
				closeDialogsForFlow();
				shareAfterSave = true;
				performAction("open save screen");
			}
			else if (choice == ChoiceShareSaved)
				pendingShareFilename = savedFilename;
			break;
		case ConfirmPurpose::RerollTerrain:
			if (choice == ChoiceConfirm)
				performAction("reroll terrain look");
			break;
		case ConfirmPurpose::None:
			break;
	}
}

void MapEdit::requestApplicationQuit()
{
	if (!hasMapBeenModified)
	{
		doFullQuit = true;
		return;
	}
	// A save in progress finishes first; its dialog then ends the application.
	if (showingSave && loadSaveScreen && loadSaveScreen->isPersisting())
	{
		quitAfterSave = true;
		return;
	}
	openConfirm(ConfirmPurpose::QuitApplication);
}

void MapEdit::closeDialogsForFlow()
{
    setLibraryDialog.reset();
	if (showingMenuScreen)
		performAction("close menu screen");
	if (showingLoad)
		performAction("close load screen");
	if (showingScriptEditor)
		performAction("close scenario editor");
	if (showingTeamsEditor)
		performAction("close teams editor");
	if (isShowingAreaName)
		performAction("close area name");
	deviceSelection.reset();
}

void MapEdit::clearSaveFollowUps()
{
	fullQuitAfterSave = shareAfterSave = loadAfterSave = false;
}

void MapEdit::saveSucceeded()
{
	const bool share = shareAfterSave, load = loadAfterSave, quit = fullQuitAfterSave;
	clearSaveFollowUps();
	if (quit)
	{
		doFullQuit = true;
		return;
	}
	if (doQuitAfterLoadSave)
		return;
	showStatus(text("[editor map saved]"));
	if (share)
		pendingShareFilename = savedFilename;
	else if (load)
		openLoadDialog();
}

void MapEdit::finishShare(bool shared)
{
	if (shared)
		showStatus(text("[editor map shared]"));
}

void MapEdit::openLoadDialog()
{
	performAction("unselect");
	performAction("scroll horizontal stop");
	performAction("scroll vertical stop");
	loadSaveScreen = std::make_unique<LoadSaveDialog>("maps", "map", true, text("[load map]"), game.mapHeader.getMapName().c_str(),
													  glob2FilenameToName, glob2NameToFilename, Glob2UI::Surface::Editor);
	attachDialog(*loadSaveScreen);
	showingLoad = true;
}

void MapEdit::openFertilityProgress()
{
	progressDialog = std::make_unique<EditorProgressDialog>(game.map, text("[Computing Fertility]"));
	attachDialog(*progressDialog);
}

void MapEdit::finishFertilityProgress()
{
	const bool completed = progressDialog->result() == EditorProgressDialog::COMPLETED;
	progressDialog.reset();
	finishFertility(completed);
}

void MapEdit::importTerrainJson(const std::string &json)
{
	if (json.size() > TerrainRegistry::MaximumDefinitionBytes)
		throw std::runtime_error("Terrain definitions exceed 32 MiB");
	game.map.importTerrainDefinitions(json);
	minimap.resetMinimapDrawing();
	hasMapBeenModified = true;
	fertilityRequested = true;
}

void MapEdit::importResourceJson(const std::string &json)
{
	if (json.size() > ResourceRegistry::MaximumDefinitionBytes)
		throw std::runtime_error("Resource definitions exceed 32 MiB");
	game.map.installResourceDefinitions(json);
	game.gameHeader.setResourceExperiments(game.map.resourceRegistry().experiments());
	minimap.resetMinimapDrawing();
	hasMapBeenModified = true;
	fertilityRequested = true;
}

void MapEdit::beginDeviceImport()
{
	if (!GAGCore::ApplicationHost::canImportFiles() || !showingLoad || !(importingTerrain || importingResources || importingSet))
		return;
	deviceSelection = GAGCore::ApplicationHost::selectFile("json");
	GAGCore::ApplicationHost::importChanged("selecting");
	loadSaveScreen->showNotice(text("[select import file]"));
}

void MapEdit::pollDeviceImport()
{
	if (!deviceSelection)
		return;
	using State = GAGCore::ApplicationHost::FileSelectionState;
	const auto state = deviceSelection->state();
	if (state == State::Pending)
		return;
	auto selection = std::move(deviceSelection);
	// The picker can outlive its dialog only if a flow closed it meanwhile.
	if (!showingLoad || !loadSaveScreen)
		return;
	if (state != State::Selected)
	{
		const bool cancelled = state == State::Cancelled;
		GAGCore::ApplicationHost::importChanged(cancelled ? "cancelled" : "invalid");
		if (cancelled)
			loadSaveScreen->showNotice(text("[import cancelled]"));
		else
			loadSaveScreen->showLoadFailure(text("[import failed]"));
		return;
	}
	const bool resources = importingResources;
	try
	{
		const auto file = selection->takeFile();
		const std::string json(file.bytes.begin(), file.bytes.end());
		if (importingSet)
            importSetJson(json);
        else if (resources)
			importResourceJson(json);
		else
			importTerrainJson(json);
	}
	catch (const std::exception &error)
	{
		GAGCore::ApplicationHost::importChanged("invalid");
		loadSaveScreen->showLoadFailure(error.what());
		return;
	}
	GAGCore::ApplicationHost::importChanged("succeeded");
	performAction("close load screen");
	performAction(resources ? "open resource palette" : "open terrain palette");
}

std::string MapEdit::scenarioFingerprint()
{
	// Everything the scenario editor's OK commits, joined with separators that
	// cannot appear in its single-line fields.
	std::ostringstream out;
	const char separator = '\x1f';
	out << game.sgslScript.sourceCode << separator << int(game.mapscript.getMapScriptMode()) << separator
		<< game.mapscript.getMapScript() << separator << game.missionBriefing << separator;
	for (int i = 0; i < game.objectives.getNumberOfObjectives(); ++i)
		out << int(game.objectives.getObjectiveType(i)) << ':' << game.objectives.getScriptNumber(i) << ':'
			<< game.objectives.getGameObjectiveText(i) << separator;
	out << separator;
	for (int i = 0; i < game.gameHints.getNumberOfHints(); ++i)
		out << game.gameHints.getScriptNumber(i) << ':' << game.gameHints.getGameHintText(i) << separator;
	return out.str();
}

bool MapEdit::teamSlotsChanged() const
{
	if (!teamsEditor)
		return false;
	for (int i = 0; i < Team::MAX_COUNT && std::size_t(i) < teamSlotsAtOpen.size(); ++i)
	{
		const auto &before = teamSlotsAtOpen[std::size_t(i)];
		const auto &after = teamsEditor->slot(i);
		if (before.active != after.active || (after.active && (before.color != after.color || before.ai != after.ai || before.ally != after.ally)))
			return true;
	}
	return false;
}

widgetRectangle MapEdit::fertilityChipRect() const
{
	auto *font = globalContainer->standardFont;
	const std::string label = text("[editor fertility stale]");
	const int width = font->getStringWidth(label) + 24, height = font->getStringHeight(label) + 12;
	const int mapWidth = globalContainer->gfx->getW() - dockWidth();
	return widgetRectangle(std::max(0, (mapWidth - width) / 2), 24, width, height);
}

bool MapEdit::handleFlowEvent(const SDL_Event &raw)
{
	// With a dock the refresh lives beside its fertility switch.
	if (phone || dock || hasDialog() || !fertilityOverlayStale())
	{
		fertilityChipPressed = false;
		return false;
	}
	if ((raw.type != SDL_EVENT_MOUSE_BUTTON_DOWN && raw.type != SDL_EVENT_MOUSE_BUTTON_UP) || raw.button.button != SDL_BUTTON_LEFT)
		return false;
	SDL_Event event = raw;
	GAGCore::GraphicContext::translateMouseEvent(&event);
	auto chip = fertilityChipRect();
	const bool inside = chip.is_in(int(event.button.x), int(event.button.y));
	if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
	{
		fertilityChipPressed = inside;
		return inside;
	}
	if (!fertilityChipPressed)
		return false;
	fertilityChipPressed = false;
	if (inside)
		performAction("refresh fertility");
	return true;
}

void MapEdit::drawFlowOverlays()
{
	if (globalContainer->runNoX)
		return;
	auto *gfx = globalContainer->gfx;
	auto *font = globalContainer->standardFont;
	gfx->setClipRect();
	if (fertilityOverlayStale() && !phone && !dock)
	{
		const auto chip = fertilityChipRect();
		gfx->drawFilledRect(chip.x, chip.y, chip.width, chip.height, 46, 30, 72, 230);
		gfx->drawRect(chip.x, chip.y, chip.width, chip.height, 176, 148, 232);
		gfx->drawString(chip.x + 12, chip.y + 6, font, text("[editor fertility stale]"));
	}
}

void MapEdit::importSetJson(const std::string& json, const std::vector<std::string>& selected)
{
    game.map.importSet(json, selected);
    game.gameHeader.setResourceExperiments(game.map.resourceRegistry().experiments());
    minimap.resetMinimapDrawing(); hasMapBeenModified = true; fertilityRequested = true;
}
