// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "MainMenuScreen.h"
#include "FrontendTheme.h"
#include "LobbyControls.h"
#include "gui/FrontendLayout.h"
#include <InterfacePresentation.h>
#include "GlobalContainer.h"
#include <GUIButton.h>
#include <Toolkit.h>
#include <StringTable.h>
#include <algorithm>
#include <cmath>

#ifdef HAVE_CONFIG_H
#include <glob2/BuildConfig.h>
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
const Color gold(227, 192, 119);
const Color ink(17, 35, 32);
const Color textColor(36, 69, 49);
const Color muted(92, 114, 91);

const auto fillRounded = FrontendTheme::rounded;
} // namespace

// Front-page layout and primary-action emphasis; shared drawing comes from the theme.
class MainMenuButton : public TextButton
{
  public:
	bool focused = false;
	bool primary;
	bool pressed = false;

	MainMenuButton(int x, int y, int w, int h, const char *font, const char *key, int action,
				   bool primary = false)
		: TextButton(x, y, w, h, ALIGN_LEFT, ALIGN_TOP, font,
					 Toolkit::getStringTable()->getString(key), action),
		  primary(primary)
	{
	}

	void paint() override
	{
		int x, y, w, h;
		getScreenPos(&x, &y, &w, &h);
		auto *target = parent->getSurface();
		const unsigned hover = focused ? 255 : getNextHighlightValue();
		const int radius = primary ? 8 : 4;
		if (focused)
			fillRounded(target, x - 3, y - 3, w + 6, h + 6, radius + 3, Color(67, 116, 75));
		if (primary)
		{
			fillRounded(target, x, y, w, h, radius, pressed ? Color(208, 172, 99) : gold);
			if (hover)
				fillRounded(target, x, y, w, h, radius, Color(255, 245, 211, hover / 5));
		}
		else
		{
			fillRounded(target, x, y, w, h, radius, Color(234, 240, 228));
			if (hover)
				fillRounded(target, x, y, w, h, radius, Color(169, 196, 157, hover / 2));
			if (pressed)
				fillRounded(target, x, y, w, h, radius, Color(92, 130, 71, 55));
		}
		Font *labelFont = fontPtr;
		if (labelFont->getStringWidth(text) > w - (primary ? 44 : 28))
			labelFont = Toolkit::getFont("front-small");
		labelFont->pushStyle(Font::Style(Font::STYLE_NORMAL, primary ? ink : textColor));
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

	void onSDLMouseButtonDown(SDL_Event *event) override
	{
		pressed =
			event->button.button == SDL_BUTTON_LEFT && isOnWidget(event->button.x, event->button.y);
		TextButton::onSDLMouseButtonDown(event);
	}
	void onSDLMouseButtonUp(SDL_Event *event) override
	{
		pressed = false;
		TextButton::onSDLMouseButtonUp(event);
	}

	void setGeometry(int nx, int ny, int nw, int nh)
	{
		x = nx;
		y = ny;
		w = nw;
		h = nh;
	}
};

MainMenuScreen::MainMenuScreen()
{
	mobileControls = new LobbyControls();
	mobileControls->render = [this] { renderMobile(); };
	addWidget(mobileControls);
	const int width = globalContainer->gfx->getW(), height = globalContainer->gfx->getH();
	compact = height < 640;
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
		auto *fitted =
			SDL_CreateRGBSurfaceWithFormat(0, logoWidth, logoHeight, 32, SDL_PIXELFORMAT_RGBA32);
		if (fitted)
		{
			if (SDL_BlitScaled(source.getSDLSurface(), &crop, fitted, nullptr) == 0)
			{
				// The source asset has a pale matte. Recover coverage for its two
				// flat inks before compositing, including the letter openings.
				for (int row = 0; row < logoHeight; ++row)
				{
					auto *pixels = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(fitted->pixels) +
															  row * fitted->pitch);
					for (int col = 0; col < logoWidth; ++col)
					{
						Uint8 red, green, blue, alpha;
						SDL_GetRGBA(pixels[col], fitted->format, &red, &green, &blue, &alpha);
						const bool goldInk = red > green;
						double coverage = goldInk ? (int(green) - int(blue) - 20) / 65.0
												  : (232 - int(red)) / 200.0;
						coverage = coverage < 0.03 ? 0.0 : std::min(1.0, coverage);
						pixels[col] = SDL_MapRGBA(fitted->format, goldInk ? 227 : 36,
												  goldInk ? 192 : 69, goldInk ? 119 : 49,
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
	auto add = [&](const char *label, int action, int h, const char *font, bool primary = false)
	{
		auto *button = new MainMenuButton(x, y, w, h, font, label, action, primary);
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
#ifndef __EMSCRIPTEN__
	add("[lan]", MULTIPLAYERS_LAN, compact ? 28 : 34, "front-small");
#endif
	y += compact ? 12 : 18;
	const char *keys[] = {"[settings]", "[editor]", "[credits]", "[quit]"};
	const int actions[] = {GAME_SETUP, EDITOR, CREDITS, QUIT};
	const int utilityH = compact ? 28 : 32;
	for (int i = 0; i < 4; ++i)
	{
		auto *button = new MainMenuButton(x + (i % 2) * (w / 2 + 4), y + (i / 2) * (utilityH + 4),
										  w / 2 - 4, utilityH, "front-small", keys[i], actions[i]);
		buttons.push_back(button);
		addWidget(button);
	}
}

void MainMenuScreen::layout(int width, int height)
{
	panelX = std::clamp(width / 20, 20, 72);
	panelH = std::min(height - 40, 620);
	panelY = (height - panelH) / 2;
	panelW = compact ? 312 : 368;
	if (buttons.empty())
		return;

	const int x = panelX + 24, w = panelW - 48;
	int y = panelY + (compact ? 74 : 110);
	size_t index = 0;
	auto place = [&](int height)
	{
		buttons[index++]->setGeometry(x, y, w, height);
		y += height;
	};
	place(compact ? 38 : 46);
	y += 8;
	for (int i = 0; i < 3; ++i)
	{
		place(compact ? 30 : 38);
		y += 6;
	}
	y += compact ? 6 : 12;
	place(compact ? 28 : 34);
	y += 4;
#ifndef __EMSCRIPTEN__
	place(compact ? 28 : 34);
#endif
	y += compact ? 12 : 18;
	const int utilityH = compact ? 28 : 32;
	for (int i = 0; i < 4; ++i)
		buttons[index++]->setGeometry(x + (i % 2) * (w / 2 + 4), y + (i / 2) * (utilityH + 4),
									  w / 2 - 4, utilityH);
}

void MainMenuScreen::viewportResized(int oldWidth, int oldHeight, int width, int height)
{
	Glob2Screen::viewportResized(oldWidth, oldHeight, width, height);
	layout(width, height);
}

MainMenuScreen::~MainMenuScreen()
{
	for (const char *name : {"front-title", "front-action", "front-small", "front-caption"})
		Toolkit::releaseFont(name);
}

void MainMenuScreen::paint()
{
	if (FrontendTheme::current)
		FrontendTheme::current->background(gfx, false);
	const bool mobile = phonePresentationRequested();
	for (auto *button : buttons)
		button->visible = !mobile;
	mobileControls->visible = mobile;
	if (mobile)
		return;
	fillRounded(gfx, panelX + 2, panelY + 3, panelW, panelH, 10, Color(15, 39, 25, 35));
	fillRounded(gfx, panelX, panelY, panelW, panelH, 10, Color(230, 231, 210, 248));
	fillRounded(gfx, panelX + 24, panelY + 12, 36, 4, 2, gold);
	if (wordmark)
		gfx->drawSurface(panelX + 24, panelY + 24, wordmark.get());
	else
	{
		auto *title = Toolkit::getFont("front-title");
		title->setStyle(Font::Style(Font::STYLE_NORMAL, textColor));
		gfx->drawString(panelX + 24, panelY + 24, title, "Globulation 2");
	}
	auto *caption = Toolkit::getFont("front-caption");
	caption->setStyle(Font::Style(Font::STYLE_NORMAL, muted));
	gfx->drawString(panelX + 24, panelY + panelH - 30, caption, PACKAGE_VERSION);
}

void MainMenuScreen::onSDLEvent(SDL_Event *event)
{
	if (phonePresentationRequested())
	{
		if (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_ESCAPE)
		{
			more = false;
			return;
		}
		mobileControls->handle(event);
		return;
	}
	if (event->type == SDL_MOUSEMOTION)
		focusedButton = -1;
	if (event->type == SDL_KEYDOWN)
	{
		const auto key = event->key.keysym.sym;
		const int count = static_cast<int>(buttons.size());
		if (key == SDLK_ESCAPE)
			endExecute(QUIT);
		else if (key == SDLK_TAB || key == SDLK_DOWN || key == SDLK_UP)
		{
			const bool backwards =
				key == SDLK_UP || (key == SDLK_TAB && (event->key.keysym.mod & KMOD_SHIFT));
			focusedButton = focusedButton < 0
								? (backwards ? count - 1 : 0)
								: (focusedButton + (backwards ? count - 1 : 1)) % count;
		}
		else if ((key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) &&
				 focusedButton >= 0)
			endExecute(buttons[focusedButton]->returnCode);
	}
	for (size_t i = 0; i < buttons.size(); ++i)
		buttons[i]->focused = static_cast<int>(i) == focusedButton;
}

void MainMenuScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if (action == BUTTON_RELEASED || action == BUTTON_SHORTCUT)
		endExecute(par1);
}

// Mobile navigation keeps launch choices prominent and utilities one level away.
// This uses the same action codes and wordmark as the desktop menu.
void MainMenuScreen::renderMobile()
{
	auto &ui = *mobileControls;
	const auto safe = FrontendLayout::resolve(globalContainer->gfx).safe;
	ui.setScreenPosition(0, 0);
	ui.setDimensions(getW(), getH());
	const int w = std::min(440, int(safe.w) - 24), x = int(safe.x) + (int(safe.w) - w) / 2;
	const int panelHeight = std::min(int(safe.h) - 24, more ? 596 : 420);
	const int top = int(safe.y) + (int(safe.h) - panelHeight) / 2, bottom = top + panelHeight;
	ui.box({x, top, w, bottom - top}, Color(230, 231, 210, 248), 10);
	if (wordmark)
	{
		const int lw = std::min(w - 132, wordmark->getW()),
				  lh = lw * wordmark->getH() / wordmark->getW();
		gfx->drawSurface(x + 16, top + 24, lw, lh, wordmark.get());
	}
	ui.button("menu/settings", {x + w - 112, top + 8, 104, 48}, "",
			  [this] { endExecute(GAME_SETUP); });
	// Draw the settings gear geometrically: it must not depend on a font having
	// an emoji/symbol glyph. The adjacent label names the control without hover.
	const int gearX = x + w - 104, gearY = top + 32;
	ui.box({gearX, gearY - 9, 18, 18}, ui.ink, 6);
	ui.box({gearX + 5, gearY - 4, 8, 8}, ui.panel, 4);
	for (int i = 0; i < 8; ++i)
	{
		const double angle = i * 3.141592653589793 / 4;
		gfx->drawLine(gearX + 9 + int(8 * std::cos(angle)), gearY + int(8 * std::sin(angle)),
					  gearX + 9 + int(13 * std::cos(angle)), gearY + int(13 * std::sin(angle)),
					  ui.ink);
	}
	ui.text(x + w - 76, top + 24, Toolkit::getStringTable()->getString("[settings]"), "standard",
			68);
	ui.beginRegion(80, {x + 12, top + 72, w - 24, std::max(0, bottom - top - 84)});
	int y = top + 72 - ui.regions[80].offset;
	const int start = y;
	auto action = [&](const char *key, int code, bool primary = false)
	{
		ui.button(
			"menu/" + std::to_string(code), {x + 12, y, w - 24, 56},
			Toolkit::getStringTable()->getString(key),
			[this, code] { onAction(nullptr, BUTTON_RELEASED, code, 0); }, primary);
		y += 64;
	};
	const bool landscape = safe.w > safe.h && safe.h < 480;
	if (!more && landscape)
	{
		// Keep every top-level destination visible on short phones. The
		// primary play action spans the row; secondary destinations form a grid.
		action("[custom game]", CUSTOM, true);
		const int half = (w - 32) / 2;
		const char *labels[] = {"[campaign]", "[load game]", "[tutorial]", "[More]"};
		const int codes[] = {CAMPAIGN, LOAD_GAME, TUTORIAL};
		for (int i = 0; i < 4; ++i)
			ui.button(i == 3 ? "menu/more" : "menu/" + std::to_string(codes[i]),
					  {x + 12 + (i % 2) * (half + 8), y + (i / 2) * 56, half, 48},
					  Toolkit::getStringTable()->getString(labels[i]),
					  [this, i, codes]
					  {
						  if (i == 3)
						  {
							  more = true;
							  mobileControls->regions[80].offset = 0;
						  }
						  else
							  onAction(nullptr, BUTTON_RELEASED, codes[i], 0);
					  });
		y += 112;
	}
	else if (!more)
	{
		action("[custom game]", CUSTOM, true);
		y += 12;
		action("[campaign]", CAMPAIGN);
		action("[load game]", LOAD_GAME);
		action("[tutorial]", TUTORIAL);
		y += 12;
		ui.button("menu/more", {x + 12, y, w - 24, 48},
				  Toolkit::getStringTable()->getString("[More]"),
				  [this]
				  {
					  more = true;
					  mobileControls->regions[80].offset = 0;
				  });
		y += 56;
	}
	else
	{
		ui.button("menu/back", {x + 12, y, w - 24, 48},
				  Toolkit::getStringTable()->getString("[Back]"),
				  [this]
				  {
					  more = false;
					  mobileControls->regions[80].offset = 0;
				  });
		y += 64;
		action("[yog]", MULTIPLAYERS_YOG);
#ifndef __EMSCRIPTEN__
		action("[lan]", MULTIPLAYERS_LAN);
#endif
		action("[editor]", EDITOR);
		action("[credits]", CREDITS);
#if !defined(__EMSCRIPTEN__) && !defined(GLOB2_MOBILE)
		action("[quit]", QUIT);
#endif
	}
	ui.endRegion(y - start);
}

void MainMenuScreen::cancelExecutionInput()
{
	Glob2Screen::cancelExecutionInput();
	if (mobileControls)
		mobileControls->cancelTouch();
}
