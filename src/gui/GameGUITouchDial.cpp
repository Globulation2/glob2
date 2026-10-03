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
	const double minimapBottom = safe.y + (safe.h < 400 ? 80 : 104);
	DialLayout out;
	out.portrait = safe.w <= safe.h;
	// Portrait puts the read-only header under the minimap and a full-height chip
	// column on the far side of the dial; landscape puts both beside the dial.
	double radius = out.portrait ? std::min({InGameTouchTheme::dialRadius, safe.w - chip - 3 * margin,
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
	ViewRect head, chips;
	if (out.portrait)
	{
		head = {safe.x, minimapBottom + margin, safe.w, header};
		chips = {left ? safe.x + safe.w - margin - chip : safe.x + margin, head.y + header + margin, chip,
				 bottom - margin - (head.y + header + margin)};
	}
	else
	{
		const double farWidth = std::max(0.0, safe.w - radius - 3 * margin);
		const double farX = left ? safe.x + radius + 2 * margin : safe.x + margin;
		const double headWidth = std::min(farWidth, 360.0);
		head = {left ? farX : farX + farWidth - headWidth, bottom - radius, headWidth, header};
		chips = {farX, head.y + header + margin, farWidth, bottom - margin - (head.y + header + margin)};
	}
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
	// Rings are assigned outward-in by presence: workers, then a ratio or range
	// slider, then priority, so sliders always get the longer outer arcs.
	auto has = [&](auto test) { return std::any_of(rows.begin(), rows.end(), test); };
	const int workerRing = has([](const BuildingAction &r) { return r.kind == 6; }) ? 0 : -1;
	const int sliderRing = has([](const BuildingAction &r) { return r.kind == 0 || r.kind == 8; })
							   ? workerRing + 1
							   : -1;
	const int priorityRing = std::max(workerRing, sliderRing) + 1;
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
			// Unit-type chips choose which ratio the inner ring edits.
			auto type = row;
			type.kind = 9;
			type.selected = row.value == ratioType;
			options.push_back(type);
			if (row.value == ratioType)
				slider(sliderRing, row, MAX_RATIO_RANGE);
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
	const double tolerance = InGameTouchTheme::dialRingGap / 3;
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
		// Pads reach to the quadrant's edges so a thumb overshooting an end still lands.
		const double from = region.part == DialRegion::Minus ? 0 : region.from,
					 to = region.part == DialRegion::Plus ? 90 : region.to;
		if (polar->radius >= ring.inner - tolerance && polar->radius <= ring.outer + tolerance &&
			polar->angle >= from && polar->angle <= to)
			return region;
	}
	return std::nullopt;
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
	if (row.kind == 9)
	{
		ratioType = row.value;
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
	if (allocation && allocation->polar)
		TouchReadout::draw(allocation->position, std::to_string(allocation->requested), layout().safe);
}
