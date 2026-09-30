// SPDX-License-Identifier: GPL-3.0-or-later
#include "TouchReadout.h"
#include "InGameTouchTheme.h"
#include "GlobalContainer.h"
#include <algorithm>
using namespace GAGCore;

namespace TouchReadout
{
ViewRect bounds(ViewPoint contact, const std::string &text, ViewRect within)
{
	auto *font = globalContainer->standardFont;
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	const double w = std::min(within.w, std::max(InGameTouchTheme::readoutHeight * 1.6,
										  font->getStringWidth(text) * InGameTouchTheme::readoutTextScale + 24) * unit);
	const double h = InGameTouchTheme::readoutHeight * unit, lift = InGameTouchTheme::readoutLift * unit;
	ViewRect r{contact.x - w / 2, contact.y - lift - h, w, h};
	if (r.y < within.y)
		r.y = contact.y + lift; // Near the top edge the thumb comes from below.
	r.x = std::clamp(r.x, within.x, std::max(within.x, within.x + within.w - w));
	r.y = std::clamp(r.y, within.y, std::max(within.y, within.y + within.h - h));
	return r;
}
void draw(ViewPoint contact, const std::string &text, ViewRect within)
{
	auto *gfx = globalContainer->gfx;
	auto *font = globalContainer->standardFont;
	const double unit = gfx->logicalUnitsPerPoint();
	const auto r = bounds(contact, text, within);
	gfx->setClipRect();
	gfx->drawFilledRect(int(r.x), int(r.y), int(r.w), int(r.h), InGameTouchTheme::readout);
	for (int i = 0; i < std::max(1, int(2 * unit)); ++i)
		gfx->drawRect(int(r.x) + i, int(r.y) + i, int(r.w) - 2 * i, int(r.h) - 2 * i, InGameTouchTheme::border);
	InGameTouchTheme::TextStyle style(font);
	const double scale = InGameTouchTheme::readoutTextScale * unit;
	const double textW = font->getStringWidth(text) * scale, textH = font->getStringHeight(text) * scale;
	SDL_Rect clip{int(r.x), int(r.y), int(r.w), int(r.h)};
	gfx->setUITransform(scale, r.x + std::max(0.0, (r.w - textW) / 2), r.y + (r.h - textH) / 2, &clip);
	gfx->drawString(0, 0, font, text);
	gfx->setUITransform();
	gfx->setClipRect();
}
} // namespace TouchReadout
