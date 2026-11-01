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
		CHECK(code("nn").empty());
		CHECK(code("").empty());
	}

	TEST_CASE("Bengali regions share the Bengali script catalog")
	{
		CHECK(code("bn") == "bn");
		CHECK(code("bn", "BD") == "bn");
		CHECK(code("bn-IN") == "bn");
		CHECK(code("bn_Beng_BD") == "bn");
		CHECK(code("bn-Beng", "IN") == "bn");
		CHECK(code("bn-Latn").empty());
		CHECK(code("bn-Deva", "IN").empty());
		CHECK(code("bn-u-nu-latn") == "bn");
		CHECK(code("bn-x-latn") == "bn");
	}

	TEST_CASE("Norwegian Bokmaal accepts generic Norwegian but never Nynorsk")
	{
		CHECK(code("nb") == "nb");
		CHECK(code("nb-NO") == "nb");
		CHECK(code("NB", "NO") == "nb");
		CHECK(code("no") == "nb");
		CHECK(code("no_NO") == "nb");
		CHECK(code("no-bok") == "nb");
		CHECK(code("no-u-ca-nyn") == "nb");
		CHECK(code("no-x-nyn") == "nb");
		CHECK(code("nn-NO").empty());
		CHECK(code("no-nyn").empty());
		CHECK(code("no_NO_NY").empty());
		CHECK(code("no", "NO_NY").empty());
		CHECK(code("no-NO-u-ca-ny") == "nb");
		CHECK(SystemLanguage::choose({{"no-NO-NY", ""}, {"sv", "SE"}}, [](const std::string &) { return true; }) == "sv");
		CHECK(SystemLanguage::choose({{"nn", "NO"}, {"de", "DE"}}, [](const std::string &) { return true; }) == "de");
		CHECK(SystemLanguage::choose({{"nb", "NO"}, {"de", "DE"}}, [](const std::string &c) { return c == "de"; }) == "de");
	}

	TEST_CASE("expanded locales distinguish scripts and the precise Pulaar variety")
	{
		CHECK(code("hi", "IN") == "hi");
		CHECK(code("tl", "PH") == "fil");
		CHECK(code("jw") == "jv");
		CHECK(code("yo", "NG") == "yo");
		CHECK(code("pa", "PK") == "pa-arab");
		CHECK(code("pa", "IN") == "pa-guru");
		CHECK(code("pa-Guru", "PK") == "pa-guru");
		CHECK(code("pa-Arab", "IN") == "pa-arab");
		CHECK(code("pa", "Arab_PK") == "pa-arab");
		CHECK(code("pa-Deva").empty());
		CHECK(code("pa-Arab-Guru").empty());
		CHECK(code("fuc-Latn") == "fuc");
		CHECK(code("ff-Latn", "SN") == "fuc");
		CHECK(code("ff", "MR") == "fuc");
		CHECK(code("ff", "Latn_SN") == "fuc");
		CHECK(code("ff", "NG").empty());
		CHECK(code("ff-Adlm", "SN").empty());
		CHECK(code("ff-Arab", "SN").empty());
		CHECK(code("fuc-Arab").empty());
		CHECK(code("jv-Java").empty());
		CHECK(code("su-Sund").empty());
		CHECK(code("hi-Latn").empty());
		CHECK(code("hi-u-nu-latn") == "hi");
		CHECK(code("hi-x-latn") == "hi");
		CHECK(code("pa-IN-u-nu-arab") == "pa-guru");
		CHECK(code("uz-Cyrl").empty());
		CHECK(code("sd-Deva").empty());
	}

	TEST_CASE("the first available preference wins, then English")
	{
		auto all = [](const std::string &) { return true; };
		CHECK(SystemLanguage::choose({{"xx", ""}, {"de", "DE"}, {"fr", ""}}, all) == "de");
		CHECK(SystemLanguage::choose({{"xx", ""}}, all) == "en");
		CHECK(SystemLanguage::choose({{"hi", "IN"}, {"de", "DE"}}, [](const std::string &c) { return c == "de"; }) == "de");
		CHECK(SystemLanguage::choose({}, all) == "en");
		CHECK(SystemLanguage::choose({{"de", ""}, {"fr", ""}}, [](const std::string &c) { return c == "fr"; }) == "fr");
	}

	TEST_CASE("Bengali startup respects the system and saved language")
	{
		{
			PreferredLocales locales("bn_BD,fr_FR,en_US");
			CHECK(startupLanguage("") == "bn");
			CHECK(startupLanguage("fr") == "fr");
		}
		{
			PreferredLocales locales("fr_FR,en_US");
			CHECK(startupLanguage("bn") == "bn");
		}
	}

	TEST_CASE("Norwegian startup follows preferences and preserves a saved language")
	{
		PreferredLocales locales("nb_NO,en_US");
		CHECK(startupLanguage("") == "nb");
		CHECK(startupLanguage("fr") == "fr");
		CHECK(startupLanguage("nb") == "nb");
	}

	TEST_CASE("startup follows the system only when the profile has no usable language")
	{
		// The first preference must stay unsupported as new catalogs are added.
		PreferredLocales locales("xx_XX,pt_BR,en_US");
		CHECK(startupLanguage("") == "br");
		CHECK(startupLanguage("no-such-language") == "br");
		CHECK(startupLanguage("fr") == "fr");
	}
}
