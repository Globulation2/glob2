// SPDX-License-Identifier: GPL-3.0-or-later
#include "GameGUITouch.h"
#include "InGameTouchTheme.h"
#include "ThumbSide.h"
#include "GameGUI.h"
#include "GameGUIInternal.h"
#include "GlobalContainer.h"
#include "BuildingType.h"
#include <Toolkit.h>
#include <StringTable.h>
#include <algorithm>
#include <cmath>
using namespace GAGCore;

bool GameGUITouch::showsBuildPalette() const
{
	// Spacious layouts keep the palette visible while a placement is active.
	// Phone visibility remains controlled by the panel's own bounds.
	return (gui.selectionMode == GameGUI::NO_SELECTION ||
			gui.selectionMode == GameGUI::TOOL_SELECTION) &&
		   (gui.displayMode == GameGUI::CONSTRUCTION_VIEW ||
			gui.displayMode == GameGUI::FLAG_VIEW) &&
		   !globalContainer->isViewingGame();
}
std::vector<GameGUITouch::PaletteItem> GameGUITouch::paletteItems() const
{
	const bool flags = gui.displayMode == GameGUI::FLAG_VIEW;
	const auto &names = flags ? gui.flagsChoiceName : gui.buildingsChoiceName;
	const auto &enabled = flags ? gui.flagsChoiceState : gui.buildingsChoiceState;
	std::vector<PaletteItem> items;
	for (size_t i = 0; i < names.size(); ++i)
		items.push_back({names[i], enabled[i]});
	if (flags)
		for (int i = 0; i < 3; ++i)
			items.push_back({"zone:" + std::to_string(i), true});
	return items;
}
bool GameGUITouch::paletteRail(const MobileLayout &ui) const
{
	return usesHUD() && !ui.persistentPanel;
}
int GameGUITouch::paletteColumns(const MobileLayout &ui) const
{
	const int count = std::max(1, int(paletteItems().size()));
	const bool flags = gui.displayMode == GameGUI::FLAG_VIEW;
	if (!paletteRail(ui))
		return flags ? count : 4;
	const bool landscape = ui.safe.w > ui.safe.h;
	if (flags)
		return landscape ? count : 1;
	return landscape ? InGameTouchTheme::railColumnsLandscape : InGameTouchTheme::railColumnsPortrait;
}
// The rail stacks upward from its bottom edge, so revealing hidden (higher)
// rows moves content down: drags, wheels and paging scroll the other way.
double GameGUITouch::paletteScrollSign() const
{
	return showsBuildPalette() && paletteRail(layout()) ? -1 : 1;
}
ViewRect GameGUITouch::paletteItemRect(size_t index) const
{
	const auto content = panelContent();
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	const auto ui = layout();
	const int columns = paletteColumns(ui);
	if (paletteRail(ui))
	{
		// Row-major from the thumb corner: the first item sits nearest the thumb.
		const double cell = InGameTouchTheme::paletteCell * unit,
					 stride = (InGameTouchTheme::paletteCell + InGameTouchTheme::gap) * unit,
					 gap = InGameTouchTheme::gap * unit;
		const int row = int(index) / columns, column = int(index) % columns;
		const double x = ThumbSide::left() ? content.x + gap + column * stride
										   : content.x + content.w - gap - cell - column * stride;
		const double y = content.y + content.h - gap - cell - row * stride + panelScroll * unit;
		return {x, y, cell, cell};
	}
	const bool flags = gui.displayMode == GameGUI::FLAG_VIEW;
	const double cell =
		flags ? std::min(InGameTouchTheme::paletteCell,
						 (content.w / unit - 8 - (columns - 1) * InGameTouchTheme::gap) / columns)
			  : InGameTouchTheme::paletteCell;
	const double stride = (cell + InGameTouchTheme::gap) * unit;
	return {content.x + 4 * unit + (index % columns) * stride,
			content.y + 4 * unit + (index / columns) * stride - panelScroll * unit, cell * unit,
			InGameTouchTheme::paletteCell * unit};
}
std::optional<GameGUITouch::PaletteItem> GameGUITouch::paletteItemAt(ViewPoint point) const
{
	if (!showsBuildPalette() || !layout().panel.contains(point))
		return {};
	const auto items = paletteItems();
	for (size_t i = 0; i < items.size(); ++i)
		if (paletteItemRect(i).contains(point))
			return items[i];
	return {};
}
void GameGUITouch::drawBuildPalette()
{
	auto *gfx = globalContainer->gfx;
	const double unit = gfx->logicalUnitsPerPoint();
	const auto content = panelContent();
	const auto items = paletteItems();
	SDL_Rect clip{int(content.x), int(content.y), int(content.w), int(content.h)};
	for (size_t i = 0; i < items.size(); ++i)
	{
		const auto rect = paletteItemRect(i);
		gfx->setClipRect(clip.x, clip.y, clip.w, clip.h);
		gfx->drawFilledRect(int(rect.x), int(rect.y), int(rect.w), int(rect.h),
							items[i].enabled ? InGameTouchTheme::field : InGameTouchTheme::paper);
		gfx->drawRect(int(rect.x), int(rect.y), int(rect.w), int(rect.h), InGameTouchTheme::border);
		if (items[i].name.starts_with("zone:"))
		{
			const std::string zones[] = {Toolkit::getStringTable()->getString("[Forbid]"),
										 Toolkit::getStringTable()->getString("[Guard]"),
										 Toolkit::getStringTable()->getString("[Clear]")};
			drawPointLabel(rect, zones[items[i].name.back() - '0'], .75);
			continue;
		}
		auto *type = globalContainer->buildingsTypes.getByType(items[i].name.c_str(), 0, false);
		if (type)
		{
			auto *sprite = type->miniSpriteImage >= 0 ? type->miniSpritePtr : type->gameSpritePtr;
			const int frame =
				type->miniSpriteImage >= 0 ? type->miniSpriteImage : type->gameSpriteImage;
			sprite->setBaseColor(gui.localTeam->color);
			const double factor = std::min({unit, (rect.w - 8 * unit) / sprite->getW(frame),
											(rect.h - 8 * unit) / sprite->getH(frame)});
			gfx->setUITransform(factor, rect.x + (rect.w - sprite->getW(frame) * factor) / 2,
								rect.y + (rect.h - sprite->getH(frame) * factor) / 2, &clip);
			gfx->drawSprite(0, 0, sprite, frame);
			gfx->setUITransform();
		}
	}
	gfx->setClipRect();
	if (placement && !placement->dragging)
	{
		const ViewRect help{content.x, std::max(layout().safe.y, content.y - 48 * unit), content.w,
							48 * unit};
		gfx->drawFilledRect(int(help.x), int(help.y), int(help.w), int(help.h),
							InGameTouchTheme::paper);
		drawPointLabel(help, Toolkit::getStringTable()->getString("[" + placement->building + "]"),
					   .9);
	}
}
void GameGUITouch::tapBuildPalette(ViewPoint point)
{
	const auto item = paletteItemAt(point);
	if (item && item->enabled)
	{
		if (item->name.starts_with("zone:"))
		{
			gui.setSelection(GameGUI::BRUSH_SELECTION);
			gui.brush.defaultSelection();
			gui.toolManager.activateZoneTool(
				static_cast<GameGUIToolManager::ZoneType>(item->name.back() - '0'));
			panelOpen = false;
			return;
		}
		gui.setSelection(GameGUI::TOOL_SELECTION, const_cast<char *>(item->name.c_str()));
		panelOpen = false;
	}
}
