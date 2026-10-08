// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <algorithm>
#include <cassert>
#include <optional>

#include <FormatableString.h>
#include <StringTable.h>
#include <Toolkit.h>

#include "Game.h"
#include "GameGUI.h"
#include "GameGUIInternal.h"
#include "GlobalContainer.h"
#include "IntBuildingType.h"
#include "SpriteCentering.h"
#include "BuildingPresentation.h"
#include "render/scene/BuildingCatalogView.h"
#include "Ressource.h"

namespace {

// Layout of the choice panel (file-private; the constants drive both the sprite grid
// and the mouse hit grid, which share one Y origin — the caller's `panelTopY`).
constexpr int CHOICE_ROW_HEIGHT_PX = 46;
// Width of the per-cell clip rect used when blitting the building icon. This is a
// sprite-tile width, not the cell width (which is RIGHT_MENU_WIDTH/numberPerLine).
constexpr int CHOICE_SPRITE_CLIP_W_PX = 64;

// Selection-highlight sprite IDs in the `gamegui` sheet, and per-orientation Y nudge.
constexpr int CHOICE_HIGHLIGHT_SPRITE_2COL = 8;
constexpr int CHOICE_HIGHLIGHT_SPRITE_3COL = 23;
constexpr int CHOICE_HIGHLIGHT_DECY_2COL = 1;
constexpr int CHOICE_HIGHLIGHT_DECY_3COL = 4;

// Right-panel clip rect: starts at this Y and runs to the bottom of the screen.
constexpr int CHOICE_PANEL_CLIP_TOP_Y = 128;

// The info block at the bottom of the right panel is anchored this many pixels above
// the bottom of the screen.
constexpr int CHOICE_INFO_BOTTOM_OFFSET_PX = 61; // Four resource rows, including fruit.

// Find the index of `name` in `types`, or nullopt if absent.
std::optional<size_t> findChoiceIndex(const std::vector<std::string>& types, const std::string& name)
{
	auto it = std::find(types.begin(), types.end(), name);
	if (it == types.end())
		return std::nullopt;
	return static_cast<size_t>(it - types.begin());
}

} // namespace

int GameGUI::choiceVisibleRows(int panelTopY,unsigned columns) const
{
    const int extraRows=drawnScene().materialVisible(10) || drawnScene().materialVisible(11) ? 2
        : drawnScene().materialVisible(8) || drawnScene().materialVisible(9) ? 1 : 0;
    return columns==3 ? 1 : std::max(1,(globalContainer->gfx->getH()-CHOICE_INFO_BOTTOM_OFFSET_PX-extraRows*11-32-panelTopY)/CHOICE_ROW_HEIGHT_PX);
}

bool GameGUI::scrollBuildingChoices(double delta)
{
    if (mouseX<globalContainer->gfx->getW()-RIGHT_MENU_WIDTH) return false;
    if (selectionMode==BUILDING_SELECTION)
    {
        const bool overPreview=drawnScene().panels.building.valid && hoveredBuildingPreview(drawnScene().panels.building)!=BuildingPreview::None;
        if (!overPreview && (mouseY<YPOS_BASE_BUILDING || mouseY>=globalContainer->gfx->getH()-BOTTOM_BUTTON_PRIMARY_YOFFSET-4)) return false;
        buildingInfoScroll=std::clamp(buildingInfoScroll+(delta>0 ? -32 : delta<0 ? 32 : 0),0,buildingInfoScrollMaximum);
        return true;
    }
    const bool flags=displayMode==FLAG_VIEW;
    if (!flags && displayMode!=CONSTRUCTION_VIEW) return false;
    const int top=flags ? YPOS_BASE_FLAG : YPOS_BASE_CONSTRUCTION;
    const unsigned columns=flags ? 3 : 2;
    const int rows=choiceVisibleRows(top,columns);
    if (mouseY<top || mouseY>=top+rows*CHOICE_ROW_HEIGHT_PX) return false;
    const int count=static_cast<int>((flags ? flagsChoiceName : buildingsChoiceName).size());
    int& offset=flags ? flagChoiceRow : buildingChoiceRow;
    offset=std::clamp(offset+(delta>0 ? -1 : delta<0 ? 1 : 0),0,std::max(0,(count+int(columns)-1)/int(columns)-rows));
    return true;
}

void GameGUI::drawChoiceSprites(int panelTopY, const std::vector<std::string>& types, const std::vector<bool>& states, unsigned numberPerLine)
{
	const int width = RIGHT_MENU_WIDTH / static_cast<int>(numberPerLine);
	const int panelLeftX = globalContainer->gfx->getW() - RIGHT_MENU_WIDTH;

	const int offset=numberPerLine==3 ? flagChoiceRow : buildingChoiceRow;
	const int rows=choiceVisibleRows(panelTopY,numberPerLine);
	for (size_t i = size_t(offset)*numberPerLine; i < std::min(types.size(),size_t(offset+rows)*numberPerLine); i++)
	{
		if (!states[i])
			continue;

		const std::string& type = types[i];
		const BuildingType *bt = BuildingCatalogView(*drawnScene().buildingTypes).getByType(type.c_str(), 0, false);
		assert(bt);
		int imgid = bt->miniSpriteImage;

		const int x = (static_cast<int>(i % numberPerLine) * width) + panelLeftX;
		const int y = ((static_cast<int>(i / numberPerLine)-offset) * CHOICE_ROW_HEIGHT_PX) + panelTopY;
		globalContainer->gfx->setClipRect(x, y, CHOICE_SPRITE_CLIP_W_PX, CHOICE_ROW_HEIGHT_PX);

		Sprite *buildingSprite;
		if (imgid >= 0)
		{
			buildingSprite = bt->miniSpritePtr;
		}
		else
		{
			buildingSprite = bt->gameSpritePtr;
			imgid = bt->gameSpriteImage;
		}

		const SpriteCenterOffset off = centerSprite(width, CHOICE_ROW_HEIGHT_PX, buildingSprite, imgid);

		buildingSprite->setBaseColor(presentationColor(drawnScene().panels.local.state().color));
		globalContainer->gfx->drawSprite(x + off.dx, y + off.dy, buildingSprite, imgid);
		globalContainer->gfx->finishDrawingSprite(buildingSprite, 255);

		globalContainer->gfx->setClipRect();
		if (bt->shortTypeNum>=0 && highlights.find(HighlightBuildingOnPanel + bt->shortTypeNum) != highlights.end())
		{
			arrowPositions.push_back(HighlightArrowPosition(x + off.dx - 36, y - 6 + off.dy, 38));
		}
	}
}

void GameGUI::drawChoiceHighlight(int panelTopY, size_t selIdx, unsigned numberPerLine)
{
	const int width = RIGHT_MENU_WIDTH / static_cast<int>(numberPerLine);
	const int panelLeftX = globalContainer->gfx->getW() - RIGHT_MENU_WIDTH;

	const int spriteId = (numberPerLine == 2) ? CHOICE_HIGHLIGHT_SPRITE_2COL : CHOICE_HIGHLIGHT_SPRITE_3COL;
	const int decYNudge = (numberPerLine == 2) ? CHOICE_HIGHLIGHT_DECY_2COL : CHOICE_HIGHLIGHT_DECY_3COL;
	const int sw = globalContainer->gamegui->getW(spriteId);

	const int x = (static_cast<int>(selIdx % numberPerLine) * width) + panelLeftX;
	const int row=static_cast<int>(selIdx/numberPerLine)-(numberPerLine==3 ? flagChoiceRow : buildingChoiceRow);
	if (row<0 || row>=choiceVisibleRows(panelTopY,numberPerLine)) return;
	const int y=row*CHOICE_ROW_HEIGHT_PX+panelTopY;
	const int decX = (width - sw) / 2;

	globalContainer->gfx->drawSprite(x + decX, y + decYNudge, globalContainer->gamegui, spriteId);
}

std::optional<size_t> GameGUI::pickChoiceUnderMouse(int panelTopY, size_t count, unsigned numberPerLine) const
{
	const int width = RIGHT_MENU_WIDTH / static_cast<int>(numberPerLine);
	const int panelLeftX = globalContainer->gfx->getW() - RIGHT_MENU_WIDTH;

	if (mouseX <= panelLeftX)
		return std::nullopt;
	if (mouseY <= panelTopY)
		return std::nullopt;

	const int xNum = (mouseX - panelLeftX) / width;
	const int yNum = (mouseY - panelTopY) / CHOICE_ROW_HEIGHT_PX;
	if (xNum<0 || xNum>=int(numberPerLine) || yNum>=choiceVisibleRows(panelTopY,numberPerLine)) return std::nullopt;
	const int offset=numberPerLine==3 ? flagChoiceRow : buildingChoiceRow;
	const size_t id = static_cast<size_t>(yNum+offset) * numberPerLine + static_cast<size_t>(xNum);
	if (id >= count)
		return std::nullopt;
	return id;
}

void GameGUI::drawChoiceInfoPanel(const std::string& type)
{
	const int panelLeftX = globalContainer->gfx->getW() - RIGHT_MENU_WIDTH;
    const BuildingType *bt = BuildingCatalogView(*drawnScene().buildingTypes).getByType(type, 0, true);
    int extraRows=0;
    if (bt) for (unsigned material=8;material<MaterialCount;++material)
        if (bt->semantics.constructionCost[material] && drawnScene().materialVisible(material))
            extraRows=std::max(extraRows,int(material/2)-3);
	const int buildingInfoStart = globalContainer->gfx->getH() - CHOICE_INFO_BOTTOM_OFFSET_PX-extraRows*11;

	const int selected=BuildingCatalogView(*drawnScene().buildingTypes).getPlaceableTypeNum(type);
	if (selected<0) return;
	const auto* definition=BuildingCatalogView(*drawnScene().buildingTypes).get(selected);
	std::string key;
	const auto name=buildingDisplayName(*definition);
	globalContainer->gfx->drawString(panelLeftX+(RIGHT_MENU_WIDTH-globalContainer->littleFont->getStringWidth(name))/2,buildingInfoStart-32,globalContainer->littleFont,name);

	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 128, 128, 128));
	key = "[" + definition->type + " explanation]";
	if (Toolkit::getStringTable()->doesStringExist(key)) drawTextCenter(panelLeftX, buildingInfoStart - 20, key.c_str());
	key = "[" + definition->type + " explanation 2]";
	if (Toolkit::getStringTable()->doesStringExist(key)) drawTextCenter(panelLeftX, buildingInfoStart - 8, key.c_str());
	globalContainer->littleFont->popStyle();

	if (!bt)
		return;

	const int colLeftX = panelLeftX + 4 + (RIGHT_MENU_WIDTH - 128) / 2;
	// Preserve the familiar resource positions while allowing every construction
	// input. Storage capacity is independent of the construction recipe.
	constexpr unsigned materials[] = {0,4,3,1,2,5,6,7,8,9,10,11};
	for (size_t i=0; i<std::size(materials); ++i)
	{
		const int resource=materials[i];
		const int cost=bt->semantics.constructionCost[resource];
		if (resource>=HAPPINESS_BASE && cost==0) continue;
		if (!drawnScene().materialVisible(resource)) continue;
		globalContainer->gfx->drawString(colLeftX+int(i%2)*64, buildingInfoStart+6+int(i/2)*11,
			globalContainer->littleFont,
			FormattableString("%0: %1").arg(getMaterialName(resource)).arg(cost).c_str());
	}
}

void GameGUI::drawChoice(int panelTopY, std::vector<std::string> &types, std::vector<bool> &states, unsigned numberPerLine)
{
	assert(numberPerLine >= 2);
	assert(numberPerLine <= 3);

	// 1. Paint icon grid (and queue tutorial-highlight arrows).
	drawChoiceSprites(panelTopY, types, states, numberPerLine);

	// 2. Paint the selection highlight over the active tool's icon, if any.
	globalContainer->gfx->setClipRect(
		globalContainer->gfx->getW() - RIGHT_MENU_WIDTH,
		CHOICE_PANEL_CLIP_TOP_Y,
		RIGHT_MENU_WIDTH,
		globalContainer->gfx->getH() - CHOICE_PANEL_CLIP_TOP_Y);

	// Highlight the active tool's cell, but only when that tool belongs to THIS panel.
	// selectionMode and displayMode are independent axes: the user can tab-cycle the
	// display panel (CONSTRUCTION_VIEW <-> FLAG_VIEW) while a tool stays selected, which
	// leaves e.g. a building tool active over the flag panel. In that desynced state the
	// tool has no cell in `types`, so there is simply nothing to highlight here — a
	// legitimate UI state, not an error. (findChoiceIndex returns nullopt for it.)
	if (selectionMode == TOOL_SELECTION)
	{
		if (const auto selIdx = findChoiceIndex(types, toolManager.getBuildingName()))
			drawChoiceHighlight(panelTopY, *selIdx, numberPerLine);
	}

	// 3. Resolve which icon to show info for: prefer mouse-hover, fall back to the
	//    currently-selected tool when the mouse is elsewhere.
	std::optional<size_t> infoIdx = pickChoiceUnderMouse(panelTopY, types.size(), numberPerLine);
	if (!infoIdx && !toolManager.getBuildingName().empty())
		infoIdx = findChoiceIndex(types, toolManager.getBuildingName());

	// 4. Paint the info text block, but only when the chosen cell is active.
	if (infoIdx && states[*infoIdx])
		drawChoiceInfoPanel(types[*infoIdx]);
}
