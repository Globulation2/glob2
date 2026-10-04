// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

#include <functional>
#include <string>
#include <vector>

/// Picks the interface language for a profile that has not chosen one, from the
/// operating system's (or browser's) preferred languages.
namespace SystemLanguage
{
	struct Locale
	{
		std::string language; ///< ISO 639, e.g. "pt"; may also carry a script, e.g. "zh-Hant"
		std::string country;  ///< ISO 3166, e.g. "BR"; may be empty
	};

	/// The catalog code (data/texts.<code>.txt) for a locale, or "" if the game has
	/// no catalog for it. Several historical catalog codes are not ISO 639 codes
	/// (cz, dk, gr, si, br), so the ISO code is never used as the catalog code blindly:
	/// Breton "br" and Sinhala "si", for example, have no catalog.
	std::string catalogCode(const Locale &locale);

	/// The first locale, in preference order, whose catalog is available; "en" if none.
	std::string choose(const std::vector<Locale> &preferred, const std::function<bool(const std::string &)> &available);

	/// The system's preferred locales, most preferred first (SDL_GetPreferredLocales).
	std::vector<Locale> preferred();
}
