// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Android, iOS and the browser have no fribidi, so TrueTypeFont orders and shapes
// Arabic and Persian text with BidiText. Where fribidi is present (desktop builds),
// every Arabic and Persian catalog value must come out exactly as fribidi draws it.

#include "Glob2Test.h"
#include <BidiText.h>
#include <GAGSys.h>
#include <fstream>
#include <string>
#include <vector>
#ifdef HAVE_FRIBIDI
#include <fribidi/fribidi.h>
#endif

using GAGCore::visualOrder;

namespace
{
	// Code points of a UTF-8 string, so failures print readably.
	std::string codes(const std::string &text)
	{
		std::string result;
		for (size_t i = 0; i < text.size();)
		{
			const unsigned char c = text[i];
			const int length = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
			uint32_t value = length == 1 ? c : c & (0x7F >> length);
			for (int k = 1; k < length && i + k < text.size(); ++k)
				value = (value << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
			char buffer[12];
			snprintf(buffer, sizeof buffer, "%s%04X", result.empty() ? "" : " ", value);
			result += buffer;
			i += length;
		}
		return result;
	}

	std::string utf8(const std::vector<uint32_t> &points)
	{
		std::string result;
		for (uint32_t c : points)
		{
			if (c < 0x80)
				result += static_cast<char>(c);
			else if (c < 0x800)
				result += {static_cast<char>(0xC0 | (c >> 6)), static_cast<char>(0x80 | (c & 0x3F))};
			else
				result += {static_cast<char>(0xE0 | (c >> 12)), static_cast<char>(0x80 | ((c >> 6) & 0x3F)),
					static_cast<char>(0x80 | (c & 0x3F))};
		}
		return result;
	}

#ifdef HAVE_FRIBIDI
	// The same calls as TrueTypeFont::getBIDIString.
	std::string fribidi(const std::string &text)
	{
		std::vector<FriBidiChar> logical(text.size() + 2), visual(text.size() + 2);
		std::vector<char> out(4 * text.size() + 1);
		FriBidiCharType base = FRIBIDI_TYPE_ON;
		int n = fribidi_charset_to_unicode(FRIBIDI_CHAR_SET_UTF8, text.c_str(), text.size(), logical.data());
		fribidi_log2vis(logical.data(), n, &base, visual.data(), NULL, NULL, NULL);
		n = fribidi_remove_bidi_marks(visual.data(), n, NULL, NULL, NULL);
		fribidi_unicode_to_charset(FRIBIDI_CHAR_SET_UTF8, visual.data(), n, out.data());
		return out.data();
	}
#endif
}

TEST_SUITE("BidiText")
{
	TEST_CASE("left-to-right text is unchanged")
	{
		CHECK(visualOrder("Hello, world (2 vs 2)") == "Hello, world (2 vs 2)");
		CHECK(visualOrder("Français · 日本語 · Ελληνικά") == "Français · 日本語 · Ελληνικά");
		CHECK(visualOrder("") == "");
	}

	TEST_CASE("Arabic letters join and read right to left")
	{
		// سلام: seen initial, lam-alef ligature (final), meem isolated; displayed reversed.
		CHECK(codes(visualOrder(utf8({0x0633, 0x0644, 0x0627, 0x0645}))) == "FEE1 FEFC FEB3");
		// Persian بازی: beh initial, alef final, zain isolated, farsi yeh isolated.
		CHECK(codes(visualOrder(utf8({0x0628, 0x0627, 0x0632, 0x06CC}))) == "FBFC FEAF FE8E FE91");
	}

	TEST_CASE("numbers and Latin words keep their own order inside right-to-left text")
	{
		// "لعبة Glob 2" with the paragraph starting right to left.
		CHECK(codes(visualOrder(utf8({0x0644, 0x0639, 0x0628, 0x0629, ' ', 'G', 'l', 'o', 'b', ' ', '2'})))
			== "0047 006C 006F 0062 0020 0032 0020 FE94 FE92 FECC FEDF");
		// "(12) من" — brackets are mirrored and the number stays left to right.
		CHECK(codes(visualOrder(utf8({0x0645, 0x0646, ' ', '(', '1', '2', ')'}))) == "0028 0031 0032 0029 0020 FEE6 FEE3");
	}

#ifdef HAVE_FRIBIDI
	TEST_CASE("matches fribidi on every Arabic and Persian catalog value")
	{
		for (const char *catalog : {"data/texts.ar.txt", "data/texts.fa.txt"})
		{
			std::ifstream file(glob2test::sourceRoot() / catalog);
			REQUIRE(file);
			std::string key, value;
			int compared = 0;
			while (std::getline(file, key) && std::getline(file, value))
			{
				// Values are drawn one line at a time.
				for (size_t start = 0; start <= value.size();)
				{
					size_t end = value.find("\\n", start);
					if (end == std::string::npos)
						end = value.size();
					const std::string line = value.substr(start, end - start);
					INFO(catalog << " " << key << ": " << line);
					CHECK(codes(visualOrder(line)) == codes(fribidi(line)));
					++compared;
					start = end + 2;
				}
			}
			CHECK(compared > 2000);
		}
	}
#endif
}
