// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "MainMenuScreen.h"
#include "FrontendTheme.h"
#include "GlobalContainer.h"
#include <GUIButton.h>
#include <Toolkit.h>
#include <StringTable.h>
#include <algorithm>
#include <cmath>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "Globulation 2"
#endif
#ifndef PRIMARY_FONT
#define PRIMARY_FONT "sans.ttf"
#endif

using namespace GAGGUI;
using namespace GAGCore;

namespace
{
using namespace FrontendPalette;
const auto fillRounded = FrontendTheme::rounded;
}

// Front-page layout and primary-action emphasis; shared drawing comes from the theme.
class MainMenuButton : public TextButton
{
public:
	bool focused = false;
	bool primary;
	bool pressed = false;

	MainMenuButton(int x, int y, int w, int h, const char* font,
		const char* key, int action, bool primary = false)
		: TextButton(x, y, w, h, ALIGN_LEFT, ALIGN_TOP, font,
			Toolkit::getStringTable()->getString(key), action), primary(primary) {}

	void paint() override
	{
		int x, y, w, h;
		getScreenPos(&x, &y, &w, &h);
		auto* target = parent->getSurface();
		const unsigned hover = focused ? 255 : getNextHighlightValue();
		const int radius = primary ? 8 : 4;
		if (focused) fillRounded(target, x - 4, y - 4, w + 8, h + 8, radius + 4, violet);
		if (primary)
		{
			FrontendTheme::blob(target, x, y, w, h, radius, pressed ? goldPressed : gold, ink, 1);
			if (hover) fillRounded(target, x + 2, y + 2, w - 4, h - 4, radius - 1, Color(255, 235, 170, hover / 4));
		}
		else
		{
			FrontendTheme::blob(target, x, y, w, h, radius, gel, ink, 1);
			if (hover) fillRounded(target, x + 2, y + 2, w - 4, h - 4, radius - 1, Color(gold.r, gold.g, gold.b, hover / 2));
			if (pressed) fillRounded(target, x + 2, y + 2, w - 4, h - 4, radius - 1, Color(180, 140, 50, 70));
		}
		Font* labelFont=fontPtr;
		if(labelFont->getStringWidth(text)>w-(primary?44:28)) labelFont=Toolkit::getFont("front-small");
		labelFont->pushStyle(Font::Style(Font::STYLE_NORMAL, ink));
		const int textY = y + (h - labelFont->getStringHeight(text)) / 2;
		int cx, cy, cw, ch;
		target->getClipRect(&cx, &cy, &cw, &ch);
		target->setClipRect(x + 14, y, w - (primary ? 44 : 28), h);
		target->drawString(x + 14, textY, labelFont, text);
		target->setClipRect(cx, cy, cw, ch);
		labelFont->popStyle();
		if (primary)
		{
			const int ax = x + w - 26, ay = y + h / 2;
			for (int col = 0; col < 9; ++col)
			{
				const int half = (9 - col) * 2 / 3;
				target->drawFilledRect(ax + col, ay - half, 1, 2 * half + 1, ink);
			}
		}
	}

	void onSDLMouseButtonDown(SDL_Event* event) override
	{
		pressed = event->button.button == SDL_BUTTON_LEFT && isOnWidget(event->button.x, event->button.y);
		TextButton::onSDLMouseButtonDown(event);
	}
	void onSDLMouseButtonUp(SDL_Event* event) override
	{
		pressed = false;
		TextButton::onSDLMouseButtonUp(event);
	}
};

MainMenuScreen::MainMenuScreen()
{
	const int width = globalContainer->gfx->getW(), height = globalContainer->gfx->getH();
	const bool compact = height < 640;
	panelX = std::clamp(width / 20, 20, 72);
	panelH = std::min(height - 40, 620);
	panelY = (height - panelH) / 2;
	panelW = compact ? 312 : 368;
	const std::string fontFile = std::string("data/fonts/") + PRIMARY_FONT;
	Toolkit::loadFont(fontFile, compact ? 28 : 32, "front-title");
	Toolkit::loadFont(fontFile, compact ? 18 : 20, "front-action");
	Toolkit::loadFont(fontFile, compact ? 14 : 16, "front-small");
	Toolkit::loadFont(fontFile, 12, "front-caption");

	// Fit the wordmark once using the existing
	// software blitter. Keep the text heading as a fallback if loading fails.
	DrawableSurface source(1, 1);
	if (source.loadImage("data/gfx/menu-wordmark.png"))
	{
		SDL_Rect crop{76, 232, 1956, 284};
		const int logoWidth = panelW - 48;
		const int logoHeight = logoWidth * crop.h / crop.w;
		auto* fitted = SDL_CreateRGBSurfaceWithFormat(0, logoWidth, logoHeight, 32, SDL_PIXELFORMAT_RGBA32);
		if (fitted)
		{
			if (SDL_BlitScaled(source.getSDLSurface(), &crop, fitted, nullptr) == 0)
			{
				// The source asset has a pale matte. Recover coverage for its two
				// flat inks before compositing, including the letter openings.
				for (int row = 0; row < logoHeight; ++row)
				{
					auto* pixels = reinterpret_cast<Uint32*>(static_cast<Uint8*>(fitted->pixels) + row * fitted->pitch);
					for (int col = 0; col < logoWidth; ++col)
					{
						Uint8 red, green, blue, alpha;
						SDL_GetRGBA(pixels[col], fitted->format, &red, &green, &blue, &alpha);
						const bool goldInk = red > green;
						double coverage = goldInk ? (int(green) - int(blue) - 20) / 65.0 : (232 - int(red)) / 200.0;
						coverage = coverage < 0.03 ? 0.0 : std::min(1.0, coverage);
						pixels[col] = SDL_MapRGBA(fitted->format,
							goldInk ? gold.r : ink.r, goldInk ? gold.g : ink.g, goldInk ? gold.b : ink.b,
							static_cast<Uint8>(std::lround(255 * coverage)));
					}
				}
				SDL_SetSurfaceBlendMode(fitted, SDL_BLENDMODE_BLEND);
				wordmark = std::make_unique<DrawableSurface>(fitted);
			}
			SDL_FreeSurface(fitted);
		}
	}


	const int x = panelX + 24, w = panelW - 48;
	int y = panelY + (compact ? 74 : 110);
	auto add = [&](const char* label, int action, int h, const char* font, bool primary = false)
	{
		auto* button = new MainMenuButton(x, y, w, h, font, label, action, primary);
		buttons.push_back(button);
		addWidget(button);
		y += h;
	};
	add("[custom game]", CUSTOM, compact ? 38 : 46, "front-action", true);
	y += 8;
	add("[campaign]", CAMPAIGN, compact ? 30 : 38, "front-action");
	y += 6;
	add("[load game]", LOAD_GAME, compact ? 30 : 38, "front-action");
	y += 6;
	add("[tutorial]", TUTORIAL, compact ? 30 : 38, "front-action");
	y += compact ? 12 : 18;
	add("[yog]", MULTIPLAYERS_YOG, compact ? 28 : 34, "front-small");
	y += 4;
	add("[lan]", MULTIPLAYERS_LAN, compact ? 28 : 34, "front-small");
	y += compact ? 12 : 18;
	const char* keys[] = {"[settings]", "[editor]", "[credits]", "[quit]"};
	const int actions[] = {GAME_SETUP, EDITOR, CREDITS, QUIT};
	const int utilityH = compact ? 28 : 32;
	for (int i = 0; i < 4; ++i)
	{
		auto* button = new MainMenuButton(x + (i % 2) * (w / 2 + 4),
			y + (i / 2) * (utilityH + 4), w / 2 - 4, utilityH,
			"front-small", keys[i], actions[i]);
		buttons.push_back(button);
		addWidget(button);
	}
}

MainMenuScreen::~MainMenuScreen()
{
	for (const char* name : {"front-title", "front-action", "front-small", "front-caption"})
		Toolkit::releaseFont(name);
}

void MainMenuScreen::paint()
{
	if (FrontendTheme::current) FrontendTheme::current->background(gfx, false);
	fillRounded(gfx, panelX + 3, panelY + 5, panelW, panelH, 12, Color(12, 28, 16, 70));
	FrontendTheme::blob(gfx, panelX, panelY, panelW, panelH, 12,
		Color(membrane.r, membrane.g, membrane.b, FrontendTheme::panelAlpha()), ink, 2);
	fillRounded(gfx, panelX + 24, panelY + 12, 36, 4, 2, gold);
	if (wordmark) gfx->drawSurface(panelX + 24, panelY + 24, wordmark.get());
	else
	{
		auto* title = Toolkit::getFont("front-title");
		title->setStyle(Font::Style(Font::STYLE_NORMAL, ink));
		gfx->drawString(panelX + 24, panelY + 24, title, "Globulation 2");
	}
	auto* caption = Toolkit::getFont("front-caption");
	caption->setStyle(Font::Style(Font::STYLE_NORMAL, muted));
	gfx->drawString(panelX + 24, panelY + panelH - 30, caption, PACKAGE_VERSION);
}

void MainMenuScreen::onSDLEvent(SDL_Event* event)
{
	if (event->type == SDL_MOUSEMOTION)
		focusedButton = -1;
	if (event->type == SDL_KEYDOWN)
	{
		const auto key = event->key.keysym.sym;
		const int count = static_cast<int>(buttons.size());
		if (key == SDLK_ESCAPE) endExecute(QUIT);
		else if (key == SDLK_TAB || key == SDLK_DOWN || key == SDLK_UP)
		{
			const bool backwards = key == SDLK_UP || (key == SDLK_TAB && (event->key.keysym.mod & KMOD_SHIFT));
			focusedButton = focusedButton < 0 ? (backwards ? count - 1 : 0)
				: (focusedButton + (backwards ? count - 1 : 1)) % count;
		}
		else if ((key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) && focusedButton >= 0)
			endExecute(buttons[focusedButton]->returnCode);
	}
	for (size_t i = 0; i < buttons.size(); ++i)
		buttons[i]->focused = static_cast<int>(i) == focusedButton;
}

void MainMenuScreen::onAction(Widget* source, Action action, int par1, int par2)
{
	if (action == BUTTON_RELEASED || action == BUTTON_SHORTCUT)
		endExecute(par1);
}

int MainMenuScreen::menu()
{
	return MainMenuScreen().execute(globalContainer->gfx, 40);
}
