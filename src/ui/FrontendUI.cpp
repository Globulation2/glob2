// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/FrontendUI.h"
#include "ui/ThemeCatalog.h"
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

const Theme &frontendTheme()
{
	return menuTheme();
}

const Theme &inGameTheme()
{
	return gameTheme();
}

const Theme &themeFor(Surface surface)
{
	// The one table that decides which look each kind of surface wears.
	switch (surface)
	{
	case Surface::Match:
	case Surface::Editor:
		return inGameTheme();
	case Surface::Frontend:
	case Surface::Results:
	default:
		return frontendTheme();
	}
}

bool touchPresentation()
{
	return phonePresentationRequested();
}

std::string tr(const std::string &key)
{
	return GAGCore::Toolkit::getStringTable()->getString(key);
}

Screen::Screen() : UIScreen(themeFor(Surface::Frontend)) {}
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

Dialog::Dialog() : UIDialog(themeFor(Surface::Frontend)) {}

InGameDialog::InGameDialog(Surface surface) : UIDialog(themeFor(surface)) {}

Element InGameDialog::dialogActions(std::vector<MenuAction> items, const Presentation &p) const
{
	return actions(std::move(items), p);
}

Rect InGameDialog::insetAvailable(const Presentation &p, const Metrics &m)
{
	// UIDialog adds its internal padding back when painting the panel.
	// Reserve the outer gutter separately so it cannot be consumed by content.
	return UIDialog::available(p, m).inset(p.pt(16));
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

} // namespace

Element pageTitle(const std::string &text)
{
	return paragraph(text, {FontRole::Heading});
}

Element hint(const std::string &text)
{
	return paragraph(text, {FontRole::Support, true});
}

Element menu(const std::string &titleText, std::vector<MenuAction> items, const Presentation &p)
{
	if (!p.touch)
	{
		// A narrow panel of 300-point body-font buttons stacked from the top, the
		// escape route pinned at the bottom.
		std::vector<Element> parts;
		Element escape;
		for (const auto &item : items)
		{
			ButtonOptions options;
			options.shortcut = item.shortcut;
			options.enabled = item.enabled;
			options.minHeight = 36;
			auto element = button(item.key, item.label, item.action, options);
			if (item.shortcut == SDLK_ESCAPE && !escape)
				escape = element;
			else
				parts.push_back(element);
		}
		// The panel fits its buttons; it scrolls when larger text makes them outgrow the window.
		const double contentPoints = 24 + (titleText.empty() ? 0 : 40) + 46.0 * double(parts.size()) + (escape ? 56 : 0);
		std::vector<Element> body;
		if (!titleText.empty())
			body.push_back(pageTitle(titleText));
		body.push_back(expanded(scroll("menu/items", column(std::move(parts), {p.pt(10)}))));
		if (escape)
			body.push_back(escape);
		CardOptions cardOptions;
		cardOptions.padding = p.pt(12);
		const int panelHeight = std::min(p.safe.h - 2 * p.pt(8), p.pt(contentPoints * std::max(1.0, p.textGrowth)));
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
		parts.push_back(pageTitle(titleText));
	parts.push_back(scroll("menu/scroll", wrap(std::move(buttons), grid)));
	return center(maxWidth(p.pt(640), card(column(std::move(parts), {p.pt(12)}))));
}

Element actions(std::vector<MenuAction> items, const Presentation &p)
{
	if (!p.touch)
	{
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
		return adaptive([buttons, gap = p.pt(10)](const LayoutContext &ctx, Size available)
						{
							int width = buttons.empty() ? 0 : gap * (int(buttons.size()) - 1);
							int widest = 1;
							for (const auto &button : buttons)
							{
								const int naturalWidth = button->measure(ctx, {}).w;
								width += naturalWidth;
								widest = std::max(widest, naturalWidth);
							}
							if (width <= available.w)
								return row(buttons, {gap, CrossAlign::Center, MainAlign::End});
							// Large text and long translated labels need more than one row.
							return wrap(buttons, {gap, widest});
						});
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
		parts.push_back(pageTitle(titleText));
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

// The HUD wears the in-game theme; see InGameTouchTheme.h.
#define HUD_TOKEN(name) \
	const GAGCore::Color &InGameTouchTheme::name() { return Glob2UI::gameTheme().hud.name; }
HUD_TOKEN(ink)
HUD_TOKEN(paper)
HUD_TOKEN(field)
HUD_TOKEN(selected)
HUD_TOKEN(border)
HUD_TOKEN(readout)
HUD_TOKEN(dialTrack)
HUD_TOKEN(dialFill)
HUD_TOKEN(dialPadFill)
HUD_TOKEN(destroy)
HUD_TOKEN(erasePreview)
#undef HUD_TOKEN
