// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/FrontendUI.h"
inline std::string musicText(const std::string &text)
{
	const auto key = "[music " + text + "]";
	const auto translated = Glob2UI::tr(key);
	return translated == key ? text : translated;
}

inline Glob2UI::Element musicPlaceholder(int pixels)
{
	using namespace Glob2UI;
	return canvas(
		"music.placeholder", {pixels, pixels},
		[](Canvas &c, Rect r, const Frame &)
		{
			int side = std::min(r.w, r.h);
			Rect box{r.x + (r.w - side) / 2, r.y + (r.h - side) / 2, side, side};
			c.fillRounded(box, 8, GAGCore::Color(52, 91, 70));
			auto ink = GAGCore::Color(248, 230, 172);
			int x = box.x + side / 3, y = box.y + side / 3;
			c.line({x, y + side / 3}, {x, y}, ink);
			c.line({x, y}, {x + side / 3, y - side / 12}, ink);
			c.line({x + side / 3, y - side / 12}, {x + side / 3, y + side / 4}, ink);
			c.fillRounded({x - side / 12, y + side / 3, side / 8, side / 12}, side / 24, ink);
			c.fillRounded({x + side / 4, y + side / 4, side / 8, side / 12}, side / 24, ink);
		},
		{.keepAspect = true});
}
