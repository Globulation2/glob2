// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Character-class checks for identifiers read from files, packages and archives.
// These replace std::regex patterns with counted repeats such as [0-9a-f]{64}:
// libstdc++ compiles each repeat into that many copies of the atom and recurses
// once per copy, which overflowed the stack of LTO-optimized MinGW builds.

#include <cstddef>
#include <string_view>

namespace GAGCore::TextShape
{
	constexpr bool lowerHexDigit(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }
	constexpr bool asciiDigit(char c) { return c >= '0' && c <= '9'; }
	constexpr bool asciiLower(char c) { return c >= 'a' && c <= 'z'; }
	constexpr bool asciiAlnum(char c) { return asciiDigit(c) || asciiLower(c) || (c >= 'A' && c <= 'Z'); }

	// Every character satisfies the predicate (true for an empty view).
	template <typename Predicate>
	constexpr bool all(std::string_view text, Predicate predicate)
	{
		for (char c : text)
			if (!predicate(c))
				return false;
		return true;
	}

	// Exactly `length` characters of [0-9a-f].
	constexpr bool lowerHex(std::string_view text, std::size_t length)
	{
		return text.size() == length && all(text, lowerHexDigit);
	}

	// A lowercase UUID: [0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}.
	constexpr bool lowerUuid(std::string_view text)
	{
		if (text.size() != 36)
			return false;
		for (std::size_t i = 0; i < text.size(); ++i)
		{
			const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
			if (dash ? text[i] != '-' : !lowerHexDigit(text[i]))
				return false;
		}
		return true;
	}
}
