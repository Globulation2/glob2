// SPDX-License-Identifier: GPL-3.0-or-later
#include "HandPreferenceScreen.h"
#include "GlobalContainer.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
// Each button presents a separate one-handed grip. Coordinates are mirrored
// from a left-hand master, so both choices have identical visual weight.
void grip(Glob2UI::Canvas &c, Glob2UI::Rect r, bool left, bool pressed, bool focused)
{
	using namespace Glob2UI;
	const auto &p = frontendTheme().palette;
	const bool landscape = r.w > r.h * 1.5;
	const double designWidth = landscape ? 220 : 156;
	const double designHeight = landscape ? 126 : 196;
	const double handOffset = landscape ? 38 : 0;
	const double scale = std::min(r.w / designWidth, r.h / designHeight);
	const double ox = r.x + (r.w - designWidth * scale) / 2;
	const double oy = r.y + (r.h - designHeight * scale) / 2;
	auto box = [&](double x, double y, double w, double h) {
		return Rect{int(ox + (left ? x : designWidth - x - w) * scale), int(oy + y * scale),
			std::max(1, int(w * scale)), std::max(1, int(h * scale))};
	};
	auto rounded = [&](double x, double y, double w, double h, double radius, GAGCore::Color color) {
		c.fillRounded(box(x, y, w, h), std::max(1, int(radius * scale)), color);
	};
	c.pushClip(r);
	// A light upper lip and darker lower edge give the entire choice a bevel.
	rounded(3, 4, designWidth - 6, designHeight - 8, 18, p.muted);
	rounded(3, 2, designWidth - 6, designHeight - 9, 18, focused ? p.focus : p.line);
	rounded(5, 4, designWidth - 10, designHeight - 13, 16, pressed ? p.line : p.field);
	rounded(7, 6, designWidth - 14, designHeight - 17, 14, pressed ? p.rail : p.paper);
	const int shift = pressed ? 2 : 0;
	const double phoneWidth = landscape ? 158 : 91;
	const double phoneHeight = landscape ? 101 : 165;
	rounded(49, 13 + shift, phoneWidth, phoneHeight, 13, p.ink);
	rounded(51, 15 + shift, phoneWidth - 4, phoneHeight - 6, 11, p.muted);
	rounded(54, 18 + shift, phoneWidth - 10, phoneHeight - 12, 9, p.ink);
	rounded(57, 21 + shift, phoneWidth - 16, phoneHeight - 18, 7, p.field);
	if (landscape) {
		rounded(60, 48 + shift, 4, 27, 2, p.ink);
		rounded(197, 50 + shift, 2, 22, 1, p.line);
	} else {
		rounded(80, 23 + shift, 29, 5, 2, p.ink);
		rounded(84, 160 + shift, 22, 2, 1, p.line);
	}
	// The circular control sits under the thumb, with a raised ivory rim.
	rounded(69, 85 - handOffset + shift, 36, 38, 18, p.ink);
	rounded(69, 83 - handOffset + shift, 36, 36, 18, p.field);
	rounded(72, 86 - handOffset + shift, 30, 30, 15, p.accent);
	struct P { double x, y; };
	std::vector<P> outline{{23, 184}};
	auto curve = [&](P a, P b, P end) {
		const P start = outline.back();
		for (int i = 1; i <= 24; ++i) {
			const double t = i / 24., u = 1 - t;
			outline.push_back({u*u*u*start.x + 3*u*u*t*a.x + 3*u*t*t*b.x + t*t*t*end.x,
				u*u*u*start.y + 3*u*u*t*a.y + 3*u*t*t*b.y + t*t*t*end.y});
		}
	};
	curve({22, 165}, {16, 146}, {18, 126});
	curve({18, 101}, {27, 75}, {38, 71});
	curve({45, 68}, {53, 72}, {53, 79});
	curve({53, 87}, {45, 91}, {43, 104});
	curve({51, 113}, {58, 102}, {71, 91});
	curve({79, 84}, {91, 85}, {94, 93});
	curve({98, 104}, {83, 114}, {73, 125});
	curve({64, 135}, {59, 140}, {61, 152});
	curve({61, 165}, {68, 174}, {71, 184});
	auto paint = [&](const std::vector<P> &path, GAGCore::Color color) {
		for (int y = 0; y < int((designHeight - 12) * scale); ++y) {
			const double level = (y + .5) / scale + handOffset;
			std::vector<double> crossings;
			for (size_t i = 0; i < path.size(); ++i) {
				const auto a = path[i], b = path[(i + 1) % path.size()];
				if ((a.y > level) != (b.y > level))
					crossings.push_back(a.x + (level - a.y) * (b.x - a.x) / (b.y - a.y));
			}
			std::sort(crossings.begin(), crossings.end());
			for (size_t i = 0; i + 1 < crossings.size(); i += 2) {
				const double x1 = ox + (left ? crossings[i] : designWidth - crossings[i+1]) * scale;
				const double x2 = ox + (left ? crossings[i+1] : designWidth - crossings[i]) * scale;
				const int lo = int(std::ceil(x1)), hi = int(std::floor(x2));
				const int py = int(oy) + y + int(shift * scale);
				if (hi > lo) c.fillRect({lo, py, hi-lo, 1}, color);
				c.fillRect({lo-1, py, 1, 1}, color.applyAlpha(int((lo-x1)*255)));
				c.fillRect({hi, py, 1, 1}, color.applyAlpha(int((x2-hi)*255)));
			}
		}
	};
	paint(outline, p.muted);
	// Four short pads wrapping the phone edge describe a grip, while the
	// longer diagonal pad describes the thumb rather than a pointing finger.
	rounded(36, 77 - handOffset + shift, 20, 13, 6, p.muted);
	rounded(33, 94 - handOffset + shift, 21, 13, 6, p.muted);
	rounded(31, 111 - handOffset + shift, 20, 12, 6, p.muted);
	rounded(32, 128 - handOffset + shift, 16, 11, 5, p.muted);
	auto crease = [&](P a, P b, P end, GAGCore::Color color, double weight = .8) {
		std::vector<P> upper, lower;
		for (int i = 0; i <= 32; ++i) {
			const double t = i / 32., u = 1-t;
			const P at{u*u*a.x+2*u*t*b.x+t*t*end.x, u*u*a.y+2*u*t*b.y+t*t*end.y};
			const P tangent{2*u*(b.x-a.x)+2*t*(end.x-b.x), 2*u*(b.y-a.y)+2*t*(end.y-b.y)};
			const double length = std::max(.001, std::hypot(tangent.x, tangent.y));
			const P normal{-tangent.y / length * weight / 2, tangent.x / length * weight / 2};
			upper.push_back({at.x+normal.x, at.y+normal.y});
			lower.push_back({at.x-normal.x, at.y-normal.y});
		}
		upper.insert(upper.end(), lower.rbegin(), lower.rend());
		paint(upper, color);
	};
	crease({39, 91}, {44, 94}, {50, 92}, p.line.applyAlpha(130));
	crease({36, 108}, {42, 111}, {49, 108}, p.line.applyAlpha(130));
	crease({35, 125}, {40, 128}, {46, 126}, p.line.applyAlpha(130));
	crease({42, 132}, {31, 145}, {42, 161}, p.line.applyAlpha(110));
	crease({34, 174}, {45, 177}, {56, 174}, p.line.applyAlpha(100));
	std::vector<P> nail;
	for (int i = 0; i < 48; ++i) {
		const double t = i * 6.28318530718 / 48.;
		const double x = 6.5 * std::cos(t), y = 3.8 * std::sin(t);
		nail.push_back({84 + .75*x + .66*y, 96 - .66*x + .75*y});
	}
	paint(nail, p.line);

	c.popClip();
}

}

void HandPreferenceScreen::choose(int side)
{
	if (side != Settings::THUMB_LEFT && side != Settings::THUMB_RIGHT) return;
	auto &settings = globalContainer->settings;
	const int previous = settings.thumbSide;
	const bool previouslyChosen = settings.thumbSideChosen;
	settings.thumbSide = side;
	settings.thumbSideChosen = true;
	if (!settings.save())
	{
		settings.thumbSide = previous;
		settings.thumbSideChosen = previouslyChosen;
		saveFailed = true;
		invalidate();
		return;
	}
	endExecute(side);
}

Glob2UI::Element HandPreferenceScreen::build(const Glob2UI::Presentation &p)
{
	using namespace Glob2UI;
	std::vector<Element> body;
	body.push_back(paragraph(tr("[hand preference explanation]"), {FontRole::Body, false, TextAlign::Center}));
	const int pictureHeight = p.landscape() ? 126 : p.safe.h < p.pt(700) ? 200 : 290;
	// Transparent picture buttons own input; the canvas paints a mirrored
	// one-handed grip for each choice. Canvas nodes never intercept taps.
	body.push_back(stack({
		row({expanded(button("hand.grip.left", "", [this] { choose(Settings::THUMB_LEFT); },
			{.flat = true, .minHeight = double(pictureHeight), .accessibleLabel = tr("[Left hand]")})),
			expanded(button("hand.grip.right", "", [this] { choose(Settings::THUMB_RIGHT); },
			{.flat = true, .minHeight = double(pictureHeight), .accessibleLabel = tr("[Right hand]")}))}, {0}),
		canvas("hand.preview", {p.pt(340), p.pt(pictureHeight)},
			[](Canvas &c, Rect r, const Frame &f) {
				const int gap = std::max(4, r.w / 40);
				const int width = (r.w - gap) / 2;
				grip(c, {r.x, r.y, width, r.h}, true,
					f.pressed("hand.grip.left") || f.pressed("hand.left"), f.focused("hand.grip.left"));
				grip(c, {r.x + width + gap, r.y, width, r.h}, false,
					f.pressed("hand.grip.right") || f.pressed("hand.right"), f.focused("hand.grip.right"));
			}, {.accessibleText = tr("[hand silhouettes description]")})}));

	body.push_back(hint(tr("[hand preference change later]")));
	if (saveFailed) body.push_back(paragraph(tr("[hand preference save failed]")));
	return page(tr("[Which hand holds your phone?]"), scroll("hand.body", column(std::move(body))),
		row({expanded(button("hand.left", tr("[Left hand]"), [this] { choose(Settings::THUMB_LEFT); })),
			expanded(button("hand.right", tr("[Right hand]"), [this] { choose(Settings::THUMB_RIGHT); }))},
			{p.pt(8)}), p, 520);
}
