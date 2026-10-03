// SPDX-License-Identifier: GPL-3.0-or-later
// The compact inspector's thumb dial. One region list drives drawing, hit
// testing, keyboard focus and the harness; every change goes through the same
// requests and orders as the Spacious row list.
#include <FormatableString.h>
#include "GameGUITouch.h"
#include "InGameTouchTheme.h"
#include "ThumbSide.h"
#include "TouchReadout.h"
#include "GameGUI.h"
#include "GameGUIInternal.h"
#include "GlobalContainer.h"
#include "BuildingType.h"
#include "UnitDisplayNames.h"
#include <Toolkit.h>
#include <StringTable.h>
#include <algorithm>
#include <cmath>
using namespace GAGCore;

bool GameGUITouch::usesDial() const
{
	return usesHUD() && !layout().persistentPanel;
}

GameGUITouch::DialLayout GameGUITouch::dialLayout(const MobileLayout &ui) const
{
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	const bool left = ThumbSide::left();
	const ViewRect safe{ui.safe.x / unit, ui.safe.y / unit, ui.safe.w / unit, ui.safe.h / unit};
	const double bottom = ui.actions.y / unit, margin = 8, header = InGameTouchTheme::inspectorHeader,
				 chip = InGameTouchTheme::dialChipWidth;
	const auto hud = hudLayout(ui);
	// Use the same geometry as drawing and minimap input: compact inspection
	// can enlarge the minimap to leave room for two stat rows and the title.
	const double minimapBottom = (hud.minimap.y + hud.minimap.h) / unit + 4;
	DialLayout out;
	out.portrait = safe.w <= safe.h;
	// Portrait keeps a full-height chip column on the far side of the dial;
	// landscape puts the chips beside the dial.
	double radius = out.portrait ? std::min({InGameTouchTheme::dialRadius, safe.w - chip - 2 * margin,
											 bottom - minimapBottom - header - 2 * margin})
								 : std::min({InGameTouchTheme::dialRadius, bottom - minimapBottom - margin,
											 safe.w / 2});
	radius = std::max(radius, InGameTouchTheme::dialMinimumRadius);
	const double scale = radius / InGameTouchTheme::dialRadius;
	const double thickness = InGameTouchTheme::dialRingThickness * std::clamp(scale, 0.8, 1.0),
				 gap = InGameTouchTheme::dialRingGap * scale;
	auto &g = out.geometry;
	g.mirrored = left;
	g.unit = unit;
	g.center = {(left ? safe.x : safe.x + safe.w) * unit, bottom * unit};
	for (int i = 0; i < 3; ++i)
	{
		const double outer = radius - i * (thickness + gap);
		g.rings[i] = {outer - thickness, outer};
	}
	g.sweepStart = InGameTouchTheme::dialSweepStart;
	g.sweepEnd = InGameTouchTheme::dialSweepEnd;
	ViewRect chips;
	if (out.portrait)
	{
		const double top = minimapBottom + header + 2 * margin;
		chips = {left ? safe.x + safe.w - margin - chip : safe.x + margin, top, chip,
				 bottom - margin - top};
	}
	else
	{
		const double farWidth = std::max(0.0, safe.w - radius - 3 * margin);
		const double farX = left ? safe.x + radius + 2 * margin : safe.x + margin;
		const double top = bottom - radius + header + margin;
		chips = {farX, top, farWidth, bottom - margin - top};
	}
	// The identity bar shares the stats column and the minimap's bottom edge.
	// Keep the allocation controls where they were; this is a HUD placement change.
	const ViewRect head{hud.stats.x / unit, (hud.minimap.y + hud.minimap.h) / unit - header,
			hud.stats.w / unit, header};
	const ViewRect quadrant{left ? safe.x : safe.x + safe.w - radius, bottom - radius, radius, radius};
	auto scaled = [unit](ViewRect r) { return ViewRect{r.x * unit, r.y * unit, r.w * unit, r.h * unit}; };
	const double x0 = std::min({quadrant.x, head.x, chips.x}), y0 = std::min({quadrant.y, head.y, chips.y});
	const double x1 = std::max({quadrant.x + quadrant.w, head.x + head.w, chips.x + chips.w}),
				 y1 = std::max({quadrant.y + quadrant.h, head.y + head.h, chips.y + chips.h});
	out.header = scaled(head);
	out.chips = scaled(chips);
	out.bounds = scaled({x0, y0, x1 - x0, y1 - y0});
	return out;
}

std::vector<GameGUITouch::DialRegion> GameGUITouch::dialRegions() const
{
	std::vector<DialRegion> regions;
	auto *b = inspectedBuilding();
	if (!b)
		return regions;
	const auto rows = buildingActions();
	const auto dial = dialLayout(layout());
	const auto &g = dial.geometry;
	const double unit = g.unit, half = InGameTouchTheme::target / 2 * unit;
	// A thumb-sized focus box on the ring, kept inside the dial's quadrant.
	auto boxAt = [&](int ring, double from, double to)
	{
		const auto c = TouchDial::point(g, g.rings[ring].middle(), (from + to) / 2);
		const double x = g.mirrored ? std::max(c.x - half, g.center.x) : std::min(c.x - half, g.center.x - 2 * half);
		return ViewRect{x, std::min(c.y - half, g.center.y - 2 * half), 2 * half, 2 * half};
	};
	auto slider = [&](int ring, const BuildingAction &row, int maximum)
	{
		const double pad =
			std::min(TouchDial::padAngle(g.rings[ring], InGameTouchTheme::dialPad), InGameTouchTheme::dialPadMaximumAngle);
		const double from = g.sweepStart + pad, to = g.sweepEnd - pad;
		for (auto [part, a0, a1] : {std::tuple{DialRegion::Minus, g.sweepStart, from},
									std::tuple{DialRegion::Arc, from, to},
									std::tuple{DialRegion::Plus, to, g.sweepEnd}})
		{
			DialRegion region;
			region.part = part;
			region.action = row;
			region.ring = ring;
			region.maximum = maximum;
			region.from = a0;
			region.to = a1;
			region.sliderFrom = from;
			region.sliderTo = to;
			region.box = boxAt(ring, a0, a1);
			regions.push_back(region);
		}
	};
	// Fixed semantic lanes: optional controls never move workers or priority.
	constexpr int workerRing = 0, sliderRing = 1, priorityRing = 2;
	std::vector<BuildingAction> actions, options;
	for (const auto &row : rows)
	{
		if (row.kind == 6)
			slider(workerRing, row, MAX_UNIT_WORKING);
		else if (row.kind == 7)
		{
			const std::string labels[] = {Toolkit::getStringTable()->getString("[↓ Low]"),
										  Toolkit::getStringTable()->getString("[= Normal]"),
										  Toolkit::getStringTable()->getString("[↑ High]")};
			const double step = (g.sweepEnd - g.sweepStart) / 3;
			for (int k = 0; k < 3; ++k)
			{
				DialRegion region;
				region.part = DialRegion::Segment;
				region.action = row;
				region.action.value = k - 1;
				region.action.label = labels[k];
				region.action.selected = gui.displayedPriority(*b) == k - 1;
				region.ring = priorityRing;
				region.from = g.sweepStart + k * step;
				region.to = region.from + step;
				region.box = boxAt(priorityRing, region.from, region.to);
				regions.push_back(region);
			}
		}
		else if (row.kind == 8)
			slider(sliderRing, row, b->type->maxUnitStayRange);
		else if (row.kind == 0)
		{
			if (row.value == 0)
			{
				DialRegion region;
				region.part = DialRegion::Proportions;
				region.action = row;
				region.ring = sliderRing;
				region.from = region.sliderFrom = g.sweepStart;
				region.to = region.sliderTo = g.sweepEnd;
				region.maximum = MAX_RATIO_RANGE;
				region.box = boxAt(sliderRing, region.from, region.to);
				regions.push_back(region);
				const auto ratios = gui.displayedRatio(*b);
				options.push_back({Toolkit::getStringTable()->getString("[Pause]"), 10, 0,
								   ratios[0] + ratios[1] + ratios[2] == 0});
			}
		}
		else if (row.kind >= 3 && row.kind <= 5)
			actions.push_back(row);
		else
			options.push_back(row);
	}
	// Actions sit nearest the toolbar: Destroy (or its confirmation) lowest.
	std::stable_sort(actions.begin(), actions.end(), [](const BuildingAction &a, const BuildingAction &b)
					 { return (a.kind == 3 ? 6 : a.kind) < (b.kind == 3 ? 6 : b.kind); });
	actions.insert(actions.end(), options.begin(), options.end());
	const double w = InGameTouchTheme::dialChipWidth * unit, h = InGameTouchTheme::dialChipHeight * unit,
				 space = 6 * unit;
	const auto &area = dial.chips;
	const int columns = dial.portrait ? 1 : std::max(1, int((area.w + space) / (w + space)));
	const bool left = g.mirrored;
	for (size_t i = 0; i < actions.size(); ++i)
	{
		const int row = int(i) / columns, column = int(i) % columns;
		// Fill from the bottom, starting next to the dial.
		const double x = dial.portrait ? area.x
						 : left		   ? area.x + column * (w + space)
									   : area.x + area.w - (column + 1) * w - column * space;
		const double y = area.y + area.h - (row + 1) * h - row * space;
		if (y < area.y - 0.5)
			break;
		DialRegion region;
		region.part = DialRegion::Chip;
		region.action = actions[i];
		region.box = {x, y, dial.portrait ? area.w : w, h};
		regions.push_back(region);
	}
	return regions;
}

std::optional<GameGUITouch::DialRegion> GameGUITouch::dialRegionAt(ViewPoint point) const
{
	const auto regions = dialRegions();
	if (regions.empty())
		return std::nullopt;
	const auto g = dialLayout(layout()).geometry;
	const auto polar = TouchDial::polar(g, point);
	std::optional<DialRegion> nearest;
	double nearestDistance = InGameTouchTheme::target;
	for (const auto &region : regions)
	{
		if (region.part == DialRegion::Chip)
		{
			if (region.box.contains(point))
				return region;
			continue;
		}
		if (!polar)
			continue;
		const auto &ring = g.rings[region.ring];
		const double tolerance = std::max(InGameTouchTheme::dialRingGap / 3,
			(InGameTouchTheme::target - (ring.outer - ring.inner)) / 2);
		// Pads reach to the quadrant's edges so a thumb overshooting an end still lands.
		const double from = (region.part == DialRegion::Minus || region.part == DialRegion::Proportions) ? 0 : region.from,
					 to = (region.part == DialRegion::Plus || region.part == DialRegion::Proportions) ? 90 : region.to;
		if (polar->radius >= ring.inner - tolerance && polar->radius <= ring.outer + tolerance &&
			polar->angle >= from && polar->angle <= to)
		{
			// Thin visible bands retain forgiving touch targets. Where those
			// targets overlap, the nearest band wins rather than the outer one.
			const double distance = std::abs(polar->radius - ring.middle());
			if (distance < nearestDistance)
			{
				nearest = region;
				nearestDistance = distance;
			}
		}
	}
	return nearest;
}

ViewPoint GameGUITouch::dialActionPoint(int kind, int value, int side) const
{
	const auto g = dialLayout(layout()).geometry;
	for (const auto &region : dialRegions())
	{
		if (region.action.kind != kind)
			continue;
		if (kind != 6 && kind != 8 && kind != 3 && kind != 4 && kind != 5 && region.action.value != value)
			continue;
		if (region.part == DialRegion::Chip)
			return {region.box.x + region.box.w / 2, region.box.y + region.box.h / 2};
		if (region.part == DialRegion::Segment)
			return TouchDial::point(g, g.rings[region.ring].middle(), (region.from + region.to) / 2);
		if ((side < 0 && region.part == DialRegion::Minus) || (side > 0 && region.part == DialRegion::Plus) ||
			(side == 0 && region.part == DialRegion::Arc))
			return TouchDial::point(g, g.rings[region.ring].middle(), (region.from + region.to) / 2);
	}
	return {-1, -1};
}

void GameGUITouch::tapDial(Building &b, const DialRegion &region, ViewPoint point)
{
	const auto &row = region.action;
	if (row.kind == 10)
	{
		const auto values = gui.displayedRatio(b);
		commitRatios(b, values[0] + values[1] + values[2] == 0
			? std::array<int, 3>{MAX_RATIO_RANGE, 0, 0} : std::array<int, 3>{0, 0, 0});
		return;
	}
	if (row.kind == 7)
	{
		gui.requestBuildingPriority(b, row.value);
		return;
	}
	if (row.kind == 6 || row.kind == 8 || row.kind == 0)
	{
		const int current = row.kind == 6	? gui.displayedMaxUnitWorking(b)
							: row.kind == 8 ? gui.displayedUnitStayRange(b)
											: gui.displayedRatio(b)[row.value];
		int next = current + (region.part == DialRegion::Minus ? -1 : region.part == DialRegion::Plus ? 1 : 0);
		if (region.part == DialRegion::Arc)
			if (const auto polar = TouchDial::polar(dialLayout(layout()).geometry, point))
				next = TouchDial::value(polar->angle, region.sliderFrom, region.sliderTo, region.maximum);
		if (row.kind == 6)
			gui.requestWorkerAllocation(b, next);
		else if (row.kind == 8)
			gui.requestFlagRange(b, next);
		else
			setRatio(b, row.value, next);
		return;
	}
	applyDiscreteAction(b, row);
}

std::array<int, 3> GameGUITouch::dialRatios() const
{
	if (allocation && allocation->divider >= 0)
		return allocation->ratios;
	if (const auto *b = inspectedBuilding())
		return gui.displayedRatio(*b);
	return {};
}

void GameGUITouch::drawDial()
{
	auto *b = inspectedBuilding();
	if (!b)
		return;
	auto *gfx = globalContainer->gfx;
	const auto dial = dialLayout(layout());
	const auto &g = dial.geometry;
	const double unit = g.unit;
	const auto regions = dialRegions();
	gfx->setClipRect();
	if (regions.empty())
	{
		const ViewRect note{dial.chips.x, dial.chips.y + dial.chips.h - InGameTouchTheme::dialChipHeight * unit,
							dial.chips.w, InGameTouchTheme::dialChipHeight * unit};
		gfx->drawFilledRect(int(note.x), int(note.y), int(note.w), int(note.h), InGameTouchTheme::paper);
		drawPointLabel(note, Toolkit::getStringTable()->getString("[Read-only building]"), .8);
		return;
	}
	bool rings[3] = {};
	for (const auto &region : regions)
		if (region.ring >= 0)
			rings[region.ring] = true;
	for (int i = 0; i < 3; ++i)
		if (rings[i])
			TouchDial::fill(g, g.rings[i].inner, g.rings[i].outer, g.sweepStart, g.sweepEnd,
							InGameTouchTheme::dialTrack);
	const double seam = 0.6; // Degrees left between neighbouring parts.
	std::vector<std::pair<ViewRect, std::string>> sliderCaptions;
	// Captions are sized to their text and kept on screen near the edge.
	const auto safe = layout().safe;
	auto captionRect = [&](ViewPoint centre, const std::string &text, double textScale)
	{
		const double w = std::min(safe.w, (globalContainer->standardFont->getStringWidth(text) *
												   gfx->textUnitsPerPoint() + 16 * unit) * textScale),
					 h = 22 * unit * InGameTouchTheme::textGrowth();
		const double x = std::clamp(centre.x - w / 2, safe.x, std::max(safe.x, safe.x + safe.w - w));
		return ViewRect{x, centre.y - h / 2, w, h};
	};
	for (const auto &region : regions)
	{
		const auto &row = region.action;
		if (region.part == DialRegion::Chip)
		{
			const auto &box = region.box;
			gfx->drawFilledRect(int(box.x), int(box.y), int(box.w), int(box.h),
								row.kind == 4	 ? InGameTouchTheme::destroy
								: row.selected ? InGameTouchTheme::selected
											   : InGameTouchTheme::field);
			gfx->drawRect(int(box.x), int(box.y), int(box.w), int(box.h), InGameTouchTheme::border);
			drawPointLabel(box, row.label, .78);
			continue;
		}
		const auto &ring = g.rings[region.ring];
		const auto centre = TouchDial::point(g, ring.middle(), (region.from + region.to) / 2);
		if (region.part == DialRegion::Proportions)
		{
			const auto ratios = dialRatios();
			const int total = ratios[0] + ratios[1] + ratios[2];
			const auto percentages = total ? TouchDial::shares(ratios, 100) : std::array<int, 3>{};
			const Color colors[] = {Color(222, 177, 77), Color(85, 183, 192), Color(202, 117, 165)};
			double angle = region.from;
			for (int type = 0; type < 3; ++type)
			{
				const double next = angle + (region.to - region.from) * ratios[type] / std::max(1, total);
				TouchDial::fill(g, ring.inner, ring.outer, angle, next, colors[type]);
				angle = next;
				// Read-only legend: every share remains legible, including zero.
				const auto &area = dial.chips;
				const double width = area.w / (dial.portrait ? 1 : 3);
				const ViewRect label{area.x + (dial.portrait ? 0 : type * width),
					area.y + (dial.portrait ? type * 24 * unit : 0), width, 22 * unit};
				gfx->drawFilledRect(int(label.x), int(label.y), int(label.w), int(label.h), InGameTouchTheme::readout);
				gfx->drawFilledRect(int(label.x), int(label.y), int(4 * unit), int(label.h), colors[type]);
				const auto text = std::string(getUnitName(type)) + " " +
					std::to_string(percentages[type]) + "%";
				drawPointLabel(label, text, .70);
			}
			// Stagger the two divider grips radially: even a zero-width middle
			// share leaves both grips visible and independently reachable.
			for (int divider = 0, sum = 0; divider < 2; ++divider)
			{
				sum += ratios[divider];
				const double at = TouchDial::angleOf(sum, region.from, region.to, std::max(1, total));
				const auto inside = TouchDial::point(g, ring.inner, at);
				const auto outside = TouchDial::point(g, ring.outer, at);
				gfx->drawLine(int(inside.x), int(inside.y), int(outside.x), int(outside.y), InGameTouchTheme::ink);
				const auto grip = TouchDial::point(g, divider == 0 ? ring.inner + 4 : ring.outer - 4, at);
				gfx->drawFilledRect(int(grip.x - 4 * unit), int(grip.y - 4 * unit), int(8 * unit), int(8 * unit), InGameTouchTheme::ink);
			}
			continue;
		}
		if (region.part == DialRegion::Segment)
		{
			TouchDial::fill(g, ring.inner, ring.outer, region.from + seam, region.to - seam,
							row.selected ? InGameTouchTheme::selected : InGameTouchTheme::field);
			drawPointLabel(captionRect(centre, row.label, .78), row.label, .78);
			continue;
		}
		if (region.part != DialRegion::Arc)
		{
			TouchDial::fill(g, ring.inner, ring.outer, region.from + seam, region.to - seam,
							InGameTouchTheme::dialPadFill);
			drawPointLabel({centre.x - 22 * unit, centre.y - 22 * unit, 44 * unit, 44 * unit},
						   region.part == DialRegion::Minus ? "−" : "+", 1.1);
			continue;
		}
		// The slider: the brass fill is the requested value (previewed while
		// dragging); on the worker ring a thin ink arc shows who is assigned.
		const bool dragging = allocation && allocation->polar && allocation->kind == row.kind &&
							  (row.kind != 0 || allocation->value == row.value);
		const int current = dragging		  ? allocation->requested
							: row.kind == 6 ? gui.displayedMaxUnitWorking(*b)
							: row.kind == 8 ? gui.displayedUnitStayRange(*b)
											: gui.displayedRatio(*b)[row.value];
		TouchDial::fill(g, ring.inner, ring.outer, region.from,
						TouchDial::angleOf(current, region.sliderFrom, region.sliderTo, region.maximum),
						InGameTouchTheme::dialFill);
		if (row.kind == 6)
			TouchDial::fill(g, ring.outer - 5, ring.outer, region.from,
							TouchDial::angleOf(int(b->unitsWorking), region.sliderFrom, region.sliderTo,
											   region.maximum),
							InGameTouchTheme::ink);
		sliderCaptions.push_back({captionRect(centre, row.label, .72), row.label});
	}
	// Slider captions go over every ring, so neighbouring fills never cover them.
	for (const auto &[caption, text] : sliderCaptions)
	{
		gfx->drawFilledRect(int(caption.x), int(caption.y), int(caption.w), int(caption.h), InGameTouchTheme::readout);
		drawPointLabel(caption, text, .72);
	}
	if (allocation && allocation->polar && allocation->divider < 0)
		TouchReadout::draw(allocation->position, std::to_string(allocation->requested), layout().safe);
}
