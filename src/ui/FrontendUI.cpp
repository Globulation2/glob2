// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/FrontendUI.h"
#include "GlobalContainer.h"
#include "GUIMapPreview.h"
#include "gui/InGameTouchTheme.h"
#include <GUIStyle.h>
#include <StringTable.h>
#include <Toolkit.h>

namespace Glob2UI
{
const Theme &frontendTheme()
{
	static const Theme theme = []
	{
		Theme t;
		t.fonts = {"front-title", "menu", "standard", "little", "front-caption"};
		t.touchFonts = {"front-title", "menu", "frontend-body", "frontend-support", "front-caption"};
		return t;
	}();
	return theme;
}

const Theme &inGameTheme()
{
	static const Theme theme = []
	{
		Theme t;
		t.fonts = {"menu", "menu", "standard", "little", "little"};
		t.touchFonts = {"menu", "menu", "frontend-body", "frontend-support", "frontend-support"};
		// The dark in-match look of the touch HUD, so dialogs sit on the map without
		// borrowing the frontend's paper.
		auto &c = t.palette;
		c.ink = InGameTouchTheme::ink;
		c.muted = GAGCore::Color(204, 188, 152);
		c.paper = GAGCore::Color(43, 28, 66);
		c.panel = GAGCore::Color(43, 28, 66, 244);
		c.field = GAGCore::Color(65, 43, 88);
		c.rail = GAGCore::Color(55, 36, 78);
		c.line = InGameTouchTheme::border;
		c.accent = InGameTouchTheme::border;
		c.accentInk = GAGCore::Color(30, 18, 40);
		c.selected = GAGCore::Color(114, 78, 111);
		c.hover = GAGCore::Color(92, 62, 116);
		c.focus = GAGCore::Color(255, 214, 120);
		c.scrim = GAGCore::Color(10, 6, 20, 140);
		c.disabled = GAGCore::Color(52, 38, 70);
		return t;
	}();
	return theme;
}

std::string tr(const std::string &key)
{
	return GAGCore::Toolkit::getStringTable()->getString(key);
}

double frontendTextScale(const Presentation &presentation)
{
	if (!presentation.touch)
		return 1;
	const int percent = globalContainer ? globalContainer->settings.mobileDialogTextPercent : 100;
	return 1.15 * (percent > 0 ? percent : 100) / 100.0;
}

Screen::Screen() : UIScreen(frontendTheme()) {}
Screen::~Screen() = default;

double Screen::textScale(const Presentation &presentation) const
{
	return frontendTextScale(presentation);
}

void Screen::paintBackground(Canvas &canvas)
{
	auto *surface = canvas.surface();
	if (FrontendTheme::current && surface)
		FrontendTheme::current->background(surface, false);
	else
		UIScreen::paintBackground(canvas);
}

void Screen::beforePaint()
{
	if (GAGGUI::Style::style)
		GAGGUI::Style::style->onFrame();
}

Dialog::Dialog() : UIDialog(frontendTheme()) {}

double Dialog::textScale(const Presentation &presentation) const
{
	return frontendTextScale(presentation);
}

InGameDialog::InGameDialog() : UIDialog(inGameTheme()) {}

double InGameDialog::textScale(const Presentation &presentation) const
{
	return frontendTextScale(presentation);
}

namespace
{
Element actionButton(const MenuAction &action, bool large)
{
	ButtonOptions options;
	options.primary = action.primary;
	options.shortcut = action.shortcut;
	options.enabled = action.enabled;
	if (large)
	{
		options.minHeight = 48;
		options.role = FontRole::Heading;
	}
	return button(action.key, action.label, action.action, options);
}
} // namespace

Element menu(const std::string &titleText, std::vector<MenuAction> items, const Presentation &p)
{
	std::vector<Element> buttons;
	for (const auto &item : items)
		buttons.push_back(actionButton(item, true));
	WrapOptions grid;
	grid.minChildWidth = p.pt(260);
	grid.maxColumns = 2;
	std::vector<Element> parts;
	if (!titleText.empty())
		parts.push_back(paragraph(titleText, {FontRole::Heading, false, TextAlign::Center}));
	parts.push_back(scroll("menu/scroll", wrap(std::move(buttons), grid)));
	return center(maxWidth(p.pt(640), card(column(std::move(parts), {p.pt(12)}))));
}

Element actions(std::vector<MenuAction> items, const Presentation &p)
{
	std::vector<Element> buttons;
	for (const auto &item : items)
		buttons.push_back(actionButton(item, p.touch));
	WrapOptions grid;
	grid.minChildWidth = p.pt(150);
	return wrap(std::move(buttons), grid);
}

Element page(const std::string &titleText, Element body, Element actionRow, const Presentation &p,
			 double maxWidthPoints)
{
	std::vector<Element> parts;
	if (!titleText.empty())
		parts.push_back(paragraph(titleText, {FontRole::Heading, false, TextAlign::Center}));
	parts.push_back(footer(std::move(body), std::move(actionRow)));
	return center(maxWidth(p.pt(maxWidthPoints), card(column(std::move(parts), {p.pt(12)}))));
}
} // namespace Glob2UI

namespace Glob2UI
{
Element animation(const std::string &key, GAGCore::Sprite *sprite, int frames, int millisecondsPerFrame)
{
	if (!sprite || frames <= 0)
		return empty();
	const Size size{sprite->getW(0), sprite->getH(0)};
	const int period = std::max(1, millisecondsPerFrame);
	return canvas(key, size,
				  [sprite, frames, period](Canvas &c, Rect r, const Frame &frame)
				  {
					  const int index = int((frame.tick / Uint32(period)) % Uint32(frames));
					  c.drawSprite({r.x + (r.w - sprite->getW(index)) / 2, r.y + (r.h - sprite->getH(index)) / 2}, sprite, index);
				  });
}

Element mapPreview(const std::string &key, ::MapPreview &preview, double points, bool flexible)
{
	CanvasOptions options;
	options.keepAspect = true;
	options.accessibleText = "map preview";
	auto *widget = &preview;
	auto forward = [widget](PointerPhase phase, Point local, Host &)
	{
		SDL_Event event{};
		const Point at{widget->getLeft() + local.x, widget->getTop() + local.y};
		if (phase == PointerPhase::Down || phase == PointerPhase::Up)
		{
			event.type = phase == PointerPhase::Down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
			event.button.button = SDL_BUTTON_LEFT;
			event.button.x = at.x;
			event.button.y = at.y;
		}
		else if (phase == PointerPhase::Move)
		{
			event.type = SDL_MOUSEMOTION;
			event.motion.state = SDL_BUTTON_LMASK;
			event.motion.x = at.x;
			event.motion.y = at.y;
		}
		else
		{
			widget->cancelDrag();
			return;
		}
		widget->handlePreviewEvent(&event);
	};
	options.pointer = forward;
	options.wheel = [widget](int direction, Point local)
	{
		SDL_Event motion{};
		motion.type = SDL_MOUSEMOTION;
		motion.motion.x = widget->getLeft() + local.x;
		motion.motion.y = widget->getTop() + local.y;
		widget->handlePreviewEvent(&motion);
		SDL_Event wheel{};
		wheel.type = SDL_MOUSEWHEEL;
		wheel.wheel.y = direction;
		widget->handlePreviewEvent(&wheel);
	};
	auto element = canvas(key, {int(points), int(points)},
						  [widget](Canvas &c, Rect r, const Frame &)
						  {
							  widget->setScreenRectangle(r.x, r.y, r.w, r.h);
							  widget->paint(c.surface());
						  },
						  options);
	if (flexible)
		element->flex = 1;
	return element;
}
} // namespace Glob2UI
