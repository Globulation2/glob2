// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#include "MainMenuScreen.h"
#include "GlobalContainer.h"
#include <algorithm>
#include <cmath>

#ifdef HAVE_CONFIG_H
#include <glob2/BuildConfig.h>
#endif
#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "Globulation 2"
#endif

using namespace Glob2UI;

MainMenuScreen::MainMenuScreen() = default;
MainMenuScreen::~MainMenuScreen() = default;

// Fit the wordmark once with the software blitter; the text heading remains
// the fallback when the asset is missing.
void MainMenuScreen::loadWordmark(int logoWidth)
{
	if (wordmark && wordmarkWidth == logoWidth)
		return;
	wordmark.reset();
	wordmarkWidth = logoWidth;
	GAGCore::DrawableSurface source(1, 1);
	if (!source.loadImage("data/gfx/menu-wordmark.png"))
		return;
	SDL_Rect crop{76, 232, 1956, 284};
	const int logoHeight = std::max(1, logoWidth * crop.h / crop.w);
	auto *fitted = SDL_CreateRGBSurfaceWithFormat(0, logoWidth, logoHeight, 32, SDL_PIXELFORMAT_RGBA32);
	if (!fitted)
		return;
	if (SDL_BlitScaled(source.getSDLSurface(), &crop, fitted, nullptr) == 0)
	{
		// The source asset has a pale matte. Recover coverage for its two
		// flat inks before compositing, including the letter openings.
		for (int row = 0; row < logoHeight; ++row)
		{
			auto *pixels = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(fitted->pixels) + row * fitted->pitch);
			for (int col = 0; col < logoWidth; ++col)
			{
				Uint8 red, green, blue, alpha;
				SDL_GetRGBA(pixels[col], fitted->format, &red, &green, &blue, &alpha);
				const bool goldInk = red > green;
				double coverage = goldInk ? (int(green) - int(blue) - 20) / 65.0 : (232 - int(red)) / 200.0;
				coverage = coverage < 0.03 ? 0.0 : std::min(1.0, coverage);
				pixels[col] = SDL_MapRGBA(fitted->format, goldInk ? 227 : 36, goldInk ? 192 : 69,
										  goldInk ? 119 : 49, static_cast<Uint8>(std::lround(255 * coverage)));
			}
		}
		SDL_SetSurfaceBlendMode(fitted, SDL_BLENDMODE_BLEND);
		wordmark = std::make_unique<GAGCore::DrawableSurface>(fitted);
	}
	SDL_FreeSurface(fitted);
}

Element MainMenuScreen::build(const Presentation &p)
{
	auto choose = [this](int code) { return [this, code] { endExecute(code); }; };
	auto action = [&](const char *key, int code, ButtonOptions options)
	{ return button("menu/" + std::to_string(code), tr(key), choose(code), options); };
	CardOptions cardOptions;
	cardOptions.color = theme().palette.panel.applyAlpha(248);
	cardOptions.radius = p.pt(10);
	if (p.touch)
	{
		// Phones: launch choices prominent, utilities one level away behind More,
		// settings beside the wordmark.
		const int w = std::min(p.pt(440), p.safe.w - p.pt(24));
		const int panelHeight = std::min(p.safe.h - p.pt(24), p.pt(more ? 596 : 420));
		loadWordmark(std::max(64, w - p.pt(140)));
		ButtonOptions gear;
		gear.role = FontRole::Body;
		gear.minHeight = 48;
		auto header = row({expanded(wordmark ? image(wordmark.get(), {false}) : title("Globulation 2")),
						   button("menu/settings", tr("[settings]"), choose(GAME_SETUP), gear)},
						  {p.pt(8), CrossAlign::Center});
		ButtonOptions rowStyle;
		rowStyle.alignLeft = true;
		rowStyle.minHeight = 56;
		ButtonOptions primaryStyle = rowStyle;
		primaryStyle.primary = true;
		ButtonOptions small;
		small.minHeight = 48;
		std::vector<Element> content;
		if (!more)
		{
			content.push_back(action("[custom game]", CUSTOM, primaryStyle));
			const bool landscape = p.landscape() && p.safe.h < p.pt(480);
			if (landscape)
			{
				// Keep every top-level destination visible on short phones.
				WrapOptions grid;
				grid.maxColumns = 2;
				grid.minChildWidth = 1;
				content.push_back(wrap({action("[campaign]", CAMPAIGN, small), action("[load game]", LOAD_GAME, small),
										action("[tutorial]", TUTORIAL, small),
										button("menu/more", tr("[More]"), [this] { showMore(true); }, small)},
									   grid));
			}
			else
			{
				content.push_back(spacer(p.pt(4)));
				content.push_back(action("[campaign]", CAMPAIGN, rowStyle));
				content.push_back(action("[load game]", LOAD_GAME, rowStyle));
				content.push_back(action("[tutorial]", TUTORIAL, rowStyle));
				content.push_back(spacer(p.pt(4)));
				content.push_back(button("menu/more", tr("[More]"), [this] { showMore(true); }, small));
			}
		}
		else
		{
			ButtonOptions back = small;
			back.shortcut = SDLK_ESCAPE;
			content.push_back(button("menu/back", tr("[Back]"), [this] { showMore(false); }, back));
			content.push_back(action("[yog]", MULTIPLAYERS_YOG, rowStyle));
#ifndef __EMSCRIPTEN__
			content.push_back(action("[lan]", MULTIPLAYERS_LAN, rowStyle));
#endif
			content.push_back(action("[editor]", EDITOR, rowStyle));
			content.push_back(action("[credits]", CREDITS, rowStyle));
#if !defined(__EMSCRIPTEN__) && !defined(GLOB2_MOBILE)
			content.push_back(action("[quit]", QUIT, rowStyle));
#endif
		}
		cardOptions.padding = p.pt(12);
		auto body = scroll("menu/scroll", column(std::move(content), {p.pt(8)}));
		return center(sized({w, panelHeight}, card(column({header, expanded(body)}, {p.pt(12)}), cardOptions)));
	}
	// Desktop: the tall panel at the left over the live colony.
	const int panelW = p.pt(p.safe.h < p.pt(600) ? 312 : 368);
	const int panelH = std::min(p.safe.h - p.pt(40), p.pt(620));
	loadWordmark(std::max(64, panelW - 2 * p.pt(24)));
	ButtonOptions launch;
	launch.role = FontRole::Heading;
	launch.minHeight = 42;
	ButtonOptions primary = launch;
	primary.primary = true;
	primary.minHeight = 48;
	primary.shortcut = SDLK_RETURN;
	ButtonOptions utility;
	utility.role = FontRole::Body;
	utility.minHeight = 32;
	std::vector<Element> content;
	content.push_back(canvas("", {p.pt(36), p.pt(4)},
							 [](Canvas &c, Rect r, const Frame &frame) { c.fillRounded(r, r.h / 2, frame.layout.theme.palette.accent); }));
	content.push_back(spacer(p.pt(4)));
	if (wordmark)
		content.push_back(image(wordmark.get(), {true}));
	else
		content.push_back(title("Globulation 2"));
	content.push_back(spacer(p.pt(16)));
	content.push_back(action("[custom game]", CUSTOM, primary));
	content.push_back(action("[campaign]", CAMPAIGN, launch));
	content.push_back(action("[load game]", LOAD_GAME, launch));
	content.push_back(action("[tutorial]", TUTORIAL, launch));
	content.push_back(spacer(p.pt(12)));
	content.push_back(action("[yog]", MULTIPLAYERS_YOG, utility));
#ifndef __EMSCRIPTEN__
	content.push_back(action("[lan]", MULTIPLAYERS_LAN, utility));
#endif
	content.push_back(spacer(p.pt(12)));
	std::vector<Element> utilities{action("[settings]", GAME_SETUP, utility), action("[editor]", EDITOR, utility),
								   action("[credits]", CREDITS, utility)};
#if !defined(__EMSCRIPTEN__) && !defined(GLOB2_MOBILE)
	utilities.push_back(action("[quit]", QUIT, utility));
#endif
	WrapOptions grid;
	grid.maxColumns = 2;
	grid.minChildWidth = 1;
	content.push_back(wrap(std::move(utilities), grid));
	cardOptions.padding = p.pt(24);
	auto panel = sized({panelW, panelH},
					   card(column({expanded(scroll("menu/scroll", column(std::move(content), {p.pt(6)}))), caption(PACKAGE_VERSION)}, {p.pt(6)}),
							cardOptions));
	const int margin = std::clamp(p.safe.w / 20, p.pt(20), p.pt(72));
	return padding({margin, 0, 0, 0}, align(Alignment::Left, panel));
}

void MainMenuScreen::showMore(bool value)
{
	more = value;
	invalidate();
}
