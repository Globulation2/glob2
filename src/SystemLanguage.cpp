// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "SystemLanguage.h"

#include <SDL3/SDL_locale.h>
#include <SDL3/SDL_stdinc.h>
#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace SystemLanguage
{
	namespace
	{
		std::string lower(std::string text)
		{
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return text;
		}
	}

	std::string catalogCode(const Locale &locale)
	{
		// Some platforms report "zh-Hant" or "pt_BR" in the language field alone, so
		// the script and region subtags after it count as much as the country.
		std::string language = lower(locale.language);
		std::set<std::string> tags{lower(locale.country)};
		for (size_t separator; (separator = language.find_last_of("-_")) != std::string::npos;)
		{
			tags.insert(language.substr(separator + 1));
			language.erase(separator);
		}
		auto has = [&](const char *tag) { return tags.count(tag) != 0; };

		if (language == "zh")
			return has("hant") || has("tw") || has("hk") || has("mo") ? "zh-tw" : "zh-cn";
		if (language == "pt")
			return has("br") ? "br" : "pt";
		static const std::map<std::string, std::string> codes = {
			{"ar", "ar"}, {"ca", "ca"}, {"cs", "cz"}, {"da", "dk"}, {"de", "de"}, {"el", "gr"},
			{"en", "en"}, {"eo", "eo"}, {"es", "es"}, {"eu", "eu"}, {"fa", "fa"}, {"fi", "fi"},
			{"fr", "fr"}, {"hu", "hu"}, {"id", "id"}, {"in", "id"}, {"it", "it"}, {"ja", "ja"},
			{"ko", "ko"}, {"nl", "nl"}, {"pl", "pl"}, {"ro", "ro"}, {"ru", "ru"}, {"sk", "sk"},
			{"sl", "si"}, {"sr", "sr"}, {"sv", "sv"}, {"tr", "tr"}, {"uk", "uk"}, {"vi", "vi"},
		};
		const auto found = codes.find(language);
		return found == codes.end() ? std::string() : found->second;
	}

	std::string choose(const std::vector<Locale> &preferred, const std::function<bool(const std::string &)> &available)
	{
		for (const Locale &locale : preferred)
		{
			const std::string code = catalogCode(locale);
			if (!code.empty() && available(code))
				return code;
		}
		return "en";
	}

	std::vector<Locale> preferred()
	{
		std::vector<Locale> result;
		int count = 0;
		SDL_Locale **locales = SDL_GetPreferredLocales(&count);
		if (!locales)
			return result;
		for (int i = 0; i < count && locales[i]; ++i)
			result.push_back({locales[i]->language ? locales[i]->language : "", locales[i]->country ? locales[i]->country : ""});
		SDL_free(locales);
		return result;
	}
}
