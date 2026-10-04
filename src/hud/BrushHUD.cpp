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
Layout layout(ViewRect area, bool left, double unit, bool withMode, bool withPan, bool withUndo)
{
	Layout out;
	out.left = left;
	out.unit = unit;
	const int count = int(BrushTool::BRUSH_COUNT) + withMode + withPan;
	const double gap = InGameTouchTheme::gap * unit, width = InGameTouchTheme::brushRailWidth * unit;
	const double minimum = InGameTouchTheme::brushRailMinimumCell * unit;
	// Short (landscape) areas fold the rail into two columns.
	const bool twoColumns = count * (minimum + gap) > area.h;
	const int rows = twoColumns ? (withMode || withPan ? 1 : 0) + int(BrushTool::BRUSH_COUNT) / 2 : count;
	const double cell = std::clamp((area.h - gap) / rows - gap, minimum, InGameTouchTheme::target * unit);
	const int columns = twoColumns ? 2 : 1;
	const double railW = columns * width + (columns - 1) * gap;
	const double x0 = left ? area.x : area.x + area.w - railW;
	auto slot = [&](int row, int column)
	{
		// Column 0 is on the thumb side.
		const double x = left ? x0 + column * (width + gap) : x0 + railW - width - column * (width + gap);
		return ViewRect{x, area.y + area.h - (row + 1) * (cell + gap), width, cell};
	};
	int row = 0;
	if (twoColumns)
	{
		if (withMode || withPan)
		{
			if (withMode)
				out.mode = slot(0, 0);
			if (withPan)
				out.pan = slot(0, withMode ? 1 : 0);
			row = 1;
		}
		for (unsigned i = 0; i < BrushTool::BRUSH_COUNT; ++i)
			out.detents.push_back(slot(row + int(i) / 2, int(i) % 2));
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
		const auto foot = withMode ? out.mode : out.detents.front();
		out.undo = {left ? x0 + railW + gap : x0 - gap - w, foot.y, w, foot.h};
	}
	return out;
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
void drawButton(const ViewRect &r, const std::string &text, bool selected, double unit)
{
	auto *gfx = globalContainer->gfx;
	auto *font = globalContainer->standardFont;
	gfx->drawFilledRect(int(r.x), int(r.y), int(r.w), int(r.h),
						selected ? InGameTouchTheme::selected() : InGameTouchTheme::field());
	gfx->drawRect(int(r.x), int(r.y), int(r.w), int(r.h), InGameTouchTheme::border());
	InGameTouchTheme::TextStyle style(font);
	const double scale = std::min(0.8 * gfx->textUnitsPerPoint(), (r.w - 4 * unit) / std::max(1, font->getStringWidth(text)));
	SDL_Rect clip{int(r.x), int(r.y), int(r.w), int(r.h)};
	gfx->setUITransform(scale, r.x + (r.w - font->getStringWidth(text) * scale) / 2,
						r.y + (r.h - font->getStringHeight(text) * scale) / 2, &clip);
	gfx->drawString(0, 0, font, text);
	gfx->setUITransform();
	gfx->setClipRect();
}
} // namespace

void draw(const Layout &layout, const State &state)
{
	auto *gfx = globalContainer->gfx;
	const double unit = layout.unit;
	gfx->setClipRect();
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
		drawButton(layout.mode, state.modeLabel, state.erase, unit);
	if (layout.pan.w > 0)
		drawButton(layout.pan, state.panLabel, state.pan, unit);
	if (layout.undo.w > 0)
		drawButton(layout.undo, state.undoLabel, false, unit);
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
		SDL_Rect clip{int(box.x), int(box.y), int(box.w), int(box.h)};
		// Rises by however much larger text grew, so it stays inside the box.
		gfx->setUITransform(scale, box.x + (box.w - font->getStringWidth(size) * scale) / 2,
							box.y + box.h - 20 * unit - (scale - 0.8 * unit) * font->getStringHeight(size), &clip);
		gfx->drawString(0, 0, font, size);
		gfx->setUITransform();
		gfx->setClipRect();
	}
}
} // namespace BrushHUD
