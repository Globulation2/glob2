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
#include "TeamDisplay.h"
#include <Toolkit.h>
#include <StringTable.h>
#include <algorithm>
#include <cmath>
using namespace GAGCore;

bool GameGUITouch::usesDial() const
{
	return usesDial(layout());
}

bool GameGUITouch::usesDial(const MobileLayout &ui) const
{
	if (!usesHUD() || ui.persistentPanel)
		return false;
	// The minimum ring size preserves usable controls, but is not permission
	// to cover the HUD or safe-area gutters. Fall back to scrolling rows when
	// that minimum cannot fit. This policy never asks layout() to resolve itself.
	const auto dial = dialLayout(ui);
	const double unit = dial.geometry.unit;
	const double radius = dial.geometry.rings[0].outer * unit;
	const auto hud = hudLayout(ui);
	return dial.geometry.center.y - radius >= hud.minimap.y + hud.minimap.h + 4 * unit &&
		ui.safe.w >= radius + (dial.portrait ? 16 : InGameTouchTheme::dialChipWidth + 16) * unit &&
		dial.chips.h >= InGameTouchTheme::dialChipHeight * unit && dialChips(dial).fits;
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
	// Portrait keeps action chips above the arc quadrant; landscape puts them
	// beside it. The shared header already sits beneath the stats, so the arc
	// can use the remaining height below the minimap.
	double radius = out.portrait ? std::min({InGameTouchTheme::dialRadius, safe.w - chip - 2 * margin + InGameTouchTheme::dialRingThickness + InGameTouchTheme::dialRingGap,
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
	for (int i = 0; i < 5; ++i)
	{
		const double outer = radius - i * (thickness + gap);
		g.rings[i] = {outer - thickness, outer};
	}
	g.sweepStart = InGameTouchTheme::dialSweepStart;
	g.sweepEnd = InGameTouchTheme::dialSweepEnd;
	// A short landscape screen needs a shallower sweep at the larger radius
	// to keep both thumb sides below the header's production readouts.
	if (!out.portrait)
		g.sweepEnd = std::min(g.sweepEnd, std::asin(std::clamp(
			(bottom - minimapBottom - 30) / radius, 0., 1.)) * 180 / 3.141592653589793);
	ViewRect chips;
	if (out.portrait)
	{
		const double top = minimapBottom + header + 2 * margin;
		chips = {left ? safe.x + safe.w - margin - chip : safe.x + margin, top, chip,
				 bottom - radius - margin - top};
	}
	else
	{
		const double farWidth = std::max(0.0, safe.w - radius - 3 * margin);
		const double farX = left ? safe.x + radius + 2 * margin : safe.x + margin;
		const double top = bottom - radius + header + margin;
		chips = {farX, top, farWidth, bottom - margin - top};
	}
	// The identity bar shares the stats column and the minimap's bottom edge.
	// Allocation geometry above reserves clearance for this shared HUD.
	const ViewRect head{hud.identity.x / unit, hud.identity.y / unit,
			hud.identity.w / unit, hud.identity.h / unit};
	const ViewRect quadrant{left ? safe.x : safe.x + safe.w - radius, bottom - radius, radius, radius};
	auto scaled = [unit](ViewRect r) { return ViewRect{r.x * unit, r.y * unit, r.w * unit, r.h * unit}; };
	const double x0 = std::min({quadrant.x, head.x, chips.x}), y0 = std::min({quadrant.y, head.y, chips.y});
	const double x1 = std::max({quadrant.x + quadrant.w, head.x + head.w, chips.x + chips.w}),
				 y1 = std::max({quadrant.y + quadrant.h, head.y + head.h, chips.y + chips.h});
	// The arc origin is at the screen corner. Inset the circular destroy target
	// into its hollow center so its full touch area stays above the toolbar.
	const double diameter = InGameTouchTheme::target;
	out.destroy = scaled({left ? safe.x + margin : safe.x + safe.w - margin - diameter,
		bottom - margin - diameter, diameter, diameter});
	out.header = scaled(head);
	out.chips = scaled(chips);
	out.bounds = scaled({x0, y0, x1 - x0, y1 - y0});
	return out;
}

GameGUITouch::DialChips GameGUITouch::dialChips(const DialLayout &dial) const
{
	// Shared by the fit policy, drawing and hit testing. Never call layout() or
	// usesDial() here: layout itself needs this content measurement to choose
	// between the dial and scrollable rows, including confirmation controls.
	DialChips result;
	if (confirmDestroy) return result;
	std::vector<BuildingAction> options;
	bool production = false;
	for (const auto &row : buildingActions())
	{
		if (row.kind == 0)
		{
			production |= row.value == 0;
		}
		else if (row.kind == 4)
		{
			if (confirmDestroy) result.actions.push_back(row);
		}
		else if (row.kind == 3 || row.kind == 5)
			result.actions.push_back(row);
		else if (row.kind != 6 && row.kind != 7 && row.kind != 8 && row.kind != 1 &&
			row.kind != 2 && row.kind != 12 && row.kind != 11)
			options.push_back(row);
	}
	// Actions sit nearest the toolbar: Destroy (or its confirmation) lowest.
	std::stable_sort(result.actions.begin(), result.actions.end(), [](const BuildingAction &a, const BuildingAction &b)
		{ return (a.kind == 3 ? 6 : a.kind) < (b.kind == 3 ? 6 : b.kind); });
	result.actions.insert(result.actions.end(), options.begin(), options.end());
	const double unit = dial.geometry.unit;
	const double w = InGameTouchTheme::dialChipWidth * unit, h = InGameTouchTheme::dialChipHeight * unit,
		space = 6 * unit;
	const auto &area = dial.chips;
	result.legend = {dial.header.x, dial.header.y + dial.header.h + 4 * unit,
		dial.header.w, production ? 26 * unit : 0};
	// Keep the action column's breathing room as the readouts move to the header.
	const double top = area.y + (production ? (dial.portrait ? 72 : 24) * unit + space : 0);
	result.fits = top <= area.y + area.h;
	const int columns = dial.portrait ? 1 : std::max(1, int((area.w + space) / (w + space)));
	for (size_t i = 0; i < result.actions.size(); ++i)
	{
		const int row = int(i) / columns, column = int(i) % columns;
		const double x = dial.portrait ? area.x : dial.geometry.mirrored
			? area.x + column * (w + space) : area.x + area.w - (column + 1) * w - column * space;
		const double y = area.y + area.h - (row + 1) * h - row * space;
		const ViewRect box{x, y, dial.portrait ? area.w : w, h};
		result.fits &= box.y >= top && box.x >= area.x && box.x + box.w <= area.x + area.w;
		result.boxes.push_back(box);
	}
	return result;
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
	// Confirmation replaces the entire dial with two choices on its middle lane.
	if (confirmDestroy)
	{
		const double middle = (g.sweepStart + g.sweepEnd) / 2;
		for (int kind : {5, 4})
		{
			const auto action = std::find_if(rows.begin(), rows.end(),
				[kind](const BuildingAction &row) { return row.kind == kind; });
			if (action == rows.end()) continue;
			DialRegion region;
			region.part = DialRegion::Segment;
			region.action = *action;
			if (kind == 4) region.action.label = Toolkit::getStringTable()->getString("[destroy]");
			region.ring = 2;
			region.from = kind == 5 ? g.sweepStart : middle;
			region.to = kind == 5 ? middle : g.sweepEnd;
			region.box = boxAt(region.ring, region.from, region.to);
			regions.push_back(region);
		}
		return regions;
	}
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
	constexpr int workerRing = 0, priorityRing = 1, sliderRing = 2, levelRing = 3, materialRing = 4;
	for (const auto &row : rows)
	{
		if (row.kind == 4)
		{
			DialRegion region;
			region.part = DialRegion::Destroy;
			region.action = row;
			region.box = dial.destroy;
			regions.push_back(region);
		}
		else if (row.kind == 6)
			slider(workerRing, row, b->type->semantics.assignmentLimit);
		else if (row.kind == 7)
		{
			const std::string labels[] = {Toolkit::getStringTable()->getString("[↓ Low]"),
										  std::string("• ") + Toolkit::getStringTable()->getString("[Normal]"),
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
		else if (row.kind == 1 || row.kind == 2 || row.kind == 12 || row.kind == 11)
		{
			const int count = int(std::count_if(rows.begin(), rows.end(), [&](const auto &other) {
				return other.kind == row.kind;
			}));
			const int index = int(std::count_if(rows.begin(), std::find_if(rows.begin(), rows.end(), [&](const auto &other) {
				return other.kind == row.kind && other.value == row.value;
			}), [&](const auto &other) { return other.kind == row.kind; }));
			DialRegion region;
			region.part = row.kind == 1 ? DialRegion::Toggle : DialRegion::Radio;
			region.action = row;
			if (row.kind == 2 || row.kind == 12)
				region.action.label = std::to_string(row.value + 1);
			region.ring = row.kind == 1 ? materialRing : levelRing;
			const double step = (g.sweepEnd - g.sweepStart) / count;
			region.from = g.sweepStart + index * step;
			region.to = region.from + step;
			region.box = boxAt(region.ring, region.from, region.to);
			regions.push_back(region);
		}
		else if (row.kind == 8)
			slider(sliderRing, row, b->type->maxUnitStayRange);
		else if (row.kind == 0)
		{
			const auto ratios = dialRatios();
			const int total = ratios[0] + ratios[1] + ratios[2] + 3;
			double from = g.sweepStart;
			for (int type = 0; type < row.value; ++type)
				from += (g.sweepEnd - g.sweepStart) * (ratios[type] + 1) / total;
			DialRegion region;
			region.part = DialRegion::RatioButton;
			region.action = row;
			region.ring = sliderRing;
			region.from = from;
			region.to = from + (g.sweepEnd - g.sweepStart) * (ratios[row.value] + 1) / total;
			region.box = boxAt(sliderRing, region.from, region.to);
			regions.push_back(region);
		}
	}
	const auto chips = dialChips(dial);
	for (size_t i = 0; i < chips.actions.size(); ++i)
	{
		DialRegion region;
		region.part = DialRegion::Chip;
		region.action = chips.actions[i];
		region.box = chips.boxes[i];
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
		if (region.part == DialRegion::Destroy)
		{
			if (std::hypot(point.x - region.box.x - region.box.w / 2,
				point.y - region.box.y - region.box.h / 2) <= region.box.w / 2) return region;
			continue;
		}
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
		const double from = region.part == DialRegion::Minus ? 0 : region.from,
					 to = region.part == DialRegion::Plus ? 90 : region.to;
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
		if (region.part == DialRegion::Chip || region.part == DialRegion::Destroy)
			return {region.box.x + region.box.w / 2, region.box.y + region.box.h / 2};
		if (region.part == DialRegion::Segment || region.part == DialRegion::RatioButton ||
			region.part == DialRegion::Radio || region.part == DialRegion::Toggle)
			return TouchDial::point(g, g.rings[region.ring].middle(), (region.from + region.to) / 2);
		if ((side < 0 && region.part == DialRegion::Minus) || (side > 0 && region.part == DialRegion::Plus) ||
			(side == 0 && region.part == DialRegion::Arc))
			return TouchDial::point(g, g.rings[region.ring].middle(), (region.from + region.to) / 2);
	}
	return {-1, -1};
}

void GameGUITouch::tapDial(const SceneBuildingPanel &b, const DialRegion &region, ViewPoint point)
{
	const auto &row = region.action;
	if (row.kind == 0 && region.part == DialRegion::RatioButton)
	{
		const int current = gui.displayedRatio(b)[row.value];
		// Existing saves may have other weights: advance to the next preset.
		const std::array<int, 5> presets{0, 1, 2, 3, 5};
		const auto next = std::upper_bound(presets.begin(), presets.end(), current);
		setRatio(b, row.value, next == presets.end() ? 0 : *next);
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
	const auto legend = dialChips(dial).legend;
	gfx->setClipRect();
	if (regions.empty())
	{
		const ViewRect note{dial.chips.x, dial.chips.y + dial.chips.h - InGameTouchTheme::dialChipHeight * unit,
							dial.chips.w, InGameTouchTheme::dialChipHeight * unit};
		gfx->drawFilledRect(int(note.x), int(note.y), int(note.w), int(note.h), InGameTouchTheme::paper());
		drawPointLabel(note, Toolkit::getStringTable()->getString("[Read-only building]"), .8);
		return;
	}
	bool rings[5] = {};
	for (const auto &region : regions)
		if (region.ring >= 0)
			rings[region.ring] = true;
	for (int i = 0; i < 5; ++i)
		if (rings[i])
			dialPainter.fill(g, g.rings[i].inner, g.rings[i].outer, g.sweepStart, g.sweepEnd,
							(i == 0 ? Color(42, 30, 57) : InGameTouchTheme::dialTrack()));
	if (confirmDestroy)
	{
		const auto &caption = g.rings[1];
		dialPainter.fill(g, caption.inner, caption.outer, g.sweepStart, g.sweepEnd,
			InGameTouchTheme::readout());
		dialPainter.label(g, caption, g.sweepStart, g.sweepEnd,
			Toolkit::getStringTable()->getString("[Destroy this building?]"),
			globalContainer->standardFont, .82 * gfx->textUnitsPerPoint(), InGameTouchTheme::ink());
	}
	const double seam = 0.8; // Degrees left between neighbouring parts.
	std::vector<std::tuple<TouchDial::Ring, double, double, std::string>> sliderCaptions;
	for (const auto &region : regions)
	{
		const auto &row = region.action;
		if (region.part == DialRegion::Destroy)
		{
			const auto &box = region.box;
			dialPainter.circle({box.x + box.w / 2, box.y + box.h / 2}, box.w / 2,
				InGameTouchTheme::destroy(), &InGameTouchTheme::ink());
			continue;
		}
		if (region.part == DialRegion::Chip)
		{
			const auto &box = region.box;
			gfx->drawFilledRect(int(box.x), int(box.y), int(box.w), int(box.h),
								row.kind == 4	 ? InGameTouchTheme::destroy()
								: row.selected ? InGameTouchTheme::selected()
											   : InGameTouchTheme::field());
			gfx->drawRect(int(box.x), int(box.y), int(box.w), int(box.h), InGameTouchTheme::border());
			drawPointLabel(box, row.label, .78);
			continue;
		}
		const auto &ring = g.rings[region.ring];
		if (region.part == DialRegion::RatioButton)
		{
			const auto ratios = dialRatios();
			const Color colors[] = {Color(222, 177, 77), Color(85, 183, 192), Color(202, 117, 165)};
			const int type = row.value;
			dialPainter.fill(g, ring.inner, ring.outer, region.from + seam, region.to - seam, colors[type]);
			const auto number = std::to_string(ratios[type]);
			// Colored ratio sectors are bright, so use dark ink for their numbers.
			const auto teamColor = presentationColor(b->owner().color);
			dialPainter.label(g, ring, region.from + seam, region.to - seam, ratios[type] == 0 ? std::string{} : number,
				globalContainer->standardFont, .85 * gfx->textUnitsPerPoint(), Color(32, 25, 35),
				globalContainer->unitmini, type, &teamColor);
			const double gap = 4 * unit, width = (legend.w - 2 * gap) / 3;
			const ViewRect label{legend.x + type * (width + gap), legend.y, width, legend.h};
			gfx->drawFilledRect(float(label.x), float(label.y), float(label.w), float(label.h), InGameTouchTheme::readout());
			gfx->drawFilledRect(float(label.x), float(label.y + label.h - 2 * unit), float(label.w), float(2 * unit), colors[type]);
			drawPointLabel(label, std::string(getUnitName(type)) + " " + number, .76);
			continue;
		}
		if (region.part == DialRegion::Segment || region.part == DialRegion::Radio || region.part == DialRegion::Toggle)
		{
			dialPainter.fill(g, ring.inner, ring.outer, region.from + seam, region.to - seam,
							row.kind == 4 ? Color(162, 43, 52) :
							row.selected ? Color(232, 194, 107) : InGameTouchTheme::field());
			if (row.selected && (region.part == DialRegion::Radio || region.part == DialRegion::Toggle))
				dialPainter.fill(g, ring.inner, ring.inner + 3, region.from + seam, region.to - seam,
					InGameTouchTheme::dialFill());
			const bool level = row.kind == 2 || row.kind == 12;
			const auto teamColor = presentationColor(b->owner().color);
			dialPainter.label(g, ring, region.from + seam, region.to - seam, row.label,
				globalContainer->standardFont, .95 * gfx->textUnitsPerPoint(),
				row.selected ? Color(32, 25, 35) : InGameTouchTheme::ink(),
				level ? globalContainer->unitmini : nullptr, row.kind == 2 ? WARRIOR : WORKER,
				&teamColor, row.kind == 4);
			continue;
		}
		if (region.part != DialRegion::Arc)
		{
			dialPainter.fill(g, ring.inner, ring.outer, region.from + seam, region.to - seam,
							InGameTouchTheme::dialPadFill());
			dialPainter.label(g, ring, region.from + seam, region.to - seam,
				region.part == DialRegion::Minus ? "−" : "+", globalContainer->standardFont,
				1.1 * gfx->textUnitsPerPoint(), InGameTouchTheme::ink());
			continue;
		}
		// Worker progress stays dark beneath its pale readout at every value.
		// A thin gold edge shows the actual assignment independently of target.
		const bool dragging = allocation && allocation->polar && allocation->kind == row.kind &&
							  (row.kind != 0 || allocation->value == row.value);
		const int current = dragging		  ? allocation->requested
							: row.kind == 6 ? gui.displayedMaxUnitWorking(*b)
							: row.kind == 8 ? gui.displayedUnitStayRange(*b)
											: gui.displayedRatio(*b)[row.value];
		dialPainter.fill(g, ring.inner, ring.outer, region.from,
						TouchDial::angleOf(current, region.sliderFrom, region.sliderTo, region.maximum),
						(row.kind == 6 ? Color(100, 69, 37) : InGameTouchTheme::dialFill()));
		if (row.kind == 6)
			dialPainter.fill(g, ring.outer - 5, ring.outer, region.from,
							TouchDial::angleOf(int(b->state().working.count), region.sliderFrom, region.sliderTo,
											   region.maximum),
							Color(232, 194, 107));
		if (row.kind == 6)
		{
			const auto teamColor = presentationColor(b->owner().color);
			const auto counts = std::to_string(b->state().working.count) + " / " + std::to_string(current);
			dialPainter.label(g, ring, region.from + seam, region.to - seam, counts,
				globalContainer->standardFont, 1.0 * gfx->textUnitsPerPoint(), Color(255, 239, 204),
				globalContainer->unitmini, WORKER, &teamColor);
			continue;
		}
		const TouchDial::Ring captionRing = ring;
		const double textWidth = globalContainer->standardFont->getStringWidth(row.label) *
			.95 * gfx->textUnitsPerPoint() / unit;
		const double span = std::min(region.to - region.from,
			(textWidth + 16) / captionRing.middle() * 180 / M_PI);
		const double middle = (region.from + region.to) / 2;
		sliderCaptions.emplace_back(captionRing, middle - span / 2, middle + span / 2, row.label);
	}
	// Draw curved slider readouts last so neighbouring fills never cover them.
	for (const auto &[ring, from, to, text] : sliderCaptions)
	{
		dialPainter.fill(g, ring.inner, ring.outer, from, to, InGameTouchTheme::readout());
		dialPainter.label(g, ring, from, to, text, globalContainer->standardFont,
			.95 * gfx->textUnitsPerPoint(), InGameTouchTheme::ink());
	}
	if (allocation && allocation->polar)
		TouchReadout::draw(allocation->position, std::to_string(allocation->requested), layout().safe);
}
