// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "Settings.h"
#include "InGameTouchTheme.h"
#include "ui/FrontendUI.h"
#include "ui/ThemeCatalog.h"
#include <FileManager.h>
#include <Toolkit.h>
#include <filesystem>
#include <fstream>

using GAGGUI::ui::Theme;
using Glob2UI::ThemeCatalog;

namespace
{
bool same(const GAGCore::Color &a, const GAGCore::Color &b)
{
	return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// Every token of two themes, so a shipped file cannot drift from the compiled fallback.
void checkSameTokens(const Theme &file, const Theme &builtin)
{
#define TOKEN(group, name) CHECK_MESSAGE(same(file.group.name, builtin.group.name), #group "." #name)
	TOKEN(palette, ink); TOKEN(palette, muted); TOKEN(palette, paper); TOKEN(palette, panel);
	TOKEN(palette, field); TOKEN(palette, rail); TOKEN(palette, line); TOKEN(palette, accent);
	TOKEN(palette, accentInk); TOKEN(palette, selected); TOKEN(palette, hover); TOKEN(palette, focus);
	TOKEN(palette, scrim); TOKEN(palette, disabled); TOKEN(palette, danger); TOKEN(palette, success);
	TOKEN(palette, warning); TOKEN(palette, shadow); TOKEN(palette, pressed); TOKEN(palette, backdrop);
	TOKEN(palette, neutral); TOKEN(palette, placeholder);
	TOKEN(hud, ink); TOKEN(hud, paper); TOKEN(hud, field); TOKEN(hud, selected); TOKEN(hud, border);
	TOKEN(hud, readout); TOKEN(hud, dialTrack); TOKEN(hud, dialFill); TOKEN(hud, dialPadFill);
	TOKEN(hud, destroy); TOKEN(hud, erasePreview);
	TOKEN(backdrop, veil);
#undef TOKEN
	CHECK(file.backdrop.kind == builtin.backdrop.kind);
	CHECK(file.radius == builtin.radius);
	CHECK(file.fonts == builtin.fonts);
	CHECK(file.touchTextScale == builtin.touchTextScale);
}

GAGCore::Color over(const GAGCore::Color &top, const GAGCore::Color &under)
{
	// Composite a translucent colour over an opaque one.
	auto mix = [&](int a, int b) { return (a * top.a + b * (255 - top.a)) / 255; };
	return GAGCore::Color(mix(top.r, under.r), mix(top.g, under.g), mix(top.b, under.b));
}
} // namespace

TEST_SUITE("ThemeCatalog")
{
	TEST_CASE("colours parse and print as #rrggbb[aa]")
	{
		CHECK(same(*Glob2UI::parseThemeColor("#244531"), GAGCore::Color(36, 69, 49)));
		CHECK(same(*Glob2UI::parseThemeColor("#0F2719aB"), GAGCore::Color(15, 39, 25, 171)));
		for (const char *bad : {"", "#12345", "244531", "#12345g", "#1234567"})
			CHECK_FALSE(Glob2UI::parseThemeColor(bad));
		CHECK(Glob2UI::formatThemeColor(GAGCore::Color(255, 214, 120, 50)) == "#ffd67832");
		CHECK(Glob2UI::contrastRatio(GAGCore::Color(0, 0, 0), GAGCore::Color(255, 255, 255)) == doctest::Approx(21));
	}

	TEST_CASE("shipped themes load, and light and dark match the compiled defaults")
	{
		glob2test::HeadlessGlobals globals;
		const auto &catalog = ThemeCatalog::shared();
		for (const char *id : {"light", "dark", "classic", "ocean", "dune", "high-contrast"})
			CHECK_MESSAGE(catalog.find(id), id);
		CHECK(catalog.themes().front().id == "light");
		checkSameTokens(*catalog.find("light"), ThemeCatalog::builtinLight());
		checkSameTokens(*catalog.find("dark"), ThemeCatalog::builtinDark());
		const Theme &classic = *catalog.find("classic");
		CHECK(classic.backdrop.kind == GAGGUI::ui::Backdrop::Kind::Terrain);
		CHECK(classic.buttonPainter);
		CHECK(GAGCore::Toolkit::getFileManager()->exists(classic.backdrop.wordmark));
	}

	TEST_CASE("every shipped theme keeps text readable")
	{
		glob2test::HeadlessGlobals globals;
		for (const auto &theme : ThemeCatalog::shared().themes())
		{
			CAPTURE(theme.id);
			const auto &p = theme.palette;
			// Body text on paper, fields and opaque panels: WCAG AA.
			CHECK(Glob2UI::contrastRatio(p.ink, p.paper) >= 4.5);
			CHECK(Glob2UI::contrastRatio(p.ink, over(p.field, p.paper)) >= 4.5);
			CHECK(Glob2UI::contrastRatio(p.muted, p.paper) >= 3.0);
			// Labels on the default action and on HUD panels over a mid-tone map.
			CHECK(Glob2UI::contrastRatio(p.accentInk, p.accent) >= 4.5);
			const auto &h = theme.hud;
			CHECK(Glob2UI::contrastRatio(h.ink, over(h.paper, p.paper)) >= 4.5);
			CHECK(Glob2UI::contrastRatio(h.ink, over(h.field, p.paper)) >= 4.5);
		}
	}

	TEST_CASE("inheritance, player themes and broken files")
	{
		std::vector<std::string> errors;
		auto catalog = ThemeCatalog::parse(
			{{"data/themes/light.json", "{ not json", true},
			 {"themes/mine.json", R"({"schema":1,"id":"mine","name":"Mine","extends":"dark",
				"palette":{"accent":"#ff0000","bogus":"#000000","ink":"red"}})", false},
			 {"themes/light.json", R"({"schema":1,"id":"light","name":"Hijack"})", false},
			 {"themes/loop.json", R"({"schema":1,"id":"loop","extends":"loop"})", false},
			 {"themes/bad.json", R"({"schema":1,"id":"Bad Id"})", false},
			 {"themes/escape.json", R"({"schema":1,"id":"escape","backdrop":{"kind":"image","image":"../../secret.png"}})", false}},
			errors);
		// A broken shipped file still leaves the built-in theme in place.
		REQUIRE(catalog.find("light"));
		CHECK(catalog.find("light")->name == "Hijack");
		REQUIRE(catalog.find("dark"));
		const Theme *mine = catalog.find("mine");
		REQUIRE(mine);
		CHECK(mine->name == "Mine");
		CHECK(same(mine->palette.accent, GAGCore::Color(255, 0, 0)));
		// Unset and invalid tokens keep the parent's values.
		CHECK(same(mine->palette.ink, ThemeCatalog::builtinDark().palette.ink));
		CHECK(same(mine->hud.paper, ThemeCatalog::builtinDark().hud.paper));
		CHECK_FALSE(catalog.find("Bad Id"));
		REQUIRE(catalog.find("escape"));
		CHECK(catalog.find("escape")->backdrop.image.empty());
		CHECK(errors.size() >= 6);
		CHECK(&catalog.resolve("missing", "dark") == catalog.find("dark"));
	}

	TEST_CASE("shipped themes win over player files with the same id")
	{
		std::vector<std::string> errors;
		auto catalog = ThemeCatalog::parse(
			{{"data/themes/light.json", R"({"schema":1,"id":"light","name":"Light"})", true},
			 {"themes/light.json", R"({"schema":1,"id":"light","name":"Hijack"})", false}},
			errors);
		CHECK(catalog.find("light")->name == "Light");
	}

	TEST_CASE("menu and in-game slots switch independently and keep their addresses")
	{
		glob2test::HeadlessGlobals globals;
		const Theme *menu = &Glob2UI::themeFor(Glob2UI::Surface::Frontend);
		const Theme *game = &Glob2UI::themeFor(Glob2UI::Surface::Match);
		const unsigned before = Glob2UI::themeGeneration();
		Glob2UI::applyThemes("dark", "light");
		CHECK(Glob2UI::themeGeneration() != before);
		CHECK(&Glob2UI::themeFor(Glob2UI::Surface::Frontend) == menu);
		CHECK(menu->id == "dark");
		CHECK(game->id == "light");
		CHECK(&Glob2UI::themeFor(Glob2UI::Surface::Editor) == game);
		CHECK(&Glob2UI::themeFor(Glob2UI::Surface::Results) == menu);
		// The HUD reads the in-game slot.
		CHECK(same(InGameTouchTheme::ink(), ThemeCatalog::builtinLight().hud.ink));
		Glob2UI::applyThemes("no-such-theme", "classic");
		CHECK(menu->id == ThemeCatalog::menuDefault);
		CHECK(game->id == "classic");
		Glob2UI::applyThemes(ThemeCatalog::menuDefault, ThemeCatalog::gameDefault);
		CHECK(same(InGameTouchTheme::ink(), GAGCore::Color(249, 232, 187)));
	}

	TEST_CASE("theme choices persist in preferences")
	{
		glob2test::HeadlessGlobals globals;
		const std::string file = "theme-test.txt";
		Settings defaults;
		CHECK(defaults.menuTheme == "light");
		CHECK(defaults.gameTheme == "dark");
		Settings s;
		s.menuTheme = "classic";
		s.gameTheme = "ocean";
		REQUIRE(s.save(file));
		Settings reloaded;
		reloaded.load(file);
		CHECK(reloaded.menuTheme == "classic");
		CHECK(reloaded.gameTheme == "ocean");
		std::filesystem::remove(std::filesystem::path(GAGCore::Toolkit::getFileManager()->getDir(0)) / file);
	}
}
