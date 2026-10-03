// SPDX-License-Identifier: GPL-3.0-or-later
#include "FrontendTheme.h"
#include "ui/FrontendUI.h"
#include "MenuColony.h"
#include "GlobalContainer.h"
#include <Toolkit.h>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace GAGCore;
using namespace GAGGUI;
FrontendTheme *FrontendTheme::current = nullptr;
bool FrontendTheme::allowed = true;
namespace
{
const char *fontNames[] = {"menu", "standard", "little"};
// Live scopes in creation order.
std::vector<const FrontendScope *> liveScopes;
} // namespace

FrontendTheme::FrontendTheme() : original(Style::style)
{
	current = this;
	colony = std::make_unique<MenuColony>();
	const auto &palette = Glob2UI::frontendTheme().palette;
	textColor = palette.ink;
	highlightColor = palette.hover;
	frameColor = palette.line;
	backColor = palette.panel;
	for (int i = 0; i < 3; ++i)
		originalFonts[i] = Toolkit::getFont(fontNames[i])->getStyle();
	fallback = std::make_unique<DrawableSurface>(1, 1);
	if (!fallback->loadImage("data/gfx/menu-colony.png"))
		fallback.reset();
}
FrontendTheme::~FrontendTheme()
{
	current = nullptr;
}

FrontendScope::FrontendScope(bool enabled) : enabled(enabled)
{
	liveScopes.push_back(this);
	apply();
	if (!enabled && FrontendTheme::current)
		FrontendTheme::current->colony->pause();
}
FrontendScope::~FrontendScope()
{
	liveScopes.erase(std::find(liveScopes.begin(), liveScopes.end(), this));
	apply();
	if (FrontendTheme::current)
		FrontendTheme::current->colony->pause();
}
void FrontendScope::apply()
{
	// With no live scope, restore the presentation the theme was created over, so
	// teardown never finds Style::style pointing at a destroyed theme.
	const bool anyScope = !liveScopes.empty();
	FrontendTheme::allowed = !anyScope || liveScopes.back()->enabled;
	if (!FrontendTheme::current)
		return;
	auto &theme = *FrontendTheme::current;
	const bool themed = anyScope && liveScopes.back()->enabled;
	Style::style = themed ? &theme : theme.original;
	for (int i = 0; i < 3; ++i)
		Toolkit::getFont(fontNames[i])
			->setStyle(themed ? Font::Style(Font::STYLE_NORMAL, theme.textColor)
							  : theme.originalFonts[i]);
}

void FrontendTheme::rounded(DrawableSurface *s, int x, int y, int w, int h, int r, Color c)
{
	if (w <= 0 || h <= 0)
		return;
	r = std::min({r, w / 2, h / 2});
	s->drawFilledRect(x, y + r, w, h - 2 * r, c);
	for (int row = 0; row < r; ++row)
	{
		const float dy = r - row - 0.5f;
		const int inset = r - int(std::sqrt(r * r - dy * dy));
		s->drawFilledRect(x + inset, y + row, w - 2 * inset, 1, c);
		s->drawFilledRect(x + inset, y + h - row - 1, w - 2 * inset, 1, c);
	}
}
void FrontendTheme::onFrame()
{
	if (!painted)
		return; // Present the still before doing any loading work.
	// The colony draws with the game sprites, which the browser installs after
	// the main menu; the still image stands in until then.
	if (!attempted && globalContainer->ensureGameGraphics())
	{
		attempted = true;
		colony->load();
	}
	// An unfocused visible menu still animates (including the gallery host).
	SDL_Window *window = SDL_GetWindowFromID(globalContainer->gfx->windowID());
	const bool visible = window && !(SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED);
	colony->update(SDL_GetTicks(), visible);
}
void FrontendTheme::background(DrawableSurface *s, bool panel, const SDL_Rect *content)
{
	const int w = s->getW(), h = s->getH();
	const auto &palette = Glob2UI::frontendTheme().palette;
	s->drawFilledRect(0, 0, w, h, palette.backdrop);
	if (colony->ready())
		colony->draw(w, h);
	else if (fallback)
	{
		// Cache the cropped fallback at the logical viewport size.
		if (!fittedFallback || fittedFallback->getW() != w || fittedFallback->getH() != h)
		{
			const double scale =
				std::max(double(w) / fallback->getW(), double(h) / fallback->getH());
			SDL_Rect dest{0, 0, int(std::ceil(fallback->getW() * scale)),
						  int(std::ceil(fallback->getH() * scale))};
			dest.x = (w - dest.w) / 2;
			dest.y = (h - dest.h) / 2;
			auto *fitted = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
			if (fitted && SDL_BlitSurfaceScaled(fallback->getSDLSurface(), nullptr, fitted, &dest, SDL_SCALEMODE_NEAREST))
				fittedFallback = std::make_unique<DrawableSurface>(fitted);
			SDL_DestroySurface(fitted);
		}
		if (fittedFallback)
			s->drawSurface(0, 0, fittedFallback.get());
	}
	s->drawFilledRect(0, 0, w, h, palette.paper.applyAlpha(42));
	if (panel)
	{
		const SDL_Rect area = content ? *content : SDL_Rect{(w - 640) / 2, (h - 480) / 2, 640, 480};
		const int x = std::max(0, area.x - 12), y = std::max(0, area.y - 12);
		const int pw = std::min(w, area.x + area.w + 12) - x,
				  ph = std::min(h, area.y + area.h + 12) - y;
		rounded(s, x + 2, y + 3, pw, ph, 10, palette.shadow);
		rounded(s, x, y, pw, ph, 10, palette.panel.applyAlpha(248));
	}
	painted = true;
}
void FrontendTheme::drawTextButtonBackground(DrawableSurface *s, int x, int y, int w, int h,
											 unsigned hi)
{
	const auto &palette = Glob2UI::frontendTheme().palette;
	rounded(s, x, y, w, h, 4, palette.field);
	if (hi)
		rounded(s, x, y, w, h, 4, palette.hover.applyAlpha(hi / 2));
}
void FrontendTheme::drawFrame(DrawableSurface *s, int x, int y, int w, int h, unsigned hi)
{
	// Frames are also drawn AFTER list contents; never erase their interior.
	s->drawRect(x, y, w, h, hi ? highlightColor : frameColor);
}
