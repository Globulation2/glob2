// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/FrontendUI.h"
#include "GlobalContainer.h"
#include "GUIMapPreview.h"
#include "gui/InGameTouchTheme.h"
#include "InterfacePresentation.h"
#include <GUIStyle.h>
#include <StringTable.h>
#include <Toolkit.h>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <map>

namespace Glob2UI
{
IconRef uiIcon(UIIcon icon)
{
	static std::map<std::string, std::weak_ptr<const IconAsset>> assets;
	static constexpr std::array names = {"settings",       "pencil",     "folder-open",
										 "device-desktop", "volume",     "adjustments-horizontal",
										 "building",       "keyboard",   "user",
										 "player-play",    "flag",       "book",
										 "world",          "network",    "info-circle",
										 "logout",         "arrow-left", "dots",
										 "send",           "x",          "refresh",
										 "info-circle",    "flask",      "link",
										 "copy",           "share",      "trophy",
										 "robot",          "wifi-off",   "antenna-bars-5",
										 "shield-check",   "server",     "users",
										 "map",            "message",    "check",
										 "plus",           "login",      "crown",
										 "lock",           "external-link", "download",
										 "bolt",           "hash",       "door-exit",
										 "player-play",    "adjustments-horizontal", "loader-2",
										 "alert-triangle", "search",   "upload",     "heart"};
	static_assert(names.size() == static_cast<std::size_t>(UIIcon::Count));
	const char *name = names.at(static_cast<std::size_t>(icon));
	if (auto asset = assets[name].lock())
		return asset;
	auto asset = std::make_shared<IconAsset>();
	asset->name = name;
	for (int pixels : {20, 24, 40, 48, 60, 72})
	{
		auto surface = std::make_shared<GAGCore::DrawableSurface>(1, 1);
		const std::string path =
			"data/gui/tabler-" + asset->name + "-" + std::to_string(pixels) + ".png";
		if (surface->loadImage(path))
			asset->rasters.push_back({pixels, std::move(surface)});
		else
			std::fprintf(stderr, "ui: missing icon raster %s\n", path.c_str());
	}
	assets[name] = asset;
	return asset;
}

Element compactButton(const std::string &key, const std::string &label, UIIcon icon,
					  std::function<void()> action, const Presentation &p, ButtonOptions options)
{
	if (!p.touch)
		return button(key, label, std::move(action), options);
	options.icon = uiIcon(icon);
	options.iconSize = 24;
	options.minHeight = std::max(48.0, options.minHeight);
	options.accessibleLabel = label;
	options.tooltip = label;
	if (!options.icon->available())
		return button(key, label, std::move(action), options);
	return width(p.pt(48), button(key, "", std::move(action), options));
}

// Every touch theme shares one text size, so menus, dialogs over gameplay and
// the end-of-game sheet read alike; the player's preference multiplies it.
constexpr double touchTextBase = 1.15;

const Theme &frontendTheme()
{
	static const Theme theme = []
	{
		Theme t;
		t.fonts = {"front-title", "menu", "standard", "little", "front-caption"};
		t.touchFonts = {"front-title", "menu", "frontend-body", "frontend-support", "front-caption"};
		t.touchTextScale = touchTextBase;
		return t;
	}();
	return theme;
}

const Theme &inGameTheme()
{
	// Dialogs over a match (the in-game menu, confirmations, the end of the match) and
	// the results use the frontend's paper, ink and gold, the same as the Online hub
	// and the room, on every host: one design language instead of a navy desktop box
	// and a purple phone sheet.
	static const Theme theme = []
	{
		Theme t = frontendTheme();
		t.palette.scrim = GAGCore::Color(10, 6, 20, 120);
		return t;
	}();
	return theme;
}

const Theme &classicInGameTheme()
{
	static const Theme theme = []
	{
		Theme t;
		t.fonts = {"menu", "menu", "standard", "little", "little"};
		t.touchFonts = t.fonts;
		// The classic overlay: navy panel, white text, gold sprite buttons.
		auto &c = t.palette;
		c.ink = GAGCore::Color(255, 255, 255);
		c.muted = GAGCore::Color(200, 200, 220);
		c.paper = GAGCore::Color(0, 0, 40);
		c.panel = GAGCore::Color(0, 0, 40);
		c.field = GAGCore::Color(20, 20, 70);
		c.rail = GAGCore::Color(12, 12, 55);
		c.line = GAGCore::Color(199, 165, 87);
		c.accent = GAGCore::Color(222, 190, 110);
		c.accentInk = GAGCore::Color(24, 22, 48);
		c.selected = GAGCore::Color(60, 60, 120);
		c.hover = GAGCore::Color(120, 120, 200);
		c.focus = GAGCore::Color(255, 214, 120);
		c.warning = GAGCore::Color(255, 214, 120);
		c.disabled = GAGCore::Color(40, 40, 70);
		c.success = GAGCore::Color(100, 255, 100);
		c.danger = GAGCore::Color(255, 80, 80);
		c.pressed = GAGCore::Color(255, 255, 255, 40);
		c.shadow = GAGCore::Color(0, 0, 0, 0);
		t.controlHeight = 28;
		t.radius = 12;
		t.padding = 10;
		// The gold sprite buttons are drawn at exactly 40 logical pixels; other
		// sizes and scales fall back to the palette's rounded gold.
		t.buttonPainter = [](Canvas &canvas, Rect r, const ButtonPaintState &state)
		{
			auto *surface = canvas.surface();
			if (!surface || !GAGGUI::Style::style || r.h != 40 || !state.enabled)
				return false;
			GAGGUI::Style::style->drawTextButtonBackground(surface, r.x, r.y, r.w, r.h, state.pressed ? 255 : state.hovered ? 128 : 0);
			return true;
		};
		return t;
	}();
	return theme;
}

bool touchPresentation()
{
	return phonePresentationRequested();
}

std::string tr(const std::string &key)
{
	return GAGCore::Toolkit::getStringTable()->getString(key);
}

Screen::Screen() : UIScreen(frontendTheme()) {}
Screen::~Screen() = default;

void Screen::paintBackground(Canvas &canvas)
{
	auto *surface = canvas.surface();
	if (FrontendTheme::current && surface)
		FrontendTheme::current->background(surface, false);
	else
		UIScreen::paintBackground(canvas);
}

void Screen::adjustPresentation(Presentation &p)
{
	const char *forced = std::getenv("GLOB2_UI_SCALE");
	const bool followsDesktop = GAGCore::GraphicContext::getRequestedUiScale() <= 0 && !(forced && *forced);
	applyComfortScale(p, comfortScale(p, followsDesktop));
}

void Screen::beforePaint()
{
	if (GAGGUI::Style::style)
		GAGGUI::Style::style->onFrame();
}

Dialog::Dialog() : UIDialog(frontendTheme()) {}

InGameDialog::InGameDialog()
	: UIDialog(inGameTheme()), classicLook(false)
{
}

Rect InGameDialog::insetAvailable(const Presentation &p, const Metrics &m)
{
	// UIDialog adds its internal padding back when painting the panel.
	// Reserve the outer gutter separately so it cannot be consumed by content.
	return UIDialog::available(p, m).inset(p.pt(16));
}

void InGameDialog::paintPanel(Canvas &canvas, Rect panel)
{
	if (!classicLook)
	{
		UIDialog::paintPanel(canvas, panel);
		return;
	}
	canvas.fillRect(panel, theme().palette.paper);
	auto *surface = canvas.surface();
	if (surface && GAGGUI::Style::style)
		GAGGUI::Style::style->drawFrame(surface, panel.x, panel.y, panel.w, panel.h, 0);
	else
		canvas.strokeRect(panel, theme().palette.line);
}

Element InGameDialog::dialogActions(std::vector<MenuAction> items, const Presentation &p) const
{
	if (!classicLook)
		return actions(std::move(items), p);
	std::vector<Element> buttons;
	for (const auto &item : items)
	{
		ButtonOptions options;
		options.shortcut = item.shortcut;
		options.enabled = item.enabled;
		options.role = FontRole::Heading;
		options.minHeight = 40;
		buttons.push_back(width(p.pt(135), button(item.key, item.label, item.action, options)));
	}
	return row(std::move(buttons), {p.pt(10), CrossAlign::Center, MainAlign::End});
}

Element InGameDialog::classicButton(const std::string &key, const std::string &label, std::function<void()> action,
									SDL_Keycode shortcut, bool enabled, double widthPoints) const
{
	ButtonOptions options;
	options.shortcut = shortcut;
	options.enabled = enabled;
	options.role = FontRole::Heading;
	options.minHeight = 40;
	const auto &p = presentation();
	return center(width(p.pt(widthPoints), button(key, label, std::move(action), options)));
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
		options.minHeight = std::max(48.0, options.minHeight);
		options.role = FontRole::Heading;
	}
	return button(action.key, action.label, action.action, options);
}

// A classic frontend button: menu font, 40 points tall, plain paper.
Element classicButton(const MenuAction &action, const Presentation &p, double widthPoints)
{
	ButtonOptions options;
	options.shortcut = action.shortcut;
	options.enabled = action.enabled;
	options.role = FontRole::Heading;
	options.minHeight = 40;
	return constrained({p.pt(widthPoints), 0, Constraints::Unbounded, Constraints::Unbounded},
					   button(action.key, action.label, action.action, options));
}
} // namespace

Element menu(const std::string &titleText, std::vector<MenuAction> items, const Presentation &p)
{
	if (!p.touch)
	{
		// The classic narrow panel: 300-point buttons stacked from the top, the
		// escape route pinned at the bottom.
		std::vector<Element> parts;
		Element escape;
		for (const auto &item : items)
		{
			auto element = classicButton(item, p, 300);
			if (item.shortcut == SDLK_ESCAPE && !escape)
				escape = element;
			else
				parts.push_back(element);
		}
		std::vector<Element> body;
		if (!titleText.empty())
			body.push_back(paragraph(titleText, {FontRole::Heading, false, TextAlign::Center}));
		// Scrolls when larger text makes the buttons outgrow the fixed panel.
		body.push_back(expanded(scroll("menu/items", column(std::move(parts), {p.pt(20)}))));
		if (escape)
			body.push_back(escape);
		CardOptions cardOptions;
		cardOptions.padding = p.pt(12);
		const int panelHeight = std::min(p.safe.h - 2 * p.pt(8), p.pt(titleText.empty() ? 405 : 440));
		return center(sized({p.pt(324), panelHeight}, card(column(std::move(body), {p.pt(10)}), cardOptions)));
	}
	std::vector<Element> buttons;
	for (auto item : items)
	{
		// Touch menus list plain actions, as before.
		item.primary = false;
		buttons.push_back(actionButton(item, true));
	}
	WrapOptions grid;
	grid.minChildWidth = p.pt(260);
	grid.maxColumns = 2;
	std::vector<Element> parts;
	if (!titleText.empty())
		parts.push_back(paragraph(titleText, {FontRole::Heading, false, TextAlign::Center}));
	parts.push_back(scroll("menu/scroll", wrap(std::move(buttons), grid)));
	return center(maxWidth(p.pt(640), card(column(std::move(parts), {p.pt(12)}))));
}

Element actions(std::vector<MenuAction> items, const Presentation &p, ActionStyle style)
{
	if (!p.touch)
	{
		if (style == ActionStyle::Classic)
		{
			// 180-point menu-font buttons at the right; a row that cannot fit
			// becomes a grid, as the previous action rows did.
			return adaptive(
				[items, p](const LayoutContext &ctx, Size available) -> Element
				{
					int needed = 0;
					std::vector<Element> buttons;
					for (const auto &item : items)
					{
						needed += std::max(p.pt(180), ctx.text.width(FontRole::Heading, item.label) + 2 * ctx.metrics.padding) + ctx.metrics.gap;
						buttons.push_back(classicButton(item, p, 180));
					}
					if (needed <= available.w)
						return row(std::move(buttons), {p.pt(10), CrossAlign::Center, MainAlign::End});
					WrapOptions grid;
					grid.minChildWidth = p.pt(180);
					return wrap(std::move(buttons), grid);
				});
		}
		std::vector<Element> buttons;
		for (const auto &item : items)
		{
			ButtonOptions options;
			options.primary = item.primary;
			options.shortcut = item.shortcut;
			options.enabled = item.enabled;
			buttons.push_back(constrained({p.pt(90), 0, Constraints::Unbounded, Constraints::Unbounded},
										  padding(Insets::symmetric(p.pt(10), 0), button(item.key, item.label, item.action, options))));
		}
		return row(std::move(buttons), {p.pt(10), CrossAlign::Center, MainAlign::End});
	}
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
	if (!p.touch)
	{
		// The classic 640x480 paper panel (plus its 12-point margin), centered.
		parts.push_back(expanded(body));
		parts.push_back(actionRow);
		CardOptions cardOptions;
		cardOptions.padding = p.pt(12);
		const int w = std::min(p.safe.w - 2 * p.pt(8), p.pt(std::max(640.0, maxWidthPoints) + 24));
		const int h = std::min(p.safe.h - 2 * p.pt(8), p.pt(504));
		return center(sized({w, h}, card(column(std::move(parts), {p.pt(12)}), cardOptions)));
	}
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
			event.type = phase == PointerPhase::Down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
			event.button.button = SDL_BUTTON_LEFT;
			event.button.x = at.x;
			event.button.y = at.y;
		}
		else if (phase == PointerPhase::Move)
		{
			event.type = SDL_EVENT_MOUSE_MOTION;
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
		motion.type = SDL_EVENT_MOUSE_MOTION;
		motion.motion.x = widget->getLeft() + local.x;
		motion.motion.y = widget->getTop() + local.y;
		widget->handlePreviewEvent(&motion);
		SDL_Event wheel{};
		wheel.type = SDL_EVENT_MOUSE_WHEEL;
		wheel.wheel.y = direction;
		widget->handlePreviewEvent(&wheel);
	};
	auto element = canvas(key, {int(points), int(points)},
						  [widget](Canvas &c, Rect r, const Frame &)
						  {
							  widget->setScreenRectangle(r.x, r.y, r.w, r.h);
							  if (auto *surface = c.surface()) // null on a recording canvas
								  widget->paint(surface);
						  },
						  options);
	if (flexible)
		element->flex = 1;
	return element;
}
} // namespace Glob2UI
