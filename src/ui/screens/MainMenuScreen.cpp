// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#include "MainMenuScreen.h"
#include "GlobalContainer.h"
#include "ui/ThemeCatalog.h"
#include <algorithm>
#include <cmath>
#include <ApplicationHost.h>

#ifdef HAVE_CONFIG_H
#include <glob2/BuildConfig.h>
#endif
#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "Globulation 2"
#endif

using namespace Glob2UI;

MainMenuScreen::MainMenuScreen() = default;
MainMenuScreen::~MainMenuScreen() = default;

// Fit the wordmark once per width and theme with the software blitter; the
// text heading remains the fallback when the asset is missing.
void MainMenuScreen::loadWordmark(int logoWidth)
{
	if (wordmark && wordmarkWidth == logoWidth && wordmarkTheme == themeGeneration())
		return;
	wordmark.reset();
	wordmarkWidth = logoWidth;
	wordmarkTheme = themeGeneration();
	const auto &own = theme().backdrop.wordmark;
	GAGCore::DrawableSurface source(1, 1);
	if (!source.loadImage(own.empty() ? "data/gfx/menu-wordmark.webp" : own))
		return;
	// A theme's own wordmark is shown whole and as drawn.
	SDL_Rect crop = own.empty() ? SDL_Rect{76, 232, 1956, 284} : SDL_Rect{0, 0, source.getW(), source.getH()};
	const int logoHeight = std::max(1, logoWidth * crop.h / crop.w);
	auto *fitted = SDL_CreateSurface(logoWidth, logoHeight, SDL_PIXELFORMAT_RGBA32);
	if (!fitted)
		return;
	if (!SDL_BlitSurfaceScaled(source.getSDLSurface(), &crop, fitted, nullptr, own.empty() ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR))
	{
		SDL_DestroySurface(fitted);
		return;
	}
	if (own.empty())
	{
		// The source asset has a pale matte. Recover coverage for its two
		// flat inks before compositing, including the letter openings, and
		// paint them in the theme's ink and accent.
		const auto &palette = theme().palette;
		for (int row = 0; row < logoHeight; ++row)
		{
			auto *pixels = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(fitted->pixels) + row * fitted->pitch);
			for (int col = 0; col < logoWidth; ++col)
			{
				Uint8 red, green, blue, alpha;
				SDL_GetRGBA(pixels[col], SDL_GetPixelFormatDetails(fitted->format), SDL_GetSurfacePalette(fitted), &red, &green, &blue, &alpha);
				const bool goldInk = red > green;
				double coverage = goldInk ? (int(green) - int(blue) - 20) / 65.0 : (232 - int(red)) / 200.0;
				coverage = coverage < 0.03 ? 0.0 : std::min(1.0, coverage);
				const auto &ink = goldInk ? palette.accent : palette.ink;
				pixels[col] = SDL_MapSurfaceRGBA(fitted, ink.r, ink.g, ink.b, static_cast<Uint8>(std::lround(255 * coverage)));
			}
		}
	}
	SDL_SetSurfaceBlendMode(fitted, SDL_BLENDMODE_BLEND);
	wordmark = std::make_unique<GAGCore::DrawableSurface>(fitted);
	SDL_DestroySurface(fitted);
}

Element MainMenuScreen::build(const Presentation &p)
{
	auto choose = [this](int code) { return [this, code] { endExecute(code); }; };
	auto action = [&](const char *key, int code, ButtonOptions options)
	{
		// Every destination carries its icon on every host.
		switch (code)
		{
		case CUSTOM:
			options.icon = uiIcon(UIIcon::CustomGame);
			break;
		case CAMPAIGN:
			options.icon = uiIcon(UIIcon::Campaign);
			break;
		case LOAD_GAME:
			options.icon = uiIcon(UIIcon::LoadGame);
			break;
		case TUTORIAL:
			options.icon = uiIcon(UIIcon::Tutorial);
			break;
		case PLAY_ONLINE:
			options.icon = uiIcon(UIIcon::Online);
			break;
		case MULTIPLAYERS_LAN:
			options.icon = uiIcon(UIIcon::LAN);
			break;
		case GAME_SETUP:
			options.icon = uiIcon(UIIcon::Settings);
			break;
		case EDITOR:
			options.icon = uiIcon(UIIcon::Editor);
			break;
		case CREDITS:
			options.icon = uiIcon(UIIcon::Credits);
			break;
		case QUIT:
			options.icon = uiIcon(UIIcon::Quit);
			break;
		default:
			break;
		}
		return button("menu/" + std::to_string(code), tr(key), choose(code), options);
	};
	CardOptions cardOptions;
	cardOptions.color = theme().palette.panel.applyAlpha(theme().palette.panel.a * 248 / 255);
	cardOptions.radius = p.pt(10);
	if (p.touch)
	{
		// Phones: launch choices prominent, utilities one level away behind More,
		// settings beside the wordmark.
		const int w = std::min(p.pt(440), p.safe.w - p.pt(24));
		const int panelHeight = std::min(p.safe.h - p.pt(24), p.pt(more ? 596 : 420));
		loadWordmark(std::max(64, w - p.pt(140)));
		auto header =
			row({expanded(wordmark ? image(wordmark.get(), {true}) : title("Globulation 2")),
				 compactButton("menu/settings", tr("[settings]"), UIIcon::Settings,
							   choose(GAME_SETUP), p)},
				{p.pt(8), CrossAlign::Center});
		ButtonOptions rowStyle;
		rowStyle.alignLeft = true;
		rowStyle.minHeight = 56;
		ButtonOptions primaryStyle = rowStyle;
		primaryStyle.primary = true;
		ButtonOptions small;
		small.minHeight = 48;
		ButtonOptions moreStyle = small;
		moreStyle.icon = uiIcon(UIIcon::More);
		std::vector<Element> content;
		if (!more)
		{
			content.push_back(action("[custom game]", CUSTOM, primaryStyle));
#if !defined(GLOB2_CHINA_RELEASE) && !defined(GLOB2_AMAZON_RELEASE)
			// Play online sits on the main card, under Custom game; LAN stays under More.
			content.push_back(action("[play online]", PLAY_ONLINE, rowStyle));
#endif
			const bool landscape = p.landscape() && p.safe.h < p.pt(480);
			if (landscape)
			{
				// Keep every top-level destination visible on short phones.
				WrapOptions grid;
				grid.maxColumns = 2;
				grid.minChildWidth = 1;
				content.push_back(wrap(
					{action("[campaign]", CAMPAIGN, small), action("[load game]", LOAD_GAME, small),
					 action("[tutorial]", TUTORIAL, small),
					 button(
						 "menu/more", tr("[More]"), [this] { showMore(true); }, moreStyle)},
					grid));
			}
			else
			{
				content.push_back(spacer(p.pt(4)));
				content.push_back(action("[campaign]", CAMPAIGN, rowStyle));
				content.push_back(action("[load game]", LOAD_GAME, rowStyle));
				content.push_back(action("[tutorial]", TUTORIAL, rowStyle));
				content.push_back(spacer(p.pt(4)));
				content.push_back(
					button("menu/more", tr("[More]"), [this] { showMore(true); }, moreStyle));
			}
		}
		else
		{
			ButtonOptions back = small;
			back.shortcut = SDLK_ESCAPE;
			back.icon = uiIcon(UIIcon::Back);
			content.push_back(button("menu/back", tr("[Back]"), [this] { showMore(false); }, back));
#ifndef __EMSCRIPTEN__
			content.push_back(action("[lan]", MULTIPLAYERS_LAN, rowStyle));
#endif
			content.push_back(action("[editor]", EDITOR, rowStyle));
			content.push_back(action("[credits]", CREDITS, rowStyle));
#if defined(GLOB2_MOBILE) && defined(__ANDROID__)
			content.push_back(button("menu/privacy", "Privacy policy", [] {
#if defined(GLOB2_AMAZON_RELEASE)
				GAGCore::ApplicationHost::openUrl("https://glob2online.com/privacy/#fire-tablet-edition");
#else
				GAGCore::ApplicationHost::openUrl("https://glob2online.com/privacy/");
#endif
			}, rowStyle));
#endif
#if !defined(__EMSCRIPTEN__) && !defined(GLOB2_MOBILE)
			content.push_back(action("[quit]", QUIT, rowStyle));
#endif
		}
		cardOptions.padding = p.pt(12);
		auto body = scroll("menu/scroll", column(std::move(content), {p.pt(8)}));
		return center(sized({w, panelHeight}, card(column({header, expanded(body)}, {p.pt(12)}), cardOptions)));
	}
	// Desktop: the tall panel at the left over the live colony. Short windows
	// keep every action in view with the smaller sizes the menu always used.
	const bool compact = p.safe.h < p.pt(640);
	const int panelW = p.pt(compact ? 312 : 368);
	const int panelH = std::min(p.safe.h - p.pt(compact ? 16 : 40), p.pt(620));
	loadWordmark(std::max(64, panelW - 2 * p.pt(24)));
	ButtonOptions launch;
	launch.role = compact ? FontRole::Body : FontRole::Heading;
	launch.minHeight = compact ? 30 : 42;
	ButtonOptions primary = launch;
	primary.primary = true;
	primary.minHeight = compact ? 38 : 48;
	primary.shortcut = SDLK_RETURN;
	ButtonOptions utility;
	utility.role = FontRole::Body;
	utility.minHeight = compact ? 28 : 32;
	std::vector<Element> content;
	content.push_back(canvas("", {p.pt(36), p.pt(4)},
							 [](Canvas &c, Rect r, const Frame &frame) { c.fillRounded(r, r.h / 2, frame.layout.theme.palette.accent); }));
	content.push_back(spacer(p.pt(4)));
	if (wordmark)
		content.push_back(image(wordmark.get(), {true}));
	else
		content.push_back(title("Globulation 2"));
	content.push_back(spacer(p.pt(compact ? 8 : 16)));
	content.push_back(action("[custom game]", CUSTOM, primary));
	// Play online is a way to play like the others, under Custom game as on
	// phones, not a small extra below them.
#if !defined(GLOB2_CHINA_RELEASE) && !defined(GLOB2_AMAZON_RELEASE)
	content.push_back(action("[play online]", PLAY_ONLINE, launch));
#endif
	content.push_back(action("[campaign]", CAMPAIGN, launch));
	content.push_back(action("[load game]", LOAD_GAME, launch));
	content.push_back(action("[tutorial]", TUTORIAL, launch));
	content.push_back(spacer(p.pt(compact ? 6 : 12)));
#ifndef __EMSCRIPTEN__
	content.push_back(action("[lan]", MULTIPLAYERS_LAN, utility));
#endif
	content.push_back(spacer(p.pt(compact ? 6 : 12)));
	std::vector<Element> utilities{action("[settings]", GAME_SETUP, utility), action("[editor]", EDITOR, utility),
								   action("[credits]", CREDITS, utility), action("[quit]", QUIT, utility)};
#if defined(GLOB2_MOBILE) && defined(__ANDROID__)
	utilities.push_back(button("menu/privacy", "Privacy policy", [] {
#if defined(GLOB2_AMAZON_RELEASE)
		GAGCore::ApplicationHost::openUrl("https://glob2online.com/privacy/#fire-tablet-edition");
#else
		GAGCore::ApplicationHost::openUrl("https://glob2online.com/privacy/");
#endif
	}, utility));
#endif
	WrapOptions grid;
	grid.maxColumns = 2;
	grid.minChildWidth = 1;
	content.push_back(wrap(std::move(utilities), grid));
	cardOptions.padding = p.pt(compact ? 16 : 24);
	auto panel = sized({panelW, panelH},
					   card(column({expanded(scroll("menu/scroll", column(std::move(content), {p.pt(compact ? 4 : 6)}))), caption(PACKAGE_VERSION)}, {p.pt(6)}),
							cardOptions));
	const int margin = std::clamp(p.safe.w / 20, p.pt(20), p.pt(72));
	return padding({margin, 0, 0, 0}, align(Alignment::Left, panel));
}

void MainMenuScreen::showMore(bool value)
{
	more = value;
	invalidate();
}
