// SPDX-License-Identifier: GPL-3.0-or-later
// A canvas that records what controls paint instead of drawing it, shared by the
// UI layout and presentation harnesses.
#pragma once
#include <ui/Canvas.h>
#include <ui/Element.h>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace glob2test
{
using namespace GAGGUI::ui;

struct RecordingCanvas : GAGGUI::ui::Canvas
{
	Size extent;
	const TextMeasurer &text_;
	std::vector<Rect> clips;
	std::vector<std::pair<Point, std::string>> texts;
	// Each painted line's extent at the measurer's size, already clipped.
	std::vector<std::pair<Rect, std::string>> textRects;
	struct IconDraw
	{
		Rect bounds;
		std::string name;
		GAGCore::Color color;
	};
	std::vector<IconDraw> icons;
	int fills = 0;
	RecordingCanvas(Size extent, const TextMeasurer &measurer) : extent(extent), text_(measurer)
	{
		clips.push_back({0, 0, extent.w, extent.h});
	}
	Size size() const override { return extent; }
	const TextMeasurer &measurer() const override { return text_; }
	void fillRect(Rect, GAGCore::Color) override { ++fills; }
	void strokeRect(Rect, GAGCore::Color) override {}
	void fillRounded(Rect, int, GAGCore::Color) override { ++fills; }
	void line(Point, Point, GAGCore::Color) override {}
	void text(Point at, FontRole role, const std::string &value, GAGCore::Color) override
	{
		if (value.empty())
			return;
		texts.push_back({at, value});
		const Rect extent{at.x, at.y, text_.width(role, value), text_.lineHeight(role)};
		textRects.push_back({extent.intersect(clips.back()), value});
	}
	void pushClip(Rect rect) override { clips.push_back(clips.back().intersect(rect)); }
	void popClip() override
	{
		if (clips.size() > 1)
			clips.pop_back();
	}
	Rect clip() const override { return clips.back(); }
	void drawSurface(Rect, GAGCore::DrawableSurface *, unsigned char) override {}
	void drawIcon(Rect r, const IconAsset &asset, GAGCore::Color color) override
	{
		icons.push_back({r, asset.name, color});
	}
	void drawSprite(Point, GAGCore::Sprite *, int) override {}
	void transformed(double, Point, Rect, const std::function<void()> &paint) override { paint(); }
};

// Every painted line must lie inside a control or clear of all of them; a line
// crossing a control's edge means its measured size could not hold its text.
// Returns a description of the first spill, or an empty string.
inline std::string textSpill(const RecordingCanvas &canvas, const std::vector<Node *> &controls,
							 const std::function<Rect(Node &)> &visible = {})
{
	for (const auto &[rect, value] : canvas.textRects)
	{
		if (rect.empty())
			continue;
		for (auto *node : controls)
		{
			const Rect shown = visible ? visible(*node) : node->bounds;
			if (!shown.intersect(rect).empty() && !shown.contains(rect))
				return "\"" + value + "\" spills out of " + node->key;
		}
	}
	return {};
}
} // namespace glob2test
