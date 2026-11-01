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
		std::set<std::string> tags;
		auto addTags = [&](std::string suffix)
		{
			for (;;)
			{
				const size_t separator = suffix.find_first_of("-_");
				const auto tag = suffix.substr(0, separator);
				// BCP47 extensions/private use are not text script or region tags.
				// For example, hi-u-nu-latn still requests Devanagari text.
				if (tag.size() == 1) break;
				if (!tag.empty()) tags.insert(tag);
				if (separator == std::string::npos) break;
				suffix.erase(0, separator + 1);
			}
		};
		addTags(lower(locale.country));
		const size_t separator = language.find_first_of("-_");
		if (separator != std::string::npos)
		{
			addTags(language.substr(separator + 1));
			language.erase(separator);
		}
		auto has = [&](const char *tag) { return tags.count(tag) != 0; };

		if (language == "zh")
			return has("hant") || has("tw") || has("hk") || has("mo") ? "zh-tw" : "zh-cn";
		// Generic Norwegian defaults to Bokmål. Both legacy Nynorsk forms
		// (no-nyn and no_NO_NY) explicitly request the other written standard.
		if (language == "no") return has("nyn") || (has("no") && has("ny")) ? "" : "nb";
		if (language == "pt")
			return has("br") ? "br" : "pt";
		std::set<std::string> scripts;
		for (const auto &tag : tags)
			if (tag.size() == 4 && std::all_of(tag.begin(), tag.end(), [](unsigned char c) { return std::isalpha(c); }))
				scripts.insert(tag);
		// These new catalogs cover specific written forms, not transliterations
		// or other scripts of the same language. Never select the wrong form.
		static const std::map<std::string, std::string> writtenForms = {
			{"am", "ethi"}, {"bg", "cyrl"}, {"bho", "deva"}, {"bn", "beng"}, {"ff", "latn"}, {"fuc", "latn"},
			{"fil", "latn"}, {"tl", "latn"}, {"gu", "gujr"}, {"ha", "latn"}, {"hi", "deva"},
			{"ig", "latn"}, {"jv", "latn"}, {"jw", "latn"}, {"kn", "knda"}, {"ln", "latn"},
			{"ml", "mlym"}, {"mr", "deva"}, {"my", "mymr"}, {"ne", "deva"}, {"om", "latn"},
			{"or", "orya"}, {"pcm", "latn"}, {"ps", "arab"}, {"sd", "arab"}, {"su", "latn"},
			{"sw", "latn"}, {"ta", "taml"}, {"te", "telu"}, {"th", "thai"}, {"ur", "arab"},
			{"uz", "latn"}, {"yo", "latn"},
		};
		const auto written = writtenForms.find(language);
		if (written != writtenForms.end() && !scripts.empty() &&
		    (scripts.size() != 1 || *scripts.begin() != written->second))
			return {};
		if (language == "pa")
		{
			if (scripts.size() > 1 || (!scripts.empty() && !has("arab") && !has("guru"))) return {};
			if (has("arab")) return "pa-arab";
			if (has("guru")) return "pa-guru";
			return has("pk") ? "pa-arab" : "pa-guru";
		}
		// The Pulaar catalog is specifically the Latin-script SN/MR variety,
		// not every language under the Fula macrolanguage or its Adlam script.
		if (language == "fuc" || language == "ff")
			return !has("adlm") && (language == "fuc" || has("sn") || has("mr")) ? "fuc" : "";
		static const std::map<std::string, std::string> codes = {
			{"ar", "ar"}, {"bg", "bg"}, {"ca", "ca"}, {"cs", "cz"}, {"da", "dk"}, {"de", "de"}, {"el", "gr"},
			{"en", "en"}, {"eo", "eo"}, {"es", "es"}, {"eu", "eu"}, {"fa", "fa"}, {"fi", "fi"},
			{"fr", "fr"}, {"hu", "hu"}, {"id", "id"}, {"in", "id"}, {"it", "it"}, {"ja", "ja"},
			{"ko", "ko"}, {"nb", "nb"}, {"nl", "nl"}, {"pl", "pl"}, {"ro", "ro"}, {"ru", "ru"}, {"sk", "sk"},
			{"sl", "si"}, {"sr", "sr"}, {"sv", "sv"}, {"tr", "tr"}, {"uk", "uk"}, {"vi", "vi"},
			{"am", "am"}, {"bho", "bho"}, {"bn", "bn"}, {"fil", "fil"}, {"tl", "fil"},
			{"gu", "gu"}, {"ha", "ha"}, {"hi", "hi"}, {"ig", "ig"}, {"jv", "jv"}, {"jw", "jv"},
			{"kn", "kn"}, {"ln", "ln"}, {"ml", "ml"}, {"mr", "mr"}, {"my", "my"},
			{"ne", "ne"}, {"om", "om"}, {"or", "or"}, {"pcm", "pcm"}, {"ps", "ps"},
			{"sd", "sd"}, {"su", "su"}, {"sw", "sw"}, {"ta", "ta"}, {"te", "te"},
			{"th", "th"}, {"ur", "ur"}, {"uz", "uz"}, {"yo", "yo"},
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
