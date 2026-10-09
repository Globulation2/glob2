// SPDX-License-Identifier: GPL-3.0-or-later

#include <GAG.h>
#include "LoadSaveDialog.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include <optional>
#include "FormatableString.h"
#include <ApplicationHost.h>
#include "PhoneEditor.h"
#include "ScriptEditorScreen.h"
#include "Utilities.h"
#include <SDL3/SDL.h>

bool MapEdit::performViewAction(const std::string& action, float relMouseX, float relMouseY)
{
	if(action=="scroll drag start")
	{
		isScrollDragging=true;
	}
	else if(action=="scroll drag motion")
	{
		const int direction = isLeftScrollDragging ? -1 : 1;
		camera.originX+=direction*relMouseX/camera.zoom;
		camera.originY+=direction*relMouseY/camera.zoom;
		camera.normalize();viewportX=camera.tileX();viewportY=camera.tileY();
		viewportX&=game.map.getMaskW();
		viewportY&=game.map.getMaskH();
	}
	else if(action=="scroll drag stop")
	{
		isScrollDragging=false;
	}
	else if(action=="switch to building view")
	{
		panelMode=AddBuildings;
		enableOnlyGroup("building view");
		performAction("unselect");
	}
	else if(action=="switch to flag view")
	{
		panelMode=AddFlagsAndZones;
		enableOnlyGroup("flag view");
		performAction("unselect");
	}
	else if(action=="switch to terrain view")
	{
		panelMode=Terrain;
		enableOnlyGroup("terrain view");
		performAction("unselect");
	}
	else if(action=="switch to teams view")
	{
		panelMode=Teams;
		enableOnlyGroup("teams view");
		performAction("unselect");
	}
	else if(action=="unselect")
	{
		selectionName="";
		brush.unselect();
		selectionMode=PlaceNothing;
		brushType=NoBrush;
		terrainType=TerrainSelector::NoTerrain;
		placingUnit=NoUnit;
		selectedUnitGID=NOGUID;
		view.selectedUnit=NULL;
        view.selectedBuilding=nullptr;
		deleteButton->setUnselected();
		areasButton->setUnselected();
		noResourceGrowthButton->setUnselected();
		isDraggingZone=false;
		isDraggingTerrain=false;
		isDraggingDelete=false;
		isDraggingArea=false;
		isDraggingNoResourceGrowthArea=false;
		if(panelMode==UnitEditor)
			performAction("switch to building view");
	}
	else if(action=="change menu")
	{
		if(panelMode==AddBuildings)
			performAction("switch to flag view");
		else if(panelMode==AddFlagsAndZones)
			performAction("switch to terrain view");
		else if(panelMode==Terrain)
			performAction("switch to teams view");
		else if(panelMode==Teams)
			performAction("switch to building view");
		else
			performAction("switch to building view");
	}
	else if(action=="minimap drag start")
	{
		isDraggingMinimap=true;
		minimapMouseToPos(mouseX-globalContainer->gfx->getW()+RIGHT_MENU_WIDTH-RIGHT_MENU_OFFSET, mouseY, &viewportX, &viewportY, true);
	}
	else if(action=="minimap drag motion")
	{
		minimapMouseToPos(mouseX-globalContainer->gfx->getW()+RIGHT_MENU_WIDTH-RIGHT_MENU_OFFSET, mouseY, &viewportX, &viewportY, true);
	}
	else if(action=="minimap drag stop")
	{
		isDraggingMinimap=false;
	}
	else if(action=="open menu screen")
	{
		performAction("unselect");
		performAction("scroll horizontal stop");
		performAction("scroll vertical stop");
		menuScreen=std::make_unique<MapEditMenuScreen>();
		attachDialog(*menuScreen);
		showingMenuScreen=true;
	}
	else if(action=="close menu screen")
	{
		menuScreen.reset();
		showingMenuScreen=false;
	}
	else if (action == "open set library") {
        setLibraryDialog = std::make_unique<SetLibraryDialog>(game.map,[this](std::string bytes, std::vector<std::string> selected) { importSetJson(bytes, selected); },[this]{game.gameHeader.setResourceExperiments(game.map.resourceRegistry().experiments());minimap.resetMinimapDrawing();hasMapBeenModified=true;fertilityRequested=true;});
        attachDialog(*setLibraryDialog);
    }
    else if (action == "import terrain definitions" || action == "import resource definitions" || action == "import set")
	{
		performAction("unselect");
		loadSaveScreen = std::make_unique<LoadSaveDialog>(
			action == "import set" ? "sets" : action == "import terrain definitions" ? "terrain" : "resources", "json", true,
			Glob2UI::tr(action == "import set" ? "[editor menu import set]" : action == "import terrain definitions" ? "[Import Terrain Definitions]" : "[Import Resource Definitions]"), nullptr, nullptr,
			nullptr, Glob2UI::Surface::Editor, false);
		importingTerrain = action == "import terrain definitions";
		importingSet = action == "import set";
        importingResources = action == "import resource definitions";
		// The list shows definitions already in the user's folder; "From device…"
		// opens the host picker for a file anywhere else.
		loadSaveScreen->setEmptyText(FormattableString(Toolkit::getStringTable()->getString("[No definition files found in %0]"))
										 .arg(importingSet ? "sets/" : importingTerrain ? "terrain/" : "resources/"));
		if (GAGCore::ApplicationHost::canImportFiles())
			loadSaveScreen->enableDeviceImport();
		attachDialog(*loadSaveScreen);
		showingLoad = true;
	}
	else if (action.starts_with("open terrain palette") || action.starts_with("open resource palette"))
	{
		// The palettes are sections of the dock: "open terrain palette <group>"
		// switches to its tab, expands the catalogue group and scrolls it into
		// view; the active brush stays selected. Without a dock (the phone
		// presentation) it shows the terrain tools.
		const bool resources = action.starts_with("open resource palette");
		const std::string prefix = resources ? "open resource palette" : "open terrain palette";
		const std::string group = action.size() > prefix.size() + 1 ? action.substr(prefix.size() + 1) : "";
		const auto section = resources ? BrushSection::Resources : BrushSection::Terrain;
		if (dock)
			revealBrushGroup(section, group.empty() || findBrushGroup(brushCatalog(), section, group) ? group : "");
		else if (panelMode != Terrain)
			performAction("switch to terrain view");
	}
	else if (action == "open load screen")
	{
		// Replacing the map asks about unsaved work first.
		if (hasMapBeenModified) openConfirm(ConfirmPurpose::LoadUnsaved);
		else openLoadDialog();
	}
	else if (action == "share map")
	{
		// The catalog validates a saved file, so unsaved or never-saved maps
		// are saved first and shared once that save completes.
		if (hasMapBeenModified || savedFilename.empty()) openConfirm(ConfirmPurpose::ShareSaveFirst);
		else pendingShareFilename = savedFilename;
	}
	else if (action == "request reroll terrain look")
	{
		openConfirm(ConfirmPurpose::RerollTerrain);
	}
	else if(action=="close load screen")
	{
		loadSaveScreen.reset();
		showingLoad=false;
		importingTerrain = false;
		importingResources = false;
        importingSet = false;
	}
	else if(action=="open save screen")
	{
		performAction("unselect");
		performAction("scroll horizontal stop");
		performAction("scroll vertical stop");
		loadSaveScreen=std::make_unique<LoadSaveDialog>("maps", "map", false, Toolkit::getStringTable()->getString("[save map]"), game.mapHeader.getMapName().c_str(), glob2FilenameToName, glob2NameToFilename, Glob2UI::Surface::Editor);
		// Saving again to the file this map came from needs no confirmation.
		if (!savedFilename.empty()) loadSaveScreen->allowOverwriteOf(glob2FilenameToName(savedFilename));
		attachDialog(*loadSaveScreen);
		showingSave=true;
	}
	else if(action=="close save screen")
	{
		loadSaveScreen.reset();
		showingSave=false;
	}
	else if(action=="open scenario editor")
	{
		performAction("unselect");
		performAction("scroll horizontal stop");
		performAction("scroll vertical stop");
		scenarioAtOpen=scenarioFingerprint();
		scriptEditor=std::make_unique<ScriptEditorScreen>(&game);
		attachDialog(*scriptEditor);
		showingScriptEditor=true;
	}
	else if(action=="close scenario editor")
	{
		// OK commits the editor's tabs; only a real difference is a map change.
		if (scriptEditor->finished() && scriptEditor->result()==ScriptEditorScreen::OK && scenarioFingerprint()!=scenarioAtOpen)
			hasMapBeenModified=true;
		scenarioAtOpen.clear();
		scriptEditor.reset();
		showingScriptEditor=false;
	}
	else if(action=="open teams editor")
	{
		performAction("unselect");
		performAction("scroll horizontal stop");
		performAction("scroll vertical stop");

		// The editor shows the live team colours; Cancel restores the header.
		baseTeamsAtOpen.clear();
		for (int i=0; i<game.mapHeader.getNumberOfTeams(); ++i)
		{
			baseTeamsAtOpen.push_back(game.mapHeader.getBaseTeam(i));
			game.mapHeader.getBaseTeam(i)=*game.teams[i];
		}

		teamsEditor=std::make_unique<TeamsEditor>(&game);
		teamSlotsAtOpen.clear();
		for (int i=0; i<Team::MAX_COUNT; ++i)
			teamSlotsAtOpen.push_back(teamsEditor->slot(i));
		attachDialog(*teamsEditor);
		showingTeamsEditor=true;
	}
	else if(action=="close teams editor")
	{
		const bool confirmed = teamsEditor->finished() && teamsEditor->result()==TeamsEditor::OK;
		if (confirmed && teamSlotsChanged())
			hasMapBeenModified=true;
		if (!confirmed)
			for (std::size_t i=0; i<baseTeamsAtOpen.size() && int(i)<game.mapHeader.getNumberOfTeams(); ++i)
				game.mapHeader.getBaseTeam(int(i))=baseTeamsAtOpen[i];
		baseTeamsAtOpen.clear();
		teamSlotsAtOpen.clear();
		teamsEditor.reset();
		showingTeamsEditor=false;
	}
	else if(action=="open area name")
	{
		performAction("unselect");
		performAction("scroll horizontal stop");
		performAction("scroll vertical stop");
		areaName=std::make_unique<AskForTextInput>("[Change Area Name]", game.map.getAreaName(areaNumber->getIndex()));
		attachDialog(*areaName);
		isShowingAreaName=true;
	}
	else if(action=="close area name")
	{
		// getText() is the original name after Cancel; only a new name is a change.
		const std::string name = areaName->getText();
		if (name != game.map.getAreaName(areaNumber->getIndex()))
		{
			game.map.setAreaName(areaNumber->getIndex(), name);
			hasMapBeenModified = true;
		}
		performAction("update script area number");
		areaName.reset();
		isShowingAreaName=false;
	}
	else if(action=="update script area number")
	{
		// Choosing which area to paint is not a map change.
		areaNameLabel->setLabel(game.map.getAreaName(areaNumber->getIndex()));
	}
	else if(action=="compute fertility")
	{
		//Only compute when its x'ed in, not otherwise
		if(isFertilityOn)
		{
			fertilityRequested = true;
		}
	}
	else if(action=="refresh fertility")
	{
		if(isFertilityOn)
			fertilityRequested = true;
	}
	else if(action=="quit editor")
	{
		doQuit=true;
	}
	else
		return false;
	return true;
}
