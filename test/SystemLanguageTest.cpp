// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// A profile without a language follows the system's preferred languages, mapped
// to the game's catalog codes, several of which are not ISO 639 codes.

#include "EngineFixtures.h"
#include "GlobalContainer.h"
#include "SystemLanguage.h"
#include <SDL3/SDL_hints.h>
#include <StringTable.h>
#include <Toolkit.h>
#include <string>

namespace
{
	std::string code(const char *language, const char *country = "")
	{
		return SystemLanguage::catalogCode({language, country});
	}

	// Restores SDL's preferred-locale override, which is process-wide.
	struct PreferredLocales
	{
		explicit PreferredLocales(const char *locales) { SDL_SetHint(SDL_HINT_PREFERRED_LOCALES, locales); }
		~PreferredLocales() { SDL_ResetHint(SDL_HINT_PREFERRED_LOCALES); }
	};

	std::string startupLanguage(const std::string &saved)
	{
		glob2test::GlobalsOptions options{.loadStrings = true};
		options.beforeLoad = [saved](GlobalContainer &globals) { globals.settings.language = saved; };
		glob2test::HeadlessGlobals globals(options);
		auto *strings = GAGCore::Toolkit::getStringTable();
		CHECK(strings->getLang() == strings->getLangCode(globals.globals.settings.language));
		return globals.globals.settings.language;
	}
}

TEST_SUITE("SystemLanguage")
{
	TEST_CASE("ISO locales map to the historical catalog codes")
	{
		CHECK(code("cs") == "cz");
		CHECK(code("da", "DK") == "dk");
		CHECK(code("el") == "gr");
		CHECK(code("sl") == "si");
		CHECK(code("de", "AT") == "de");
		CHECK(code("FR", "ca") == "fr");
		CHECK(code("in") == "id");
	}

	TEST_CASE("regional and script variants pick the matching catalog")
	{
		CHECK(code("pt", "BR") == "br");
		CHECK(code("pt_BR") == "br");
		CHECK(code("pt", "PT") == "pt");
		CHECK(code("pt") == "pt");
		CHECK(code("zh", "CN") == "zh-cn");
		CHECK(code("zh") == "zh-cn");
		CHECK(code("zh", "TW") == "zh-tw");
		CHECK(code("zh", "HK") == "zh-tw");
		CHECK(code("zh-Hant") == "zh-tw");
		CHECK(code("zh-Hans", "TW") == "zh-tw");
	}

	TEST_CASE("languages without a catalog are not confused with catalog codes")
	{
		CHECK(code("br").empty()); // Breton, not Brazilian Portuguese
		CHECK(code("si").empty()); // Sinhala, not Slovenian
		CHECK(code("nb").empty());
		CHECK(code("").empty());
	}

	TEST_CASE("the first available preference wins, then English")
	{
		auto all = [](const std::string &) { return true; };
		CHECK(SystemLanguage::choose({{"hi", "IN"}, {"de", "DE"}, {"fr", ""}}, all) == "de");
		CHECK(SystemLanguage::choose({{"hi", "IN"}}, all) == "en");
		CHECK(SystemLanguage::choose({}, all) == "en");
		CHECK(SystemLanguage::choose({{"de", ""}, {"fr", ""}}, [](const std::string &c) { return c == "fr"; }) == "fr");
	}

	TEST_CASE("startup follows the system only when the profile has no usable language")
	{
		PreferredLocales locales("hi_IN,pt_BR,en_US");
		CHECK(startupLanguage("") == "br");
		CHECK(startupLanguage("no-such-language") == "br");
		CHECK(startupLanguage("fr") == "fr");
	}
}
