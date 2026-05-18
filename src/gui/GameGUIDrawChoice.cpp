// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <algorithm>
#include <cassert>
#include <optional>

#include <FormatableString.h>
#include <GraphicContext.h>
#include <StringTable.h>
#include <Toolkit.h>

#include "Game.h"
#include "GameGUI.h"
#include "GameGUIInternal.h"
#include "GlobalContainer.h"
#include "IntBuildingType.h"

namespace {

// Layout of the choice panel (file-private; the constants drive both the sprite grid
// and the mouse hit grid, except for the Y origin — see BH-290).
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
constexpr int CHOICE_INFO_BOTTOM_OFFSET_PX = 50;

// Find the index of `name` in `types`, or nullopt if absent.
std::optional<size_t> findChoiceIndex(const std::vector<std::string>& types, const std::string& name)
{
	auto it = std::find(types.begin(), types.end(), name);
	if (it == types.end())
		return std::nullopt;
	return static_cast<size_t>(it - types.begin());
}

} // namespace

void GameGUI::drawChoiceSprites(const std::vector<std::string>& types, const std::vector<bool>& states, unsigned numberPerLine)
{
	const int width = RIGHT_MENU_WIDTH / static_cast<int>(numberPerLine);
	const int panelLeftX = globalContainer->gfx->getW() - RIGHT_MENU_WIDTH;

	for (size_t i = 0; i < types.size(); i++)
	{
		if (!states[i])
			continue;

		const std::string& type = types[i];
		BuildingType *bt = globalContainer->buildingsTypes.getByType(type.c_str(), 0, false);
		assert(bt);
		int imgid = bt->miniSpriteImage;

		const int x = (static_cast<int>(i % numberPerLine) * width) + panelLeftX;
		const int y = (static_cast<int>(i / numberPerLine) * CHOICE_ROW_HEIGHT_PX) + YPOS_BASE_BUILDING;
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

		const int decX = (width - buildingSprite->getW(imgid)) >> 1;
		const int decY = (CHOICE_ROW_HEIGHT_PX - buildingSprite->getW(imgid)) >> 1;

		buildingSprite->setBaseColor(localTeam->color);
		globalContainer->gfx->drawSprite(x + decX, y + decY, buildingSprite, imgid);
		globalContainer->gfx->finishDrawingSprite(buildingSprite, 255);

		globalContainer->gfx->setClipRect();
		if (hilights.find(HilightBuildingOnPanel + IntBuildingType::shortNumberFromType(type)) != hilights.end())
		{
			// Note: `y-6+decX` is preserved verbatim — see BH-291 for the X-into-Y typo.
			arrowPositions.push_back(HilightArrowPosition(x + decX - 36, y - 6 + decX, 38));
		}
	}
}

void GameGUI::drawChoiceHighlight(size_t selIdx, unsigned numberPerLine)
{
	const int width = RIGHT_MENU_WIDTH / static_cast<int>(numberPerLine);
	const int panelLeftX = globalContainer->gfx->getW() - RIGHT_MENU_WIDTH;

	const int spriteId = (numberPerLine == 2) ? CHOICE_HIGHLIGHT_SPRITE_2COL : CHOICE_HIGHLIGHT_SPRITE_3COL;
	const int decYNudge = (numberPerLine == 2) ? CHOICE_HIGHLIGHT_DECY_2COL : CHOICE_HIGHLIGHT_DECY_3COL;
	const int sw = globalContainer->gamegui->getW(spriteId);

	const int x = (static_cast<int>(selIdx % numberPerLine) * width) + panelLeftX;
	const int y = (static_cast<int>(selIdx / numberPerLine) * CHOICE_ROW_HEIGHT_PX) + YPOS_BASE_BUILDING;
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
	const size_t id = static_cast<size_t>(yNum) * numberPerLine + static_cast<size_t>(xNum);
	if (id >= count)
		return std::nullopt;
	return id;
}

void GameGUI::drawChoiceInfoPanel(const std::string& type)
{
	const int panelLeftX = globalContainer->gfx->getW() - RIGHT_MENU_WIDTH;
	const int buildingInfoStart = globalContainer->gfx->getH() - CHOICE_INFO_BOTTOM_OFFSET_PX;

	std::string key = "[" + type + "]";
	drawTextCenter(panelLeftX, buildingInfoStart - 32, key.c_str());

	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 128, 128, 128));
	key = "[" + type + " explanation]";
	drawTextCenter(panelLeftX, buildingInfoStart - 20, key.c_str());
	key = "[" + type + " explanation 2]";
	drawTextCenter(panelLeftX, buildingInfoStart - 8, key.c_str());
	globalContainer->littleFont->popStyle();

	BuildingType *bt = globalContainer->buildingsTypes.getByType(type, 0, true);
	if (!bt)
		return;

	const int colLeftX = panelLeftX + 4 + (RIGHT_MENU_WIDTH - 128) / 2;
	const int colRightX = colLeftX + 64;

	// maxRessource[] indexes are the engine-wide resource ordering: 0=Wood, 1=Corn,
	// 2=Papyrus, 3=Stone, 4=Alga. Don't reorder without auditing every consumer.
	globalContainer->gfx->drawString(colLeftX, buildingInfoStart + 6, globalContainer->littleFont,
		FormatableString("%0: %1").arg(Toolkit::getStringTable()->getString("[Wood]")).arg(bt->maxRessource[0]).c_str());
	globalContainer->gfx->drawString(colLeftX, buildingInfoStart + 17, globalContainer->littleFont,
		FormatableString("%0: %1").arg(Toolkit::getStringTable()->getString("[Stone]")).arg(bt->maxRessource[3]).c_str());

	globalContainer->gfx->drawString(colRightX, buildingInfoStart + 6, globalContainer->littleFont,
		FormatableString("%0: %1").arg(Toolkit::getStringTable()->getString("[Alga]")).arg(bt->maxRessource[4]).c_str());
	globalContainer->gfx->drawString(colRightX, buildingInfoStart + 17, globalContainer->littleFont,
		FormatableString("%0: %1").arg(Toolkit::getStringTable()->getString("[Corn]")).arg(bt->maxRessource[1]).c_str());

	globalContainer->gfx->drawString(colLeftX, buildingInfoStart + 28, globalContainer->littleFont,
		FormatableString("%0: %1").arg(Toolkit::getStringTable()->getString("[Papyrus]")).arg(bt->maxRessource[2]).c_str());
}

void GameGUI::drawChoice(int panelTopY, std::vector<std::string> &types, std::vector<bool> &states, unsigned numberPerLine)
{
	assert(numberPerLine >= 2);
	assert(numberPerLine <= 3);

	// 1. Paint icon grid (and queue tutorial-hilight arrows).
	drawChoiceSprites(types, states, numberPerLine);

	// 2. Paint the selection highlight over the active tool's icon, if any.
	globalContainer->gfx->setClipRect(
		globalContainer->gfx->getW() - RIGHT_MENU_WIDTH,
		CHOICE_PANEL_CLIP_TOP_Y,
		RIGHT_MENU_WIDTH,
		globalContainer->gfx->getH() - CHOICE_PANEL_CLIP_TOP_Y);

	if (selectionMode == TOOL_SELECTION)
	{
		const auto selIdx = findChoiceIndex(types, toolManager.getBuildingName());
		assert(selIdx);
		drawChoiceHighlight(*selIdx, numberPerLine);
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
