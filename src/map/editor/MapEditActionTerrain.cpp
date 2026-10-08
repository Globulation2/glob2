// SPDX-License-Identifier: GPL-3.0-or-later

#include "Game.h"
#include "ExperimentalFeatures.h"
#include "TerrainExperiments.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "ScriptEditorScreen.h"
#include "Unit.h"
#include "Utilities.h"
#include "GenerationContext.h"
#include <SDL3/SDL.h>
#include <charconv>

void MapEdit::beginZonePlacement(BrushType type)
{
	performAction("unselect");
	brushType = type;
	selectionMode=PlaceZone;
	if (brush.getType() == BrushTool::MODE_NONE)
		brush.defaultSelection();
	brush.setAddRemoveEnabledState(true);
}

void MapEdit::beginTerrainPlacement(TerrainSelector::TerrainType type, TerrainPlacementMode mode)
{
    const bool isTerrain = TerrainSelector::isBaseTerrain(type);
    const bool isResourceSelector = TerrainSelector::isResource(type);
    // Reject stale/invalid selector IDs and incompatible modes before changing
    // the current selection. Normalize legacy aliases to retain corner alignment.
    if (mode == TerrainPlacementMode::BaseTerrain ? !isTerrain : !isResourceSelector) return;
    if (isTerrain) {
        const auto material = TerrainSelector::baseTerrain(type);
		if (!game.map.validTerrainType(material) ||
			!game.map.terrainPresentation(material).editorSelectable)
			return;
		if (const auto requirement=terrainExperimentKey(material);
            requirement && !experimentEnabled(*requirement)) return;
        type = TerrainSelector::selectorFor(material);
    }
    else
    {
        const auto resource = TerrainSelector::resourceType(type, game.map.resourceRegistry());
        if (!game.map.resourceRegistry().valid(resource)) return;
        if (!experimentEnabled(game.map.resourceRegistry().requiredExperiment(resource))) return;
        // Legacy selectors (Wheat..PruneTree) and registry selectors name the same
        // brush: keep one canonical value so every presentation highlights it.
        type = TerrainSelector::selectorForResource(resource);
    }
	performAction("unselect");
	terrainType=type;
	selectionMode=PlaceTerrain;
	// Every terrain and resource brush starts in Add and offers Del.
	if (brush.getType() == BrushTool::MODE_NONE)
		brush.defaultSelection();
	brush.setAddRemoveEnabledState(true);
}

void MapEdit::resetPlacementTracking()
{
	lastPlacementX=-1;
	lastPlacementY=-1;
	firstPlacement.reset();
}

bool MapEdit::performTerrainAction(const std::string& action, float relMouseX, float relMouseY)
{
    if (action.starts_with("select resource "))
    {
        const auto id = game.map.resourceRegistry().find(action.substr(16));
        if (id) beginTerrainPlacement(TerrainSelector::selectorForResource(*id), TerrainPlacementMode::Resource);
        return true;
    }
    if (action.starts_with("select terrain "))
    {
        // Registry keys address built-in and imported types alike.
        if (const auto type = game.map.terrainRegistry().find(action.substr(15)))
            beginTerrainPlacement(TerrainSelector::selectorFor(*type), TerrainPlacementMode::BaseTerrain);
        return true;
    }
    // Legacy resource aliases first: an imported terrain named "stone" or "wheat"
    // must not take over these actions.
    {
        static constexpr std::pair<const char*, TerrainSelector::TerrainType> legacyResources[] = {
            {"select wheat", TerrainSelector::Wheat}, {"select trees", TerrainSelector::Trees},
            {"select stone", TerrainSelector::Stone}, {"select algae", TerrainSelector::Algae},
            {"select papyrus", TerrainSelector::Papyrus},
            {"select cherry tree", TerrainSelector::CherryTree}, {"select cherry", TerrainSelector::CherryTree},
            {"select orange tree", TerrainSelector::OrangeTree}, {"select orange", TerrainSelector::OrangeTree},
            {"select prune tree", TerrainSelector::PruneTree}, {"select prune", TerrainSelector::PruneTree}};
        for (const auto& [name, selector] : legacyResources)
            if (action == name)
            {
                beginTerrainPlacement(selector, TerrainPlacementMode::Resource);
                return true;
            }
    }
    // "select <name>" for the built-in types only (grass, sand, water, road, ...);
    // imported types are addressed by "select terrain <key>".
    if (action.starts_with("select "))
    {
        for (unsigned id=0; id<TERRAIN_COUNT && id<game.map.terrainRegistry().size(); ++id)
        {
            const auto type = static_cast<::TerrainType>(id);
			const auto &presentation = game.map.terrainPresentation(type);
			if (presentation.editorSelectable && action == std::string("select ")+presentation.name)
            {
                beginTerrainPlacement(TerrainSelector::selectorFor(type), TerrainPlacementMode::BaseTerrain);
                return true;
            }
        }
    }
	if(action.substr(0, 29)=="set place building selection ")
	{
		performAction("unselect");
		std::string type=action.substr(29, action.size()-29);
		if (game.buildingsTypes.getFinishedTypeNum(type)<0) return false;
		selectionName=type;
		selectionMode=PlaceBuilding;
	}
	else if(action=="reroll terrain look")
	{
		// Presentation only: the next scene extraction carries the new seed and
		// every terrain page recomposes. Not undoable, like team edits.
		game.map.setTerrainSeed(GenerationContext::randomSeed());
		mapHasBeenModified();
	}
	else if(action=="place building")
	{
		int typeNum=buildingSelectionType(selectionName);
		if (!game.isBuildingTypeAvailable(typeNum)) return false;
		BuildingType *bt = game.buildingsTypes.get(typeNum);
		int tempX, tempY, x, y;
		game.map.cursorToBuildingPos(mapMouseX(mouseX), mapMouseY(mouseY), bt->width, bt->height, &tempX, &tempY, viewportX, viewportY);

		if (game.checkRoomForBuilding(tempX, tempY, bt, &x, &y, team, false))
		{
			if(bt->maxUnitWorking)
				game.addBuilding(x, y, typeNum, team, 1, 0);
			else
				game.addBuilding(x, y, typeNum, team, 0, 0);
			if (typeNum==game.buildingsTypes.getStartingBuildingTypeNum())
			{
				if (game.teams[team]->startPosSet<Team::START_POS_FROM_SWARM)
				{
					game.teams[team]->startPosX=tempX;
					game.teams[team]->startPosY=tempY;
					game.teams[team]->startPosSet=Team::START_POS_FROM_SWARM;
				}
			}
			else
			{
				if (game.teams[team]->startPosSet<Team::START_POS_FROM_BUILDING)
				{
					game.teams[team]->startPosX=tempX;
					game.teams[team]->startPosY=tempY;
					game.teams[team]->startPosSet=Team::START_POS_FROM_BUILDING;
				}
			}
			game.regenerateDiscoveryMap();
			hasMapBeenModified = true;
		}
	}
	else if(action=="next building level page")
	{
		const int next=(buildingLevel/3+1)*3;
		buildingLevel=next<buildingLevelCount ? next : 0;
	}
	else if(action.starts_with("switch to building level "))
	{
		const auto text=std::string_view(action).substr(25);
		int value=0;
		const auto result=std::from_chars(text.data(),text.data()+text.size(),value);
		if (result.ec==std::errc{} && result.ptr==text.data()+text.size() && value>=1 && value<=buildingLevelCount)
			buildingLevel=value-1;
	}
	else if(action=="select forbidden zone")
	{
		beginZonePlacement(ForbiddenBrush);
	}
	else if(action=="select clearing zone")
	{
		beginZonePlacement(ClearAreaBrush);
	}
	else if(action=="select guard zone")
	{
		beginZonePlacement(GuardAreaBrush);
	}
	else if(action=="select farm zone")
	{
		if(experimentEnabled(experimentDefinition(ExperimentId::FarmAreas).key))
			beginZonePlacement(FarmAreaBrush);
	}
	else if(action=="handle zone click")
	{
		if(brushType==NoBrush)
		{
			performAction("unselect");
			performAction("select forbidden zone");
		}
		brush.handleClick(relMouseX, relMouseY);
	}
	else if(action=="zone drag start")
	{
		isDraggingZone=true;
		handleBrushClick(mapMouseX(mouseX), mapMouseY(mouseY));
		hasMapBeenModified = true;
	}
	else if(action=="zone drag motion")
	{
		handleBrushClick(mapMouseX(mouseX), mapMouseY(mouseY));
		hasMapBeenModified = true;
	}
	else if(action=="zone drag end")
	{
		isDraggingZone=false;
		resetPlacementTracking();
	}

	else if(action=="select delete objects")
	{
		performAction("unselect");
		selectionMode=RemoveObject;
		deleteButton->setSelected();

		brush.defaultSelection();
		brush.setAddRemoveEnabledState(false);
	}
	else if(action=="select no ressources growth")
	{
		performAction("unselect");
		selectionMode=ChangeNoResourceGrowthAreas;
		noResourceGrowthButton->setSelected();
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
		brush.setAddRemoveEnabledState(true);
	}
	else if(action=="handle terrain click")
	{
		// Choosing a shape or mode never picks a terrain; the next brush keeps the shape.
		brush.handleClick(relMouseX, relMouseY);
	}
	else if(action=="terrain drag start")
	{
		isDraggingTerrain=true;
		strokeCoveredCells=strokePlacedResources=0;
		handleTerrainClick(mapMouseX(mouseX), mapMouseY(mouseY));
		hasMapBeenModified = true;
	}
	else if(action=="terrain drag motion")
	{
		handleTerrainClick(mapMouseX(mouseX), mapMouseY(mouseY));
		hasMapBeenModified = true;
	}
	else if(action=="terrain drag end")
	{
		isDraggingTerrain=false;
		finishTerrainStroke();
		resetPlacementTracking();
	}
	else if(action=="delete drag start")
	{
		isDraggingDelete=true;
		handleDeleteClick(mapMouseX(mouseX), mapMouseY(mouseY));
		hasMapBeenModified = true;
	}
	else if(action=="delete drag motion")
	{
		handleDeleteClick(mapMouseX(mouseX), mapMouseY(mouseY));
		hasMapBeenModified = true;
	}
	else if(action=="delete drag end")
	{
		isDraggingDelete=false;
		resetPlacementTracking();
	}
	else if(action=="select change areas")
	{
		performAction("unselect");
		selectionMode=ChangeAreas;
		areasButton->setSelected();
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
		brush.setAddRemoveEnabledState(true);
	}
	else if(action=="area drag start")
	{
		isDraggingArea=true;
		handleAreaClick(mapMouseX(mouseX), mapMouseY(mouseY));
		hasMapBeenModified = true;
	}
	else if(action=="area drag motion")
	{
		handleAreaClick(mapMouseX(mouseX), mapMouseY(mouseY));
		hasMapBeenModified = true;
	}
	else if(action=="area drag end")
	{
		isDraggingArea=false;
		resetPlacementTracking();
	}
	else if(action=="no ressource growth area drag start")
	{
		isDraggingNoResourceGrowthArea=true;
		handleNoResourceGrowthClick(mapMouseX(mouseX), mapMouseY(mouseY));
		hasMapBeenModified = true;
	}
	else if(action=="no ressource growth area drag motion")
	{
		handleNoResourceGrowthClick(mapMouseX(mouseX), mapMouseY(mouseY));
		hasMapBeenModified = true;
	}
	else if(action=="no ressource growth area drag end")
	{
		isDraggingNoResourceGrowthArea=false;
		resetPlacementTracking();
	}
	else if(action=="add team")
	{
		if(game.mapHeader.getNumberOfTeams() < Team::MAX_COUNT)
		{
			game.addTeam();
			regenerateGameHeader();
		}
		hasMapBeenModified = true;
	}
	else if(action=="remove team")
	{
		if(game.mapHeader.getNumberOfTeams() > 1)
		{
			if(team==game.mapHeader.getNumberOfTeams()-1)
				team-=1;
			game.removeTeam();
			regenerateGameHeader();
		}
		hasMapBeenModified = true;
	}
	else if(action=="select active team")
	{
		selectActiveTeam(relMouseX / TeamColorSelector::SWATCH_SIZE
		    + (relMouseY / TeamColorSelector::SWATCH_SIZE) * TeamColorSelector::COLUMNS);
	}
	else
		return false;
	return true;
}

// Shared semantic team selection; touch controls do not emulate sidebar pixels.
void MapEdit::selectActiveTeam(int selected) {
    if(selected<0 || selected>=Team::MAX_COUNT || !game.teams[selected]) return;
    team=selected;
    game.map.computeDisplayedForbidden(team);
    game.map.computeDisplayedClearArea(team);
    game.map.computeDisplayedGuardArea(team);
    game.map.computeDisplayedFarmArea(team);
}
