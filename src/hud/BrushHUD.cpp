// SPDX-License-Identifier: GPL-3.0-or-later
#include "BrushHUD.h"
#include "Brush.h"
#include "InGameTouchTheme.h"
#include "GlobalContainer.h"
#include <algorithm>
#include <cmath>
using namespace GAGCore;

namespace BrushHUD
{
static Layout makeLayout(ViewRect area, bool left, double unit, bool withMode, bool withPan, bool withUndo, bool bounded)
{
	Layout out;
	out.left = left;
	out.unit = unit;
	if (bounded && (!(unit > 0) || !std::isfinite(unit) || !std::isfinite(area.x) ||
		!std::isfinite(area.y) || !std::isfinite(area.w) || !std::isfinite(area.h) ||
		area.w <= 0 || area.h <= 0))
		return out;
	const int count = int(BrushTool::BRUSH_COUNT) + withMode + withPan;
	const double gap = InGameTouchTheme::gap * unit, width = InGameTouchTheme::brushRailWidth * unit;
	const double minimum = InGameTouchTheme::brushRailMinimumCell * unit;
	// Short (landscape) areas fold the rail into two columns.
	const bool twoColumns = count * (minimum + gap) > area.h;
	int columns = twoColumns ? 2 : 1;
	auto rowCount = [&](int n)
	{
		return n == 1 ? count : (withMode || withPan || (bounded && withUndo && n >= 3) ? 1 : 0) +
			(int(BrushTool::BRUSH_COUNT) + n - 1) / n;
	};
	if (bounded)
	{
		// Keep the ordinary layout where it fits. On short canvases spread the
		// same controls across more columns instead of growing into the header.
		bool fits = false;
		for (int candidate = columns; candidate <= int(BrushTool::BRUSH_COUNT); ++candidate)
		{
			const int rows = rowCount(candidate);
			const double requiredWidth = candidate * width + (candidate - 1) * gap +
				(withUndo && candidate < 3 ? gap + InGameTouchTheme::brushUndoWidth * unit : 0);
			if (requiredWidth <= area.w && rows * (minimum + gap) <= area.h)
			{
				columns = candidate;
				fits = true;
				break;
			}
		}
		// An existing palette toggle can free the canvas. Never publish tiny
		// controls or let a clipped-out size steal a different surface's input.
		if (!fits)
			return out;
	}
	const int rows = rowCount(columns);
	const double cell = std::clamp((area.h - gap) / rows - gap, minimum, InGameTouchTheme::target * unit);
	const double railW = columns * width + (columns - 1) * gap;
	const double x0 = left ? area.x : area.x + area.w - railW;
	auto slot = [&](int row, int column)
	{
		// Column 0 is on the thumb side.
		const double x = left ? x0 + column * (width + gap) : x0 + railW - width - column * (width + gap);
		return ViewRect{x, area.y + area.h - (row + 1) * (cell + gap), width, cell};
	};
	int row = 0;
	if (columns > 1)
	{
		if (withMode || withPan || (bounded && withUndo && columns >= 3))
		{
			if (withMode)
				out.mode = slot(0, 0);
			if (withPan)
				out.pan = slot(0, withMode ? 1 : 0);
			row = 1;
		}
		for (unsigned i = 0; i < BrushTool::BRUSH_COUNT; ++i)
			out.detents.push_back(slot(row + int(i) / columns, int(i) % columns));
	}
	else
	{
		if (withMode)
			out.mode = slot(row++, 0);
		for (unsigned i = 0; i < BrushTool::BRUSH_COUNT; ++i)
			out.detents.push_back(slot(row++, 0));
		if (withPan)
			out.pan = slot(row++, 0);
	}
	const double top = std::min(out.detents.back().y, withPan ? out.pan.y : out.detents.back().y);
	out.rail = {x0, top, railW, area.y + area.h - top};
	if (withUndo)
	{
		const double w = InGameTouchTheme::brushUndoWidth * unit;
		if (bounded && columns >= 3)
		{
			// The wider footer has spare cells: keep Undo inside the rail rather
			// than growing into the opposite-edge map button's reserved strip.
			const int used = int(withMode) + int(withPan);
			const auto first = slot(0, used);
			const double available = (columns - used) * width + (columns - used - 1) * gap;
			const double undoWidth = std::min(w, available);
			out.undo = {left ? first.x : first.x + first.w - undoWidth,
				first.y, undoWidth, first.h};
		}
		else
		{
			const auto foot = withMode ? out.mode : out.detents.front();
			out.undo = {left ? x0 + railW + gap : x0 - gap - w, foot.y, w, foot.h};
		}
	}
	return out;
}

Layout layout(ViewRect area, bool left, double unit, bool withMode, bool withPan, bool withUndo)
{
	return makeLayout(area, left, unit, withMode, withPan, withUndo, false);
}
Layout boundedLayout(ViewRect area, bool left, double unit, bool withMode, bool withPan, bool withUndo)
{
	return makeLayout(area, left, unit, withMode, withPan, withUndo, true);
}
Hit hit(const Layout &layout, ViewPoint point)
{
	if (layout.undo.w > 0 && layout.undo.contains(point))
		return {Part::Undo, -1};
	if (layout.mode.w > 0 && layout.mode.contains(point))
		return {Part::Mode, -1};
	if (layout.pan.w > 0 && layout.pan.contains(point))
		return {Part::Pan, -1};
	for (size_t i = 0; i < layout.detents.size(); ++i)
		if (layout.detents[i].contains(point))
			return {Part::Detent, int(i)};
	return {};
}

int detentAt(const Layout &layout, ViewPoint point)
{
	int best = -1;
	double distance = 0;
	for (size_t i = 0; i < layout.detents.size(); ++i)
	{
		const auto &r = layout.detents[i];
		const double d = std::hypot(point.x - (r.x + r.w / 2), point.y - (r.y + r.h / 2));
		if (best < 0 || d < distance)
		{
			best = int(i);
			distance = d;
		}
	}
	return best;
}

int detentAt(const Layout &layout, ViewPoint point, ViewRect clip)
{
	return clip.contains(point) ? detentAt(layout, point) : -1;
}
namespace
{
// A brush mask as cells centred in a box.
void drawMask(unsigned figure, ViewRect box, double cellSize, Color color)
{
	auto *gfx = globalContainer->gfx;
	const int w = BrushTool::getBrushWidth(figure), h = BrushTool::getBrushHeight(figure);
	const double x0 = box.x + (box.w - w * cellSize) / 2, y0 = box.y + (box.h - h * cellSize) / 2;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
			if (BrushTool::getBrushValue(figure, x, y, 0, 0, -1, -1))
				gfx->drawFilledRect(int(x0 + x * cellSize), int(y0 + y * cellSize),
									std::max(1, int(cellSize) - 1), std::max(1, int(cellSize) - 1), color);
}
void applyClip(const ViewRect *limit)
{
	auto *gfx = globalContainer->gfx;
	if (limit)
		gfx->setClipRect(int(std::floor(limit->x)), int(std::floor(limit->y)),
			int(std::ceil(limit->x + limit->w) - std::floor(limit->x)),
			int(std::ceil(limit->y + limit->h) - std::floor(limit->y)));
	else
		gfx->setClipRect();
}
SDL_Rect textClip(ViewRect bounds, const ViewRect *limit)
{
	if (limit)
	{
		const double right = std::min(bounds.x + bounds.w, limit->x + limit->w);
		const double bottom = std::min(bounds.y + bounds.h, limit->y + limit->h);
		bounds.x = std::max(bounds.x, limit->x);
		bounds.y = std::max(bounds.y, limit->y);
		bounds.w = std::max(0., right - bounds.x);
		bounds.h = std::max(0., bottom - bounds.y);
	}
	if (!(bounds.w > 0 && bounds.h > 0))
		return {0, 0, 0, 0};
	return {int(std::floor(bounds.x)), int(std::floor(bounds.y)),
		int(std::ceil(bounds.x + bounds.w) - std::floor(bounds.x)),
		int(std::ceil(bounds.y + bounds.h) - std::floor(bounds.y))};
}
void drawButton(const ViewRect &r, const std::string &text, bool selected, double unit, const ViewRect *limit)
{
	auto *gfx = globalContainer->gfx;
	auto *font = globalContainer->standardFont;
	gfx->drawFilledRect(int(r.x), int(r.y), int(r.w), int(r.h),
						selected ? InGameTouchTheme::selected() : InGameTouchTheme::field());
	gfx->drawRect(int(r.x), int(r.y), int(r.w), int(r.h), InGameTouchTheme::border());
	InGameTouchTheme::TextStyle style(font);
	const double scale = std::min(0.8 * gfx->textUnitsPerPoint(), (r.w - 4 * unit) / std::max(1, font->getStringWidth(text)));
	SDL_Rect clip = textClip(r, limit);
	gfx->setUITransform(scale, r.x + (r.w - font->getStringWidth(text) * scale) / 2,
						r.y + (r.h - font->getStringHeight(text) * scale) / 2, &clip);
	gfx->drawString(0, 0, font, text);
	gfx->setUITransform();
	applyClip(limit);
}
} // namespace

static void drawLayout(const Layout &layout, const State &state, const ViewRect *limit)
{
	if (layout.detents.empty() || (limit && !(limit->w > 0 && limit->h > 0)))
		return;
	auto *gfx = globalContainer->gfx;
	const double unit = layout.unit;
	applyClip(limit);
	gfx->drawFilledRect(int(layout.rail.x - 2 * unit), int(layout.rail.y - 2 * unit), int(layout.rail.w + 4 * unit),
						int(layout.rail.h + 2 * unit), InGameTouchTheme::paper());
	for (size_t i = 0; i < layout.detents.size(); ++i)
	{
		const auto &r = layout.detents[i];
		const bool selected = i == state.figure;
		gfx->drawFilledRect(int(r.x), int(r.y), int(r.w), int(r.h),
							selected ? InGameTouchTheme::selected() : InGameTouchTheme::field());
		if (selected)
			gfx->drawRect(int(r.x), int(r.y), int(r.w), int(r.h), InGameTouchTheme::border());
		drawMask(unsigned(i), r, std::max(2.0, std::min(r.w, r.h) * 0.7 / 5),
				 selected ? InGameTouchTheme::ink() : InGameTouchTheme::border());
	}
	if (layout.mode.w > 0)
		drawButton(layout.mode, state.modeLabel, state.erase, unit, limit);
	if (layout.pan.w > 0)
		drawButton(layout.pan, state.panLabel, state.pan, unit, limit);
	if (layout.undo.w > 0)
		drawButton(layout.undo, state.undoLabel, false, unit, limit);
	if (state.touched >= 0 && state.touched < int(layout.detents.size()))
	{
		// The touched size, magnified beside the rail where the thumb cannot hide it.
		const auto &r = layout.detents[state.touched];
		const double side = InGameTouchTheme::brushPreviewSize * unit;
		const double y = std::clamp(r.y + r.h / 2 - side / 2, layout.rail.y,
									std::max(layout.rail.y, layout.rail.y + layout.rail.h - side));
		const ViewRect box{layout.left ? layout.rail.x + layout.rail.w + 8 * unit : layout.rail.x - 8 * unit - side, y,
						   side, side};
		gfx->drawFilledRect(int(box.x), int(box.y), int(box.w), int(box.h), InGameTouchTheme::readout());
		for (int i = 0; i < std::max(1, int(2 * unit)); ++i)
			gfx->drawRect(int(box.x) + i, int(box.y) + i, int(box.w) - 2 * i, int(box.h) - 2 * i,
						  InGameTouchTheme::border());
		const ViewRect cells{box.x, box.y, box.w, box.h - 18 * unit};
		drawMask(unsigned(state.touched), cells, (side - 28 * unit) / 5, InGameTouchTheme::ink());
		const auto figure = unsigned(state.touched);
		const std::string size = std::to_string(BrushTool::getBrushWidth(figure)) + " × " +
								 std::to_string(BrushTool::getBrushHeight(figure));
		auto *font = globalContainer->standardFont;
		InGameTouchTheme::TextStyle style(font);
		const double scale = 0.8 * gfx->textUnitsPerPoint();
		SDL_Rect clip = textClip(box, limit);
		// Rises by however much larger text grew, so it stays inside the box.
		gfx->setUITransform(scale, box.x + (box.w - font->getStringWidth(size) * scale) / 2,
							box.y + box.h - 20 * unit - (scale - 0.8 * unit) * font->getStringHeight(size), &clip);
		gfx->drawString(0, 0, font, size);
		gfx->setUITransform();
		applyClip(limit);
	}
}
void draw(const Layout &layout, const State &state)
{
	drawLayout(layout, state, nullptr);
}
void draw(const Layout &layout, const State &state, ViewRect clip)
{
	drawLayout(layout, state, &clip);
	globalContainer->gfx->setClipRect();
}
} // namespace BrushHUD
