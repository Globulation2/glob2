// SPDX-License-Identifier: GPL-3.0-or-later
// Compact tactical tools: the lens strip, the overlay legend and the map peek.
// Every lens runs the same menuAction as the Spacious tactical panel.
#include "GameGUITouch.h"
#include "InGameTouchTheme.h"
#include "ThumbSide.h"
#include "GameGUI.h"
#include "GameGUIInternal.h"
#include "GlobalContainer.h"
#include "OverlayAreas.h"
#include "render/Minimap.h"
#include <Toolkit.h>
#include <StringTable.h>
#include <algorithm>
#include <cmath>
using namespace GAGCore;

bool GameGUITouch::lensVisible() const
{
	return lensOpen && usesHUD() && gui.selectionMode == GameGUI::NO_SELECTION &&
		   gui.displayMode == GameGUI::STAT_TEXT_VIEW && !globalContainer->isViewingGame() && !peekOpen &&
		   !layout().persistentPanel;
}

std::vector<GameGUITouch::Lens> GameGUITouch::lenses() const
{
	auto tr = [](const char *key) { return std::string(Toolkit::getStringTable()->getString(key)); };
	const bool anyOverlay = gui.showStarvingMap || gui.showDamagedMap || gui.showDefenseMap || gui.showFertilityMap;
	// Views first (nearest the thumb), then tools.
	return {{tr("[No overlay]"), -10, !anyOverlay},
			{tr("[Starvation overlay]"), 20, gui.showStarvingMap},
			{tr("[Damage overlay]"), 21, gui.showDamagedMap},
			{tr("[Defense overlay]"), 22, gui.showDefenseMap},
			{tr("[Fertility overlay]"), 23, gui.showFertilityMap},
			{tr("[Health and food bars]"), 6, gui.drawHealthFoodBar},
			{tr("[Statistics]"), 3, false},
			{tr("[Minimap]"), 50, false},
			{tr("[Message history]"), 4, false},
			{tr("[Mark map for allies]"), 5, gui.putMark},
			{tr("[Chat]"), 1, false}};
}

// Column-major from the thumb corner: rows rise from the toolbar, and further
// columns step away from the thumb. `ui` is in drawable units.
std::vector<ViewRect> GameGUITouch::lensRects(const MobileLayout &ui) const
{
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	const double w = InGameTouchTheme::lensWidth * unit, h = InGameTouchTheme::lensHeight * unit,
				 gap = InGameTouchTheme::gap * unit, inset = InGameTouchTheme::railInset * unit;
	const double minimapBottom = ui.safe.y + ((ui.safe.h / unit < 400 ? 72 : 96) + 12) * unit;
	const double bottom = ui.actions.y - 8 * unit;
	const int rows = std::max(1, int((bottom - minimapBottom + gap) / (h + gap)));
	const bool left = ThumbSide::left();
	std::vector<ViewRect> rects;
	const size_t count = lenses().size();
	for (size_t i = 0; i < count; ++i)
	{
		const int row = int(i) % rows, column = int(i) / rows;
		const double x = left ? ui.safe.x + inset + column * (w + gap)
							  : ui.safe.x + ui.safe.w - inset - w - column * (w + gap);
		rects.push_back({x, bottom - (row + 1) * h - row * gap, w, h});
	}
	return rects;
}

void GameGUITouch::drawLenses()
{
	auto *gfx = globalContainer->gfx;
	const auto items = lenses();
	const auto rects = lensRects(layout());
	for (size_t i = 0; i < items.size() && i < rects.size(); ++i)
	{
		const auto &r = rects[i];
		gfx->drawFilledRect(int(r.x), int(r.y), int(r.w), int(r.h),
							items[i].selected ? InGameTouchTheme::selected : InGameTouchTheme::field);
		gfx->drawRect(int(r.x), int(r.y), int(r.w), int(r.h), InGameTouchTheme::border);
		drawPointLabel(r, items[i].label, .72);
	}
}

// On the far side of the toolbar corner, away from the thumb and the lenses.
ViewRect GameGUITouch::overlayLegendRect() const
{
	const auto ui = layout();
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	const double w = 176 * unit, h = 44 * unit, margin = 8 * unit;
	return {ThumbSide::left() ? ui.safe.x + ui.safe.w - margin - w : ui.safe.x + margin,
			ui.actions.y - margin - h, w, h};
}

void GameGUITouch::drawOverlayLegend()
{
	// While the lens strip is open its selected lens names the overlay.
	if (!usesHUD() || layout().persistentPanel || lensVisible() || peekOpen)
		return;
	const bool flags[] = {gui.showStarvingMap, gui.showDamagedMap, gui.showDefenseMap, gui.showFertilityMap};
	const OverlayArea::OverlayType types[] = {OverlayArea::Starving, OverlayArea::Damage, OverlayArea::Defence,
											  OverlayArea::Fertility};
	const char *names[] = {"[Starvation overlay]", "[Damage overlay]", "[Defense overlay]", "[Fertility overlay]"};
	for (int i = 0; i < 4; ++i)
	{
		if (!flags[i])
			continue;
		auto *gfx = globalContainer->gfx;
		const double unit = gfx->logicalUnitsPerPoint();
		const auto r = overlayLegendRect();
		gfx->drawFilledRect(int(r.x), int(r.y), int(r.w), int(r.h), InGameTouchTheme::readout);
		gfx->drawRect(int(r.x), int(r.y), int(r.w), int(r.h), InGameTouchTheme::border);
		drawPointLabel({r.x, r.y, r.w, 24 * unit}, Toolkit::getStringTable()->getString(names[i]), .7);
		// The ramp the overlay uses: more intense means more affected.
		const auto base = OverlayArea::colorOf(types[i]);
		const int steps = 8;
		const double stepW = (r.w - 16 * unit) / steps;
		for (int k = 0; k < steps; ++k)
			gfx->drawFilledRect(int(r.x + 8 * unit + k * stepW), int(r.y + 26 * unit), int(std::ceil(stepW)),
								int(10 * unit), Color(base.r, base.g, base.b, Uint8(200 * (k + 1) / steps)));
		return;
	}
}

ViewRect GameGUITouch::peekRect() const
{
	const auto ui = layout();
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	const double buttons = (InGameTouchTheme::target + 8) * unit;
	const double side = std::min({InGameTouchTheme::peekSide * unit, ui.safe.w - 32 * unit,
								  ui.world.h - buttons - 24 * unit});
	return {ui.safe.x + (ui.safe.w - side) / 2, ui.world.y + (ui.world.h - side - buttons) / 2, side, side};
}

std::vector<ViewRect> GameGUITouch::peekButtons() const
{
	const auto map = peekRect();
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	const double h = InGameTouchTheme::target * unit, gap = 8 * unit, w = (map.w - 2 * gap) / 3;
	std::vector<ViewRect> buttons;
	for (int i = 0; i < 3; ++i)
	{
		// Done, zoom out, zoom in; zoom in sits on the thumb side.
		const int slot = ThumbSide::left() ? 2 - i : i;
		buttons.push_back({map.x + slot * (w + gap), map.y + map.h + gap, w, h});
	}
	return buttons;
}

void GameGUITouch::navigateMinimapIn(Minimap &minimap, ViewRect rect, int size, ViewPoint point)
{
	int x, y;
	minimap.convertToMap(globalContainer->gfx->getW() - size + int((point.x - rect.x) * size / rect.w),
						 int((point.y - rect.y) * size / rect.h), x, y);
	gui.updateCamera();
	const int oldX = gui.viewportX, oldY = gui.viewportY;
	gui.camera.originX = x * 32 - gui.camera.visibleW() / 2;
	gui.camera.originY = y * 32 - gui.camera.visibleH() / 2;
	gui.camera.normalize();
	gui.viewportX = gui.camera.tileX();
	gui.viewportY = gui.camera.tileY();
	gui.viewportChanged(oldX, gui.viewportX, oldY, gui.viewportY);
}

void GameGUITouch::navigatePeek(ViewPoint point)
{
	if (!peekMinimap)
		return;
	const auto rect = peekRect();
	navigateMinimapIn(*peekMinimap, rect, InGameTouchTheme::peekMinimapSize, rect.clamp(point));
}

void GameGUITouch::drawPeek()
{
	auto *gfx = globalContainer->gfx;
	const int size = InGameTouchTheme::peekMinimapSize;
	if (!peekMinimap)
	{
		peekMinimap = std::make_unique<Minimap>(globalContainer->runNoX, size, size, 0, 0, size, size,
												Minimap::ShowFOW);
		peekMinimap->setGame(gui.game);
	}
	const auto ui = layout();
	const double unit = gfx->logicalUnitsPerPoint();
	gfx->setClipRect();
	gfx->drawFilledRect(int(ui.world.x), int(ui.world.y), int(ui.world.w), int(ui.world.h), Color(0, 0, 0, 120));
	const auto rect = peekRect();
	const auto buttons = peekButtons();
	gfx->drawFilledRect(int(rect.x - 6 * unit), int(rect.y - 6 * unit), int(rect.w + 12 * unit),
						int(buttons[0].y + buttons[0].h - rect.y + 12 * unit), InGameTouchTheme::paper);
	SDL_Rect clip{int(rect.x), int(rect.y), int(rect.w), int(rect.h)};
	gfx->setUITransform(rect.w / size, rect.x - (gfx->getW() - size) * rect.w / size, rect.y, &clip);
	peekMinimap->setMinimapMode(Minimap::ShowFOW);
	peekMinimap->draw(gui.localTeamNo, gui.viewportX, gui.viewportY, int(std::ceil(gui.camera.visibleW() / 32)),
					  int(std::ceil(gui.camera.visibleH() / 32)));
	gfx->setUITransform();
	gfx->setClipRect();
	gfx->drawRect(int(rect.x), int(rect.y), int(rect.w), int(rect.h), InGameTouchTheme::border);
	const std::string labels[] = {Toolkit::getStringTable()->getString("[Done]"), "−", "+"};
	for (int i = 0; i < 3; ++i)
	{
		const auto &b = buttons[i];
		gfx->drawFilledRect(int(b.x), int(b.y), int(b.w), int(b.h), InGameTouchTheme::field);
		gfx->drawRect(int(b.x), int(b.y), int(b.w), int(b.h), InGameTouchTheme::border);
		drawPointLabel(b, labels[i], i ? 1.2 : .9);
	}
}
