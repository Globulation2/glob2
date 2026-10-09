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
#include "TeamStat.h"
#include "TeamStatChart.h"
#include <FormatableString.h>
#include <Toolkit.h>
#include <StringTable.h>
#include <algorithm>
#include <cmath>
using namespace GAGCore;

bool GameGUITouch::lensVisible() const
{
	return lensOpen && usesHUD() && gui.selectionMode == GameGUI::NO_SELECTION &&
		   gui.displayMode == GameGUI::STAT_TEXT_VIEW && !globalContainer->isViewingGame() && !peekOpen &&
		   !statsOpen && !layout().persistentPanel;
}

std::vector<GameGUITouch::Lens> GameGUITouch::lenses() const
{
	auto tr = [](const char *key) { return std::string(Toolkit::getStringTable()->getString(key)); };
	const bool anyOverlay = gui.showStarvingMap || gui.showDamagedMap || gui.showDefenseMap || gui.showFertilityMap;
	// Views first (nearest the toolbar), then tools.
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

// Column-major from the toolbox corner: rows rise from the toolbar, and further
// columns step inward. `ui` is in drawable units.
std::vector<ViewRect> GameGUITouch::lensRects(const MobileLayout &ui) const
{
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	const double w = InGameTouchTheme::lensWidth * unit, h = InGameTouchTheme::lensHeight * unit,
				 gap = InGameTouchTheme::gap * unit, inset = InGameTouchTheme::railInset * unit;
	const double minimapBottom = hudLayout(ui).minimap.y + hudLayout(ui).minimap.h + 8 * unit;
	const double bottom = ui.actions.y - 8 * unit;
	const int rows = std::max(1, int((bottom - minimapBottom + gap) / (h + gap)));
	const bool left = ThumbSide::toolboxLeft();
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
							items[i].selected ? InGameTouchTheme::selected() : InGameTouchTheme::field());
		gfx->drawRect(int(r.x), int(r.y), int(r.w), int(r.h), InGameTouchTheme::border());
		drawPointLabel(r, items[i].label, .72);
	}
}

// Keep the legend opposite the toolboxes, on the selected thumb side.
ViewRect GameGUITouch::overlayLegendRect() const
{
	const auto ui = layout();
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	const double w = 176 * unit, h = 44 * unit, margin = 8 * unit;
	return {ThumbSide::toolboxLeft() ? ui.safe.x + ui.safe.w - margin - w : ui.safe.x + margin,
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
		gfx->drawFilledRect(int(r.x), int(r.y), int(r.w), int(r.h), InGameTouchTheme::readout());
		gfx->drawRect(int(r.x), int(r.y), int(r.w), int(r.h), InGameTouchTheme::border());
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

// Portrait puts the buttons in a row below the map; landscape in a column on
// the thumb side, zoom in lowest.
ViewRect GameGUITouch::peekRect() const
{
	const auto ui = layout();
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	const auto area = ui.world;
	const double column = InGameTouchTheme::peekButtonColumn * unit, gap = 8 * unit;
	if (area.w > area.h)
	{
		const double side = std::min({InGameTouchTheme::peekSide * unit, area.h - 24 * unit, area.w - column - 5 * gap});
		const double x = area.x + (area.w - side - gap - column) / 2;
		return {ThumbSide::left() ? x + column + gap : x, area.y + (area.h - side) / 2, side, side};
	}
	const double buttons = InGameTouchTheme::target * unit + gap;
	const double side = std::min({InGameTouchTheme::peekSide * unit, ui.safe.w - 32 * unit, area.h - buttons - 24 * unit});
	return {ui.safe.x + (ui.safe.w - side) / 2, area.y + (area.h - side - buttons) / 2, side, side};
}

std::vector<ViewRect> GameGUITouch::peekButtons() const
{
	const auto map = peekRect();
	const auto area = layout().world;
	const double unit = globalContainer->gfx->logicalUnitsPerPoint(), gap = 8 * unit;
	std::vector<ViewRect> buttons;
	if (area.w > area.h)
	{
		const double w = InGameTouchTheme::peekButtonColumn * unit, h = (map.h - 2 * gap) / 3;
		const double x = ThumbSide::left() ? map.x - gap - w : map.x + map.w + gap;
		for (int i = 0; i < 3; ++i) // Done, zoom out, zoom in (lowest).
			buttons.push_back({x, map.y + i * (h + gap), w, h});
		return buttons;
	}
	const double h = InGameTouchTheme::target * unit, w = (map.w - 2 * gap) / 3;
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
		peekMinimap->setMapSize(gui.drawnScene().map.getW(), gui.drawnScene().map.getH());
	}
	const auto ui = layout();
	const double unit = gfx->logicalUnitsPerPoint();
	gfx->setClipRect();
	gfx->drawFilledRect(int(ui.world.x), int(ui.world.y), int(ui.world.w), int(ui.world.h), Color(0, 0, 0, 120));
	const auto rect = peekRect();
	const auto buttons = peekButtons();
	{
		double x0 = rect.x, y0 = rect.y, x1 = rect.x + rect.w, y1 = rect.y + rect.h;
		for (const auto &b : buttons)
		{
			x0 = std::min(x0, b.x);
			x1 = std::max(x1, b.x + b.w);
			y1 = std::max(y1, b.y + b.h);
		}
		gfx->drawFilledRect(int(x0 - 6 * unit), int(y0 - 6 * unit), int(x1 - x0 + 12 * unit),
							int(y1 - y0 + 12 * unit), InGameTouchTheme::paper());
	}
	SDL_Rect clip{int(rect.x), int(rect.y), int(rect.w), int(rect.h)};
	gfx->setUITransform(rect.w / size, rect.x - (gfx->getW() - size) * rect.w / size, rect.y, &clip);
	peekMinimap->setMinimapMode(Minimap::ShowFOW);
	peekMinimap->draw(gui.view.drawnScene(), gui.localTeamNo, gui.viewportX, gui.viewportY, int(std::ceil(gui.camera.visibleW() / 32)),
					  int(std::ceil(gui.camera.visibleH() / 32)));
	gfx->setUITransform();
	gfx->setClipRect();
	gfx->drawRect(int(rect.x), int(rect.y), int(rect.w), int(rect.h), InGameTouchTheme::border());
	const std::string labels[] = {Toolkit::getStringTable()->getString("[Done]"), "−", "+"};
	for (int i = 0; i < 3; ++i)
	{
		const auto &b = buttons[i];
		gfx->drawFilledRect(int(b.x), int(b.y), int(b.w), int(b.h), InGameTouchTheme::field());
		gfx->drawRect(int(b.x), int(b.y), int(b.w), int(b.h), InGameTouchTheme::border());
		drawPointLabel(b, labels[i], i ? 1.2 : .9);
	}
}

// A full-width sheet above the toolbar: close on the far side, metric arrows
// under the thumb, then counters and the chart.
GameGUITouch::StatsLayout GameGUITouch::statsLayout() const
{
	const auto ui = layout();
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	const double target = InGameTouchTheme::target * unit, gap = 4 * unit;
	const bool portrait = ui.safe.w <= ui.safe.h;
	const double height = portrait ? std::min(ui.safe.h * .5, 340 * unit) : ui.world.h - 8 * unit;
	StatsLayout out;
	// Landscape sheets are tall, so they stop short of the minimap's column.
	const double width = portrait ? ui.safe.w : minimapRect().x - 8 * unit - ui.safe.x;
	out.sheet = {ui.safe.x, ui.actions.y - height, width, height};
	const bool left = ThumbSide::left();
	const double y = out.sheet.y + gap;
	out.close = {left ? out.sheet.x + out.sheet.w - gap - target : out.sheet.x + gap, y, target, target};
	out.next = {left ? out.sheet.x + gap : out.sheet.x + out.sheet.w - gap - target, y, target, target};
	out.previous = {left ? out.next.x + target + gap : out.next.x - gap - target, y, target, target};
	const double titleX = left ? out.previous.x + target + gap : out.close.x + target + gap;
	const double titleRight = left ? out.close.x - gap : out.previous.x - gap;
	out.title = {titleX, y, std::max(0.0, titleRight - titleX), target};
	out.counters = {out.sheet.x + 8 * unit, y + target + gap, out.sheet.w - 16 * unit, 40 * unit};
	const double chartTop = out.counters.y + out.counters.h + gap;
	out.chart = {out.sheet.x + 8 * unit, chartTop, out.sheet.w - 16 * unit,
				 std::max(0.0, out.sheet.y + out.sheet.h - chartTop - 8 * unit)};
	return out;
}

void GameGUITouch::drawStats()
{
    const auto& frame = gui.drawnScene();
    if (!frame.world.history) return;
	auto *gfx = globalContainer->gfx;
	const double unit = gfx->logicalUnitsPerPoint();
	const auto l = statsLayout();
	gfx->setClipRect();
	gfx->drawFilledRect(int(l.sheet.x), int(l.sheet.y), int(l.sheet.w), int(l.sheet.h), InGameTouchTheme::readout());
	gfx->drawHorzLine(int(l.sheet.x), int(l.sheet.y), int(l.sheet.w), InGameTouchTheme::border());
	const std::pair<ViewRect, std::string> buttons[] = {{l.close, "×"}, {l.previous, "‹"}, {l.next, "›"}};
	for (const auto &[r, text] : buttons)
	{
		gfx->drawFilledRect(int(r.x), int(r.y), int(r.w), int(r.h), InGameTouchTheme::field());
		drawPointLabel(r, text, 1.3);
	}
	if (statsCatalog.empty()) statsCatalog = Stats::catalogForBuildings(*gui.drawnScene().buildingTypes);
	const auto &metrics = statsCatalog;
	const Stats::Metric &metric = metrics[std::size_t(std::clamp(statsMetric, 0, int(metrics.size()) - 1))];
	auto tr = [](const char *key) { return std::string(Toolkit::getStringTable()->getString(key)); };
	drawPointLabel(l.title, tr(Stats::groupKey(metric.group)) + " · " + TeamStatChart::title(metric), 1.0);
	// The colony now, read from the same catalog as the chart below.
	const auto history = Stats::historyOf(gui.localTeamNo, *frame.world.history->teams.at(gui.localTeamNo));
	std::string counters;
	for (const char *id : {"population", "buildings", "hunger", "health", "wheat harvested"})
	{
		const Stats::Metric &shown = Stats::metricById(id);
		const auto reading = Stats::latestReading(shown, Stats::defaultView(shown), history);
		counters += (counters.empty() ? "" : "   ") + TeamStatChart::title(shown) + ": " + TeamStatChart::readingText(reading);
	}
	drawPointLabel(l.counters, counters, .7);
	if (l.chart.w <= 0 || l.chart.h <= 0)
		return;
	// The chart is laid out in points and scaled with the rest of the HUD.
	SDL_Rect clip{int(l.chart.x), int(l.chart.y), int(l.chart.w), int(l.chart.h)};
	gfx->setUITransform(unit, l.chart.x, l.chart.y, &clip);
	TeamStatChart::Options options;
	options.metric = &metric;
	options.view = Stats::defaultView(metric);
	const int own = gui.localTeamNo;
	options.teams.push_back({own, presentationColor(frame.world.teams->values.at(own).color), ""});
	TeamStatChart::paint(std::vector<Stats::TeamHistory>{history}, frame.tick, *gfx, 0, 0, int(l.chart.w / unit), int(l.chart.h / unit), options);
	gfx->setUITransform();
	gfx->setClipRect();
}
