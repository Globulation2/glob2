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
	const bool compact = p.compact() || p.heightClass() == SizeClass::Compact;
	const int panelWidth = std::min(p.safe.w - 2 * p.pt(12), p.pt(compact ? 340 : 400));
	loadWordmark(std::max(64, panelWidth - 2 * p.pt(24)));

	auto choose = [this](int code) { return [this, code] { endExecute(code); }; };
	auto launch = [&](const char *key, int code, bool primary = false)
	{
		ButtonOptions options;
		options.primary = primary;
		options.role = FontRole::Heading;
		options.minHeight = primary ? 48 : 42;
		if (primary)
			options.shortcut = SDLK_RETURN;
		return button("menu/" + std::to_string(code), tr(key), choose(code), options);
	};
	auto utility = [&](const char *key, int code)
	{
		ButtonOptions options;
		options.role = FontRole::Body;
		options.minHeight = compact ? 40 : 34;
		return button("menu/" + std::to_string(code), tr(key), choose(code), options);
	};

	std::vector<Element> utilities{utility("[settings]", GAME_SETUP), utility("[editor]", EDITOR),
								   utility("[credits]", CREDITS)};
#if !defined(__EMSCRIPTEN__) && !defined(GLOB2_MOBILE)
	utilities.push_back(utility("[quit]", QUIT));
#endif
	std::vector<Element> network{utility("[yog]", MULTIPLAYERS_YOG)};
#ifndef __EMSCRIPTEN__
	network.push_back(utility("[lan]", MULTIPLAYERS_LAN));
#endif

	std::vector<Element> content;
	if (wordmark)
		content.push_back(image(wordmark.get(), {true}));
	else
		content.push_back(title("Globulation 2"));
	content.push_back(spacer(p.pt(compact ? 8 : 16)));
	content.push_back(launch("[custom game]", CUSTOM, true));
	content.push_back(launch("[campaign]", CAMPAIGN));
	content.push_back(launch("[load game]", LOAD_GAME));
	content.push_back(launch("[tutorial]", TUTORIAL));
	content.push_back(spacer(p.pt(4)));
	content.push_back(wrap(std::move(network), {-1, p.pt(120)}));
	content.push_back(spacer(p.pt(4)));
	content.push_back(wrap(std::move(utilities), {-1, p.pt(120)}));
	content.push_back(spacer(p.pt(4)));
	content.push_back(caption(PACKAGE_VERSION));

	auto body = scroll("menu/scroll", column(std::move(content), {p.pt(6)}));
	CardOptions cardOptions;
	cardOptions.color = GAGCore::Color(230, 231, 210, 248);
	cardOptions.radius = p.pt(10);
	auto panel = maxWidth(panelWidth, card(body, cardOptions));
	if (p.compact())
		return center(panel);
	// Desktop: the panel sits at the left over the live colony.
	const int margin = std::clamp(p.safe.w / 20, p.pt(20), p.pt(72));
	return padding({margin, p.pt(20), 0, p.pt(20)}, align(Alignment::Left, panel));
}
