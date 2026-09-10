// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "FrontendTheme.h"
#include <GUIBase.h>
#include <Toolkit.h>
#include <algorithm>
#include <functional>
#include <map>

// Lobby-scoped composed controls. Stable string identities preserve focus
// across model edits; scroll and popup state belong to the view, never to the
// game draft.
class LobbyControls : public GAGGUI::RectangularWidget
{
  public:
	using Callback = std::function<void()>;
	struct Hit
	{
		std::string id;
		SDL_Rect box, clip;
		Callback action;
		bool enabled;
		int region;
	};
	struct Region
	{
		SDL_Rect box{};
		int offset = 0, maximum = 0;
	};
	std::function<void()> render;
	std::vector<Hit> hits;
	std::map<std::string, std::function<void(int)>> sliders, sliderKeys;
	std::map<int, Region> regions;
	std::string focus, pressed;
	int activeRegion = -1, hoverX = 0, hoverY = 0;
	GAGCore::Color ink = FrontendPalette::ink, muted = FrontendPalette::muted, panel = FrontendPalette::gel,
		gold = FrontendPalette::gold, line = FrontendPalette::ink;
	struct Popup
	{
		bool open = false;
		SDL_Rect anchor{};
		std::vector<std::string> options;
		std::vector<bool> enabled;
		std::function<void(int)> apply;
		int selected = 0, first = 0;
		std::string help;
	} popup;
	LobbyControls()
	{
		hAlignFlag = ALIGN_LEFT;
		vAlignFlag = ALIGN_TOP;
	}
	GAGCore::DrawableSurface *surface() { return parent->getSurface(); }
	static bool inside(SDL_Rect r, int px, int py)
	{
		return px >= r.x && py >= r.y && px < r.x + r.w && py < r.y + r.h;
	}
	SDL_Rect clip()
	{
		int a, b, c, d;
		surface()->getClipRect(&a, &b, &c, &d);
		return {a, b, c, d};
	}
	void box(SDL_Rect r, GAGCore::Color color, int radius = 5)
	{
		FrontendTheme::rounded(surface(), r.x, r.y, r.w, r.h, radius, color);
	}
	void text(int x, int y, std::string value, const char *font = "standard", int width = 10000,
			  bool quiet = false)
	{
		auto f = GAGCore::Toolkit::getFont(font);
		if (f->getStringWidth(value) > width)
		{
			while (!value.empty() && f->getStringWidth(value + "...") > width)
			{
				size_t i = value.size() - 1;
				while (i > 0 && (static_cast<unsigned char>(value[i]) & 0xc0) == 0x80)
					--i;
				value.resize(i);
			}
			value += "...";
		}
		f->pushStyle(GAGCore::Font::Style(GAGCore::Font::STYLE_NORMAL, quiet ? muted : ink));
		surface()->drawString(x, y, f, value);
		f->popStyle();
	}
	int paragraph(int x, int y, int width, const std::string &value, const char *font = "little")
	{
		auto f = GAGCore::Toolkit::getFont(font);
		int row = f->getStringHeight("Ag") + 3, start = y;
		std::string lineText, word;
		auto flush = [&]
		{
			text(x, y, lineText, font, width, true);
			y += row;
			lineText.clear();
		};
		for (size_t i = 0; i <= value.size(); ++i)
		{
			char c = i < value.size() ? value[i] : '\n';
			if (c == ' ' || c == '\n')
			{
				if (!word.empty())
				{
					if (!lineText.empty() && f->getStringWidth(lineText + " " + word) > width)
						flush();
					if (!lineText.empty())
						lineText += ' ';
					lineText += word;
					word.clear();
				}
				if (c == '\n')
					flush();
			}
			else
				word += c;
		}
		return y - start;
	}
	void button(const std::string &id, SDL_Rect r, const std::string &label, Callback action,
				bool selected = false, bool enabled = true, bool quiet = false,
				const char *font = "standard")
	{
		SDL_Rect clipping = clip();
		hits.push_back({id, r, clipping, std::move(action), enabled, activeRegion});
		if (selected)
			box(r, gold);
		else if (!quiet)
			box(r, enabled ? panel : FrontendPalette::gelDisabled);
		if (focus == id ||
			(!popup.open && inside(r, hoverX, hoverY) && inside(clipping, hoverX, hoverY)))
			surface()->drawRect(r.x, r.y, r.w, r.h, focus == id ? ink : line);
		int fh = GAGCore::Toolkit::getFont(font)->getStringHeight("Ag");
		text(r.x + 9, r.y + (r.h - fh) / 2, label, font, r.w - 18, !enabled || quiet);
	}
	void dropdown(const std::string &id, SDL_Rect r, const std::vector<std::string> &options,
				  int selected, std::function<void(int)> apply,
				  const std::vector<bool> &enabled = {}, const std::string &help = "")
	{
		auto label = selected >= 0 && selected < int(options.size()) ? options[selected] : "";
		button(id, r, label,
			   [=, this]
			   {
				   popup = {true, r, options, enabled, apply, std::max(0, selected), 0, help};
				   revealPopup();
			   });
		surface()->drawRect(r.x, r.y, r.w, r.h, line);
		// The right edge is reserved for the disclosure chevron.
		box({r.x + r.w - 18, r.y + 4, 14, r.h - 8}, panel, 2);
		text(r.x + r.w - 15, r.y + (r.h - 12) / 2, "v", "little", 12, true);
	}
	void segments(const std::string &id, SDL_Rect r, const std::vector<std::string> &options,
				  int selected, std::function<void(int)> apply,
				  const std::vector<bool> &enabled = {}, const char *font = "standard")
	{
		int n = options.size();
		for (int i = 0; i < n; ++i)
			button(
				id + "/" + std::to_string(i), {r.x + i * r.w / n, r.y, r.w / n - 3, r.h},
				options[i], [=] { apply(i); }, i == selected, enabled.empty() || enabled[i], false,
				font);
	}
	void stepper(const std::string &id, SDL_Rect r, int value, int lo, int hi,
				 std::function<void(int)> apply)
	{
		button(id + "/-", {r.x, r.y, 30, r.h}, "-", [=] { apply(value - 1); }, false, value > lo);
		text(r.x + 42, r.y + 5, std::to_string(value));
		button(
			id + "/+", {r.x + r.w - 30, r.y, 30, r.h}, "+", [=] { apply(value + 1); }, false,
			value < hi);
	}
	void slider(const std::string &id, SDL_Rect r, int value, int lo, int hi,
				std::function<void(int)> apply)
	{
		auto change = [=](int px)
		{ apply(std::clamp(lo + (px - r.x) * (hi - lo) / std::max(1, r.w - 12), lo, hi)); };
		button(id, r, "", [this, change] { change(hoverX); }, false, true, true);
		sliders[id] = change;
		sliderKeys[id] = [=](int delta) { apply(std::clamp(value + delta, lo, hi)); };
		box({r.x, r.y + r.h / 2 - 2, r.w, 4}, line, 2);
		int pos = (value - lo) * (r.w - 12) / std::max(1, hi - lo);
		box({r.x, r.y + r.h / 2 - 2, pos + 6, 4}, muted, 2);
		box({r.x + pos, r.y + 3, 12, r.h - 6}, gold, 4);
	}
	void beginRegion(int id, SDL_Rect r)
	{
		activeRegion = id;
		regions[id].box = r;
		surface()->setClipRect(r.x, r.y, r.w, r.h);
	}
	void endRegion(int contentHeight)
	{
		int regionId = activeRegion;
		auto &reg = regions[activeRegion];
		reg.maximum = std::max(0, contentHeight - reg.box.h);
		reg.offset = std::clamp(reg.offset, 0, reg.maximum);
		surface()->setClipRect();
		activeRegion = -1;
		if (reg.maximum > 0)
		{
			auto b = reg.box;
			box({b.x + b.w - 5, b.y, 4, b.h}, line, 2);
			int thumb = std::max(24, b.h * b.h / (b.h + reg.maximum));
			box({b.x + b.w - 5, b.y + (b.h - thumb) * reg.offset / reg.maximum, 4, thumb}, muted,
				2);
			std::string id = "scroll/" + std::to_string(regionId);
			hits.push_back({id,
							{b.x + b.w - 10, b.y, 10, b.h},
							clip(),
							[this, regionId, b, thumb]
							{
								auto &r = regions[regionId];
								r.offset = std::clamp((hoverY - b.y - thumb / 2) * r.maximum /
														  std::max(1, b.h - thumb),
													  0, r.maximum);
							},
							true,
							-1});
		}
	}
	void revealPopup()
	{
		popup.first = std::clamp(popup.first, std::max(0, popup.selected - 7), popup.selected);
	}
	SDL_Rect popupRect()
	{
		int pw = popup.help.empty() ? popup.anchor.w : std::max(330, popup.anchor.w);
		for (auto &s : popup.options)
			pw = std::max(pw, GAGCore::Toolkit::getFont("standard")->getStringWidth(s) + 30);
		pw = std::min(pw, surface()->getW() - 32);
		int ph = std::min(8, int(popup.options.size())) * 30 + 12 + (popup.help.empty() ? 0 : 48);
		int px = std::clamp(popup.anchor.x, 16, surface()->getW() - pw - 16);
		int py = popup.anchor.y + popup.anchor.h + 4;
		if (py + ph > surface()->getH() - 12)
			py = std::max(12, popup.anchor.y - ph - 4);
		return {px, py, pw, ph};
	}
	void paint() override
	{
		hits.clear();
		sliders.clear();
		sliderKeys.clear();
		activeRegion = -1;
		surface()->setClipRect();
		render();
		surface()->setClipRect();
		if (popup.open)
		{
			auto r = popupRect();
			box({r.x + 3, r.y + 4, r.w, r.h}, GAGCore::Color(FrontendPalette::scrim.r, FrontendPalette::scrim.g, FrontendPalette::scrim.b, 100));
			box(r, panel);
			surface()->drawRect(r.x, r.y, r.w, r.h, muted);
			for (int n = 0; n < 8 && n + popup.first < int(popup.options.size()); ++n)
			{
				int i = n + popup.first;
				bool ok = popup.enabled.empty() || popup.enabled[i];
				if (i == popup.selected)
					box({r.x + 4, r.y + 6 + n * 30, r.w - 8, 29}, gold, 3);
				text(r.x + 10, r.y + 12 + n * 30, popup.options[i], "standard", r.w - 20, !ok);
			}
			if (popup.options.size() > 8)
			{
				int thumb = 240 * 8 / int(popup.options.size());
				box({r.x + r.w - 5, r.y + 6, 3, 240}, line, 1);
				box({r.x + r.w - 5,
					 r.y + 6 + (240 - thumb) * popup.first / (int(popup.options.size()) - 8), 3,
					 thumb},
					muted, 1);
			}
			if (!popup.help.empty())
				paragraph(r.x + 10, r.y + r.h - 44, r.w - 20, popup.help);
		}
	}
	void resetFocus()
	{
		focus.clear();
		pressed.clear();
		popup.open = false;
	}
	void scroll(int id, int delta)
	{
		auto &r = regions[id];
		r.offset = std::clamp(r.offset + delta, 0, r.maximum);
	}
	bool handle(SDL_Event *e)
	{
		if (popup.open)
		{
			if (e->type == SDL_KEYDOWN)
			{
				auto k = e->key.keysym.sym;
				if (k == SDLK_ESCAPE)
					popup.open = false;
				else if (k == SDLK_UP || k == SDLK_DOWN)
				{
					popup.selected = std::clamp(popup.selected + (k == SDLK_UP ? -1 : 1), 0,
												int(popup.options.size()) - 1);
					revealPopup();
				}
				else if (k == SDLK_RETURN || k == SDLK_SPACE)
				{
					if (popup.enabled.empty() || popup.enabled[popup.selected])
					{
						auto apply = popup.apply;
						int v = popup.selected;
						popup.open = false;
						apply(v);
					}
				}
				return true;
			}
			if (e->type == SDL_MOUSEWHEEL)
			{
				popup.first = std::clamp(popup.first - e->wheel.y, 0,
										 std::max(0, int(popup.options.size()) - 8));
				return true;
			}
			if (e->type == SDL_MOUSEBUTTONDOWN)
			{
				auto r = popupRect();
				if (!inside(r, e->button.x, e->button.y))
				{
					popup.open = false;
					pressed.clear();
				}
				return true;
			}
			if (e->type == SDL_MOUSEBUTTONUP)
			{
				auto r = popupRect();
				int i = (e->button.y - r.y - 6) / 30 + popup.first;
				if (inside(r, e->button.x, e->button.y) && e->button.y >= r.y + 6 && i >= 0 &&
					i < int(popup.options.size()) && i < popup.first + 8 &&
					(popup.enabled.empty() || popup.enabled[i]))
				{
					auto apply = popup.apply;
					popup.open = false;
					apply(i);
				}
				return true;
			}
			return true;
		}
		if (e->type == SDL_MOUSEMOTION)
		{
			hoverX = e->motion.x;
			hoverY = e->motion.y;
			if (sliders.count(pressed))
			{
				auto apply = sliders[pressed];
				apply(hoverX);
				return true;
			}
			if (pressed.find("scroll/") == 0)
				for (auto &hit : hits)
					if (hit.id == pressed)
					{
						auto action = hit.action;
						action();
						return true;
					}
		}
		if (e->type == SDL_MOUSEWHEEL)
		{
			for (auto &p : regions)
				if (inside(p.second.box, hoverX, hoverY))
				{
					scroll(p.first, -e->wheel.y * 36);
					return true;
				}
		}
		if (e->type == SDL_MOUSEBUTTONDOWN || e->type == SDL_MOUSEBUTTONUP)
		{
			int px = e->button.x, py = e->button.y;
			hoverX = px;
			hoverY = py;
			if (e->type == SDL_MOUSEBUTTONDOWN)
				pressed.clear();
			for (auto &hit : hits)
				if (inside(hit.box, px, py) && inside(hit.clip, px, py))
				{
					if (e->type == SDL_MOUSEBUTTONDOWN)
					{
						focus = hit.id;
						pressed = hit.id;
						if (sliders.count(pressed))
						{
							auto apply = sliders[pressed];
							apply(px);
						}
					}
					else
					{
						bool matched = pressed == hit.id;
						pressed.clear();
						if (matched && hit.enabled)
						{
							auto action = hit.action;
							action();
						}
					}
					return true;
				}
			if (e->type == SDL_MOUSEBUTTONUP)
				pressed.clear();
		}
		if (e->type == SDL_KEYDOWN)
		{
			auto k = e->key.keysym.sym;
			if ((k == SDLK_LEFT || k == SDLK_RIGHT) && sliderKeys.count(focus))
			{
				auto apply = sliderKeys[focus];
				apply(k == SDLK_LEFT ? -1 : 1);
				return true;
			}
			if (k == SDLK_TAB)
			{
				if (hits.empty())
					return true;
				int i = -1;
				for (size_t j = 0; j < hits.size(); ++j)
					if (hits[j].id == focus)
						i = j;
				int direction = e->key.keysym.mod & KMOD_SHIFT ? -1 : 1;
				if (i == -1 && direction < 0)
					i = 0;
				for (size_t j = 0; j < hits.size(); ++j)
				{
					i = (i + direction + hits.size()) % hits.size();
					if (hits[i].enabled)
						break;
				}
				auto hit = hits[i];
				focus = hit.id;
				if (hit.region >= 0)
				{
					auto &r = regions[hit.region];
					if (hit.box.y < r.box.y)
						scroll(hit.region, hit.box.y - r.box.y);
					else if (hit.box.y + hit.box.h > r.box.y + r.box.h)
						scroll(hit.region, hit.box.y + hit.box.h - r.box.y - r.box.h);
				}
				return true;
			}
			if (k == SDLK_RETURN || k == SDLK_SPACE)
				for (auto &hit : hits)
					if (hit.id == focus)
					{
						if (hit.enabled)
						{
							auto action = hit.action;
							action();
						}
						return true;
					}
			if (k == SDLK_PAGEDOWN || k == SDLK_PAGEUP)
			{
				for (auto &p : regions)
					if (p.second.box.h > 0 && p.second.maximum > 0)
						scroll(p.first, (k == SDLK_PAGEUP ? -1 : 1) * p.second.box.h);
				return true;
			}
		}
		return false;
	}
};
