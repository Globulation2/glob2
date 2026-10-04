// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <ui/Theme.h>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Interface themes described in JSON (see docs/development/ui-framework.md,
// "Themes"). Shipped themes are listed in data/themes/index.json; players can
// add more as <user data>/themes/<id>.json. Menus and matches each wear one.
namespace Glob2UI
{
struct ThemeSource
{
	// File the theme came from, for error messages.
	std::string path;
	std::string text;
	bool shipped = true;
};

class ThemeCatalog
{
  public:
	static constexpr const char *index = "data/themes/index.json";
	static constexpr const char *menuDefault = "light";
	static constexpr const char *gameDefault = "dark";

	// Resolves every source (inheritance through "extends" included) on top of
	// the built-in light and dark themes, which a broken file never removes.
	static ThemeCatalog parse(const std::vector<ThemeSource> &sources, std::vector<std::string> &errors);
	// Shipped and player themes, read once the file manager is up.
	static const ThemeCatalog &shared();
	// The two compiled-in themes, identical to the shipped light.json and dark.json.
	static GAGGUI::ui::Theme builtinLight();
	static GAGGUI::ui::Theme builtinDark();

	const GAGGUI::ui::Theme *find(std::string_view id) const;
	// The theme with `id`, or the one with `fallback` when it is unknown.
	const GAGGUI::ui::Theme &resolve(std::string_view id, std::string_view fallback) const;
	const std::vector<GAGGUI::ui::Theme> &themes() const { return list; }

  private:
	std::vector<GAGGUI::ui::Theme> list;
};

// "#rrggbb" or "#rrggbbaa".
std::optional<GAGCore::Color> parseThemeColor(std::string_view text);
std::string formatThemeColor(const GAGCore::Color &color);
// WCAG contrast ratio of two opaque colours, 1 to 21.
double contrastRatio(const GAGCore::Color &a, const GAGCore::Color &b);

// Puts the named themes into the menu and in-game slots that every screen,
// dialog and HUD reads from, falling back to the defaults for unknown ids.
void applyThemes(const std::string &menuId, const std::string &gameId);
// The live menu and in-game themes (frontendTheme() and inGameTheme() alias them).
const GAGGUI::ui::Theme &menuTheme();
const GAGGUI::ui::Theme &gameTheme();
// Changes whenever applyThemes() changes a slot, for caches derived from a theme.
unsigned themeGeneration();
} // namespace Glob2UI
