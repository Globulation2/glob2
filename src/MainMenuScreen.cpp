// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "MainMenuScreen.h"
#include "FrontendTheme.h"
#include "GlobalContainer.h"
#include "MenuColony.h"
#include "render/UnitSkin.h"
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
// The front page is the most translucent sheet: the colony should read through it.
const int frontPageAlpha = 140;
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
		// Jelly: swell under the cursor, squash while pressed; fade in with the panel.
		const float inflate = pressed ? -1.0f : hover * 2.0f / 255.0f;
		const Uint8 entry = Uint8(255 * std::min(10, parent->animationFrame) / 10);
		auto withAlpha = [entry](Color c) { return Color(c.r, c.g, c.b, Uint8(int(c.a) * entry / 255)); };
		if (focused) fillRounded(target, x - 4, y - 4, w + 8, h + 8, radius + 4, withAlpha(violet));
		if (primary)
		{
			FrontendTheme::swell(target, x, y, w, h, radius, withAlpha(pressed ? goldPressed : gold), withAlpha(ink), 1, inflate);
			if (hover) fillRounded(target, x + 2, y + 2, w - 4, h - 4, radius - 1, Color(255, 235, 170, hover / 4));
		}
		else
		{
			FrontendTheme::swell(target, x, y, w, h, radius, withAlpha(gel), withAlpha(ink), 1, inflate);
			if (hover) fillRounded(target, x + 2, y + 2, w - 4, h - 4, radius - 1, Color(gold.r, gold.g, gold.b, hover / 2));
			if (pressed) fillRounded(target, x + 2, y + 2, w - 4, h - 4, radius - 1, Color(180, 140, 50, 70));
		}
		Font* labelFont=fontPtr;
		if(labelFont->getStringWidth(text)>w-(primary?44:28)) labelFont=Toolkit::getFont("front-small");
		labelFont->pushStyle(Font::Style(Font::STYLE_NORMAL, withAlpha(ink)));
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
	glob.x = x + 8;
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
	// The front page fades in over the screen's first ten frames.
	const int entry = std::min(10, animationFrame);
	auto withAlpha = [entry](Color c) { return Color(c.r, c.g, c.b, Uint8(int(c.a) * entry / 10)); };
	fillRounded(gfx, panelX + 3, panelY + 5, panelW, panelH, 12, withAlpha(Color(12, 28, 16, 70)));
	FrontendTheme::blob(gfx, panelX, panelY, panelW, panelH, 12,
		withAlpha(Color(membrane.r, membrane.g, membrane.b, FrontendTheme::panelAlpha(frontPageAlpha))), withAlpha(ink), 2);
	fillRounded(gfx, panelX + 24, panelY + 12, 36, 4, 2, withAlpha(gold));
	if (wordmark) gfx->drawSurface(panelX + 24, panelY + 24, wordmark.get(), Uint8(255 * entry / 10));
	else
	{
		auto* title = Toolkit::getFont("front-title");
		title->setStyle(Font::Style(Font::STYLE_NORMAL, ink));
		gfx->drawString(panelX + 24, panelY + 24, title, "Globulation 2");
	}
	auto* caption = Toolkit::getFont("front-caption");
	caption->setStyle(Font::Style(Font::STYLE_NORMAL, muted));
	gfx->drawString(panelX + 24, panelY + panelH - 30, caption, PACKAGE_VERSION);

	// The glob: the colony's own worker sprite, in its team colour, walking the
	// otherwise empty band above the version caption. Direction 3 faces east,
	// 7 west; eight walk frames per direction.
	if (globFrames.empty() && FrontendTheme::current && FrontendTheme::current->colony->ready()) buildGlobFrames();
	if (!globFrames.empty())
	{
		const int frame = glob.walking ? int(glob.phase / 80) % 8 : 0;
		auto* image = globFrames[(glob.dir > 0 ? 0 : 8) + frame].get();
		const int gx = int(glob.x) - (image->getW() - 32) / 2, gy = panelY + panelH - 92 - (image->getH() - 32) / 2;
		gfx->drawSurface(gx, gy, image, Uint8(255 * entry / 10));
	}
}

void MainMenuScreen::buildGlobFrames()
{
	const auto& skin = g_unitSkins[WORKER];
	if (!skin.sprite) return;
	skin.sprite->setBaseColor(FrontendTheme::current->colony->teamColor());
	for (int direction : {3, 7})
		for (int frame = 0; frame < 8; ++frame)
		{
			const int index = int(skin.startImage[WALK]) + 8 * direction + frame;
			const int w = skin.sprite->getW(index), h = skin.sprite->getH(index);
			DrawableSurface canvas(w, h);
			SDL_Surface* pixels = canvas.getSDLSurface();
			SDL_FillRect(pixels, nullptr, 0);
			canvas.drawSprite(0, 0, skin.sprite, index);
			for (int row = 0; row < h; ++row)
			{
				auto* line = reinterpret_cast<Uint32*>(static_cast<Uint8*>(pixels->pixels) + row * pixels->pitch);
				for (int col = 0; col < w; ++col)
				{
					Uint8 red, green, blue, alpha;
					SDL_GetRGBA(line[col], pixels->format, &red, &green, &blue, &alpha);
					if (alpha < 32) line[col] = SDL_MapRGBA(pixels->format, 0, 0, 0, 0);
				}
			}
			SDL_SetSurfaceBlendMode(pixels, SDL_BLENDMODE_BLEND);
			globFrames.push_back(std::make_unique<DrawableSurface>(pixels));
		}
}

void MainMenuScreen::onTimer(Uint32 tick)
{
	if (glob.lastTick == 0) { glob.lastTick = tick; return; }
	const Uint32 elapsed = std::min<Uint32>(tick - glob.lastTick, 100);
	glob.lastTick = tick;
	glob.walking = tick >= glob.idleUntil;
	if (!glob.walking) return;
	const int left = panelX + 32, right = panelX + panelW - 64;
	glob.x += glob.dir * 24.0 * elapsed / 1000.0;
	glob.phase += elapsed;
	glob.seed = glob.seed * 1664525u + 1013904223u;
	const bool atEdge = glob.x <= left || glob.x >= right;
	// Turn at the edges; otherwise pause now and then, about once every six seconds.
	if (atEdge || (glob.seed >> 8) % 6000 < elapsed)
	{
		glob.x = std::clamp(glob.x, double(left), double(right));
		if (atEdge) glob.dir = -glob.dir;
		glob.idleUntil = tick + 1000 + (glob.seed >> 16) % 2000;
	}
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
