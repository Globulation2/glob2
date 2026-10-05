// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/ThemeCatalog.h"
#include "ui/ThemePainters.h"
#include <FileManager.h>
#include <Stream.h>
#include <Toolkit.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <set>

using Json = nlohmann::json;
using GAGCore::Color;
using GAGGUI::ui::Backdrop;
using GAGGUI::ui::HudPalette;
using GAGGUI::ui::Palette;
using GAGGUI::ui::Theme;

namespace Glob2UI
{
namespace
{
// Every token a theme file may set, by its JSON name.
const std::pair<const char *, Color Palette::*> paletteTokens[] = {
	{"ink", &Palette::ink},
	{"muted", &Palette::muted},
	{"paper", &Palette::paper},
	{"panel", &Palette::panel},
	{"field", &Palette::field},
	{"rail", &Palette::rail},
	{"line", &Palette::line},
	{"accent", &Palette::accent},
	{"accentInk", &Palette::accentInk},
	{"selected", &Palette::selected},
	{"hover", &Palette::hover},
	{"focus", &Palette::focus},
	{"scrim", &Palette::scrim},
	{"disabled", &Palette::disabled},
	{"danger", &Palette::danger},
	{"success", &Palette::success},
	{"warning", &Palette::warning},
	{"shadow", &Palette::shadow},
	{"pressed", &Palette::pressed},
	{"backdrop", &Palette::backdrop},
	{"neutral", &Palette::neutral},
	{"placeholder", &Palette::placeholder},
};
const std::pair<const char *, Color HudPalette::*> hudTokens[] = {
	{"ink", &HudPalette::ink},
	{"paper", &HudPalette::paper},
	{"field", &HudPalette::field},
	{"selected", &HudPalette::selected},
	{"border", &HudPalette::border},
	{"readout", &HudPalette::readout},
	{"dialTrack", &HudPalette::dialTrack},
	{"dialFill", &HudPalette::dialFill},
	{"dialPadFill", &HudPalette::dialPadFill},
	{"destroy", &HudPalette::destroy},
	{"erasePreview", &HudPalette::erasePreview},
};
const std::pair<const char *, double Theme::*> metricTokens[] = {
	{"radius", &Theme::radius},
	{"focusRing", &Theme::focusRing},
};
const std::pair<const char *, Backdrop::Kind> backdropKinds[] = {
	{"colony", Backdrop::Kind::Colony},
	{"image", Backdrop::Kind::Image},
	{"terrain", Backdrop::Kind::Terrain},
	{"solid", Backdrop::Kind::Solid},
};

// Asset paths stay inside the game's data tree.
bool safeAssetPath(const std::string &path)
{
	return path.rfind("data/", 0) == 0 && path.find("..") == std::string::npos;
}

template <typename Struct, std::size_t N>
void readColors(const Json &object, const std::pair<const char *, Color Struct::*> (&tokens)[N],
				Struct &target, const std::string &where, std::vector<std::string> &errors)
{
	if (!object.is_object())
	{
		errors.push_back(where + ": expected an object");
		return;
	}
	for (const auto &[key, value] : object.items())
	{
		const auto token = std::find_if(std::begin(tokens), std::end(tokens),
										[&](const auto &t) { return key == t.first; });
		if (token == std::end(tokens))
		{
			errors.push_back(where + "." + key + ": unknown token");
			continue;
		}
		const auto color = value.is_string() ? parseThemeColor(value.template get<std::string>()) : std::nullopt;
		if (!color)
			errors.push_back(where + "." + key + ": expected \"#rrggbb\" or \"#rrggbbaa\"");
		else
			target.*(token->second) = *color;
	}
}

// Applies one theme file's settings over `theme` (its parent's values).
void apply(const Json &root, Theme &theme, const std::string &where, std::vector<std::string> &errors)
{
	if (auto name = root.find("name"); name != root.end() && name->is_string())
		theme.name = name->get<std::string>();
	if (auto palette = root.find("palette"); palette != root.end())
		readColors(*palette, paletteTokens, theme.palette, where + " palette", errors);
	if (auto hud = root.find("hud"); hud != root.end())
		readColors(*hud, hudTokens, theme.hud, where + " hud", errors);
	if (auto metrics = root.find("metrics"); metrics != root.end() && metrics->is_object())
		for (const auto &[key, value] : metrics->items())
		{
			const auto token = std::find_if(std::begin(metricTokens), std::end(metricTokens),
											[&](const auto &t) { return key == t.first; });
			if (token == std::end(metricTokens) || !value.is_number() || value.get<double>() < 0 || value.get<double>() > 32)
				errors.push_back(where + " metrics." + key + ": unknown token or out of range");
			else
				theme.*(token->second) = value.get<double>();
		}
	if (auto backdrop = root.find("backdrop"); backdrop != root.end() && backdrop->is_object())
	{
		auto &b = theme.backdrop;
		if (auto kind = backdrop->find("kind"); kind != backdrop->end())
		{
			const auto known = std::find_if(std::begin(backdropKinds), std::end(backdropKinds),
											[&](const auto &k) { return kind->is_string() && *kind == k.first; });
			if (known == std::end(backdropKinds))
				errors.push_back(where + " backdrop.kind: expected colony, image, terrain or solid");
			else
				b.kind = known->second;
		}
		for (auto [key, field] : {std::pair{"image", &Backdrop::image}, std::pair{"wordmark", &Backdrop::wordmark}})
			if (auto value = backdrop->find(key); value != backdrop->end())
			{
				if (value->is_string() && (value->get<std::string>().empty() || (safeAssetPath(*value) && value->get<std::string>().ends_with(".webp"))))
					b.*field = value->get<std::string>();
				else
					errors.push_back(where + " backdrop." + key + ": expected a WebP path under data/");
			}
		if (auto veil = backdrop->find("veil"); veil != backdrop->end())
		{
			const auto color = veil->is_string() ? parseThemeColor(veil->get<std::string>()) : std::nullopt;
			if (color)
				b.veil = *color;
			else
				errors.push_back(where + " backdrop.veil: expected a colour");
		}
		if (b.kind == Backdrop::Kind::Image && b.image.empty())
			errors.push_back(where + " backdrop: an image backdrop needs \"image\"");
	}
	if (auto buttons = root.find("buttons"); buttons != root.end() && buttons->is_object())
	{
		const std::string kind = buttons->value("kind", "palette");
		const std::string sprite = buttons->value("sprite", "data/gfx/guitheme");
		if (kind == "palette")
			theme.buttonPainter = nullptr;
		else if (kind == "sprite" && safeAssetPath(sprite))
			theme.buttonPainter = spriteButtonPainter(sprite);
		else
			errors.push_back(where + " buttons: expected kind \"palette\" or \"sprite\" with a data/ sprite");
	}
}

bool validId(const std::string &id)
{
	return !id.empty() && id.size() <= 40 &&
		   std::all_of(id.begin(), id.end(), [](char c)
					   { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; });
}

double channel(int value)
{
	const double c = value / 255.0;
	return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

std::optional<std::string> readText(const std::string &path)
{
	GAGCore::InputLineStream input(GAGCore::Toolkit::getFileManager()->openInputStreamBackend(path));
	if (input.isEndOfStream())
		return std::nullopt;
	std::string text;
	while (!input.isEndOfStream())
		text += input.readLine() + "\n";
	return text;
}

Theme &menuSlot()
{
	static Theme theme = ThemeCatalog::builtinLight();
	return theme;
}
Theme &gameSlot()
{
	static Theme theme = ThemeCatalog::builtinDark();
	return theme;
}
unsigned generation = 0;
} // namespace

std::optional<Color> parseThemeColor(std::string_view text)
{
	if ((text.size() != 7 && text.size() != 9) || text[0] != '#')
		return std::nullopt;
	int parts[4] = {0, 0, 0, 255};
	for (std::size_t i = 1; i < text.size(); i += 2)
	{
		int value = 0;
		for (char c : text.substr(i, 2))
		{
			const int digit = c >= '0' && c <= '9' ? c - '0'
							  : c >= 'a' && c <= 'f' ? c - 'a' + 10
							  : c >= 'A' && c <= 'F' ? c - 'A' + 10
													 : -1;
			if (digit < 0)
				return std::nullopt;
			value = value * 16 + digit;
		}
		parts[i / 2] = value;
	}
	return Color(parts[0], parts[1], parts[2], parts[3]);
}

std::string formatThemeColor(const Color &color)
{
	char buffer[10];
	if (color.a == 255)
		std::snprintf(buffer, sizeof buffer, "#%02x%02x%02x", color.r, color.g, color.b);
	else
		std::snprintf(buffer, sizeof buffer, "#%02x%02x%02x%02x", color.r, color.g, color.b, color.a);
	return buffer;
}

double contrastRatio(const Color &a, const Color &b)
{
	auto luminance = [](const Color &c)
	{ return 0.2126 * channel(c.r) + 0.7152 * channel(c.g) + 0.0722 * channel(c.b); };
	const double x = luminance(a) + 0.05, y = luminance(b) + 0.05;
	return std::max(x, y) / std::min(x, y);
}

Theme ThemeCatalog::builtinLight()
{
	Theme t;
	t.id = "light";
	t.name = "Light";
	t.fonts = {"front-title", "menu", "standard", "little", "front-caption"};
	t.touchFonts = {"front-title", "menu", "frontend-body", "frontend-support", "front-caption"};
	// Every theme shares one touch text size, so menus, dialogs over gameplay and
	// the end-of-game sheet read alike; the player's preference multiplies it.
	t.touchTextScale = 1.15;
	auto &h = t.hud;
	h.ink = Color(36, 69, 49);
	h.paper = Color(240, 241, 223, 232);
	h.field = Color(249, 250, 240, 240);
	h.selected = Color(228, 199, 121, 245);
	h.border = Color(160, 124, 50);
	h.readout = Color(230, 231, 210, 240);
	h.dialTrack = Color(225, 231, 209, 230);
	h.dialFill = Color(180, 140, 60, 200);
	h.dialPadFill = Color(206, 216, 190, 245);
	h.destroy = Color(236, 200, 190, 240);
	h.erasePreview = Color(240, 241, 223, 140);
	return t;
}

Theme ThemeCatalog::builtinDark()
{
	// The in-match look: aubergine panels, cream ink and gold lines, so dialogs
	// belong to the map they sit on. Fonts and sizes are the light theme's.
	Theme t = builtinLight();
	t.id = "dark";
	t.name = "Dark";
	t.hud = HudPalette{};
	auto &c = t.palette;
	c.ink = Color(249, 232, 187);
	c.muted = Color(204, 188, 152);
	c.paper = Color(43, 28, 66);
	c.panel = Color(43, 28, 66, 244);
	c.field = Color(65, 43, 88);
	c.rail = Color(55, 36, 78);
	c.line = Color(199, 165, 87);
	c.accent = Color(199, 165, 87);
	c.accentInk = Color(30, 18, 40);
	c.selected = Color(114, 78, 111);
	c.hover = Color(92, 62, 116);
	c.focus = Color(255, 214, 120);
	c.scrim = Color(10, 6, 20, 140);
	c.disabled = Color(52, 38, 70);
	c.shadow = Color(10, 6, 20, 60);
	c.pressed = Color(255, 214, 120, 50);
	c.success = Color(120, 220, 120);
	c.warning = Color(255, 214, 120);
	c.danger = Color(255, 110, 100);
	c.neutral = Color(120, 104, 140);
	c.placeholder = Color(70, 50, 92);
	// Behind dark menus the colony is dimmed rather than washed with paper.
	c.backdrop = Color(17, 10, 28);
	t.backdrop.veil = Color(17, 10, 28, 96);
	return t;
}

ThemeCatalog ThemeCatalog::parse(const std::vector<ThemeSource> &sources, std::vector<std::string> &errors)
{
	struct Pending
	{
		Json root;
		std::string where;
		bool shipped;
	};
	std::map<std::string, Pending> pending;
	std::vector<std::string> order;
	for (const auto &source : sources)
	{
		Json root = Json::parse(source.text.begin(), source.text.end(), nullptr, false);
		if (root.is_discarded() || !root.is_object())
		{
			errors.push_back(source.path + ": not a JSON object");
			continue;
		}
		if (root.value("schema", 0) != 1)
			errors.push_back(source.path + ": unsupported schema; expected \"schema\": 1");
		const std::string id = root.value("id", "");
		if (!validId(id))
		{
			errors.push_back(source.path + ": \"id\" must be lowercase letters, digits and dashes");
			continue;
		}
		if (auto existing = pending.find(id); existing != pending.end())
		{
			// A player's file never replaces a shipped theme.
			if (!(existing->second.shipped && !source.shipped))
				errors.push_back(source.path + ": duplicate theme id \"" + id + "\"");
			continue;
		}
		pending[id] = {std::move(root), source.path, source.shipped};
		order.push_back(id);
	}

	std::map<std::string, Theme> resolved;
	std::set<std::string> visiting;
	std::function<const Theme *(const std::string &)> resolve = [&](const std::string &id) -> const Theme *
	{
		if (auto done = resolved.find(id); done != resolved.end())
			return &done->second;
		const auto entry = pending.find(id);
		if (entry == pending.end())
		{
			if (id == "light")
				return &resolved.emplace(id, builtinLight()).first->second;
			if (id == "dark")
				return &resolved.emplace(id, builtinDark()).first->second;
			return nullptr;
		}
		if (!visiting.insert(id).second)
		{
			errors.push_back(entry->second.where + ": \"extends\" forms a cycle");
			return nullptr;
		}
		const auto &root = entry->second.root;
		Theme theme = builtinLight();
		if (auto parent = root.find("extends"); parent != root.end())
		{
			const Theme *base = parent->is_string() ? resolve(parent->get<std::string>()) : nullptr;
			if (base)
				theme = *base;
			else
				errors.push_back(entry->second.where + ": unknown \"extends\" theme");
		}
		theme.id = id;
		theme.name = id;
		apply(root, theme, entry->second.where, errors);
		visiting.erase(id);
		return &resolved.emplace(id, std::move(theme)).first->second;
	};

	ThemeCatalog catalog;
	// Built-ins missing from the files still lead the list.
	if (!pending.count("dark"))
		order.insert(order.begin(), "dark");
	if (!pending.count("light"))
		order.insert(order.begin(), "light");
	for (const auto &id : order)
		if (const Theme *theme = resolve(id))
			catalog.list.push_back(*theme);
	// Light and dark always exist, even when their files are broken.
	for (auto [id, make] : {std::pair{"light", &ThemeCatalog::builtinLight}, std::pair{"dark", &ThemeCatalog::builtinDark}})
		if (!catalog.find(id))
			catalog.list.push_back(make());
	return catalog;
}

const ThemeCatalog &ThemeCatalog::shared()
{
	static const ThemeCatalog builtins = []
	{
		std::vector<std::string> ignored;
		return parse({}, ignored);
	}();
	static std::optional<ThemeCatalog> loaded;
	if (loaded)
		return *loaded;
	auto *files = GAGCore::Toolkit::getFileManager();
	if (!files)
		return builtins;
	std::vector<std::string> errors;
	std::vector<ThemeSource> sources;
	const auto listing = readText(index);
	if (!listing)
	{
		// Data directories may still be added (--data-dir); look again next time.
		static bool reported = false;
		if (!reported)
			std::cerr << "themes: " << index << ": file not found" << std::endl;
		reported = true;
		return builtins;
	}
	Json root = Json::parse(listing->begin(), listing->end(), nullptr, false);
	if (!root.is_object() || !root.contains("themes") || !root["themes"].is_array())
		errors.push_back(std::string(index) + ": expected {\"themes\": [ids]}");
	else
		for (const auto &id : root["themes"])
		{
			if (!id.is_string() || !validId(id))
				continue;
			const std::string path = "data/themes/" + id.get<std::string>() + ".json";
			if (auto text = readText(path))
				sources.push_back({path, *text, true});
			else
				errors.push_back(path + ": file not found");
		}
	// Player themes sit in the writable themes directory.
	std::vector<std::string> own;
	if (files->initDirectoryListing("themes", "json"))
		for (std::string name; !(name = files->getNextDirectoryEntry()).empty();)
			own.push_back(name);
	std::sort(own.begin(), own.end());
	own.erase(std::unique(own.begin(), own.end()), own.end());
	for (const auto &name : own)
		if (auto text = readText("themes/" + name))
			sources.push_back({"themes/" + name, *text, false});
	loaded = parse(sources, errors);
	for (const auto &error : errors)
		std::cerr << "themes: " << error << std::endl;
	return *loaded;
}

const Theme *ThemeCatalog::find(std::string_view id) const
{
	for (const auto &theme : list)
		if (theme.id == id)
			return &theme;
	return nullptr;
}

const Theme &ThemeCatalog::resolve(std::string_view id, std::string_view fallback) const
{
	if (const Theme *theme = find(id))
		return *theme;
	if (const Theme *theme = find(fallback))
		return *theme;
	return list.front();
}

void applyThemes(const std::string &menuId, const std::string &gameId)
{
	const auto &catalog = ThemeCatalog::shared();
	const Theme &menu = catalog.resolve(menuId, ThemeCatalog::menuDefault);
	const Theme &game = catalog.resolve(gameId, ThemeCatalog::gameDefault);
	// Screens and dialogs hold references to the slots, so assign in place.
	if (menuSlot().id != menu.id || gameSlot().id != game.id)
		++generation;
	menuSlot() = menu;
	gameSlot() = game;
}

unsigned themeGeneration()
{
	return generation;
}

const Theme &menuTheme()
{
	return menuSlot();
}
const Theme &gameTheme()
{
	return gameSlot();
}
} // namespace Glob2UI
