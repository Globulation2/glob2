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

	TEST_CASE("Filipino aliases accept only the shipped Latin script")
	{
		CHECK(code("fil") == "fil");
		CHECK(code("fil-PH") == "fil");
		CHECK(code("tl") == "fil");
		CHECK(code("tl-PH") == "fil");
		CHECK(code("fil-Latn-PH") == "fil");
		CHECK(code("tl-Latn", "PH") == "fil");
		CHECK(code("fil-Tglg").empty());
		CHECK(code("tl-Tglg", "PH").empty());
		CHECK(code("fil-Arab").empty());
		CHECK(code("tl-Cyrl").empty());
		CHECK(code("fil-Latn-Tglg").empty());
		CHECK(code("tl-Latn-Arab").empty());
		CHECK(code("fil-u-nu-latn") == "fil");
		CHECK(code("tl-x-arab") == "fil");
		CHECK(code("en", "PH") == "en");
		CHECK(code("ceb", "PH").empty());
		auto latinOnly = [](const std::string &c) { return c == "fil"; };
		CHECK(SystemLanguage::choose({{"tl", "PH"}, {"de", ""}}, latinOnly) == "fil");
		CHECK(SystemLanguage::choose({{"fil", "PH"}}, [](const std::string &) { return false; }) == "en");
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

	TEST_CASE("startup follows the system only when the profile has no usable language")
	{
		PreferredLocales locales("hi_IN,pt_BR,en_US");
		CHECK(startupLanguage("") == "br");
		CHECK(startupLanguage("no-such-language") == "br");
		CHECK(startupLanguage("fr") == "fr");
	}
}
