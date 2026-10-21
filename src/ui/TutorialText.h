// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TutorialTextAliases.h"
#include <StringTable.h>
#include <Toolkit.h>
#include <algorithm>
#include <iterator>
#include <string>

namespace Glob2UI
{
// Translate at the client display boundary. Legacy maps/saves keep their script
// tokens, execution offsets and campaign identifiers unchanged.
// Full-source fingerprints distinguish shipped tutorial scripts (including
// saved copies) from custom scenarios that reuse generic phrases like "Good...".
inline bool isTutorialSource(const std::string &source)
{
    std::uint64_t hash = 14695981039346656037ull;
    std::size_t length = 0;
    for (std::size_t i = 0; i < source.size(); ++i)
    {
        if (source[i] == '\r' && i + 1 < source.size() && source[i + 1] == '\n') continue;
        hash = (hash ^ static_cast<unsigned char>(source[i])) * 1099511628211ull;
        ++length;
    }
    return std::any_of(std::begin(tutorialSourceSignatures), std::end(tutorialSourceSignatures),
        [&](const auto &signature) { return signature.first == hash && signature.second == length; });
}
inline std::string tutorialText(const std::string &text, bool knownTutorial = false)
{
    if (!knownTutorial) return text;
	const auto alias = std::lower_bound(std::begin(tutorialTextAliases),
		std::end(tutorialTextAliases), std::string_view(text),
		[](const auto &entry, std::string_view value) { return entry.first < value; });
	if (alias == std::end(tutorialTextAliases) || alias->first != text)
		return text;
	auto *strings = GAGCore::Toolkit::getStringTable();
	const std::string key(alias->second);
	return strings->doesStringExist(key) ? strings->getString(key) : text;
}
} // namespace Glob2UI
