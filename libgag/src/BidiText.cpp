// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Arabic shaping and the implicit part of the Unicode bidirectional algorithm (UAX #9:
// rules W1-W7, N0-N2, I1-I2, L1, L2 and L4 mirroring) for one line of text. It stands
// in for fribidi_log2vis on platforms built without fribidi, and the translation tests
// compare it against fribidi on every Arabic and Persian string the game ships.

#include <BidiText.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace GAGCore
{
	namespace
	{
		typedef uint32_t Char;

		enum BidiClass { L, R, AL, EN, ES, ET, AN, CS, NSM, BN, B, S, WS, ON };

		std::vector<Char> decode(const std::string &text)
		{
			std::vector<Char> result;
			for (size_t i = 0; i < text.size();)
			{
				const unsigned char c = text[i];
				int length = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
				if (i + length > text.size())
					length = 1;
				Char value = length == 1 ? c : c & (0x7F >> length);
				for (int k = 1; k < length; ++k)
					value = (value << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
				result.push_back(value);
				i += length;
			}
			return result;
		}

		void encode(Char c, std::string &out)
		{
			if (c < 0x80)
				out += static_cast<char>(c);
			else if (c < 0x800)
			{
				out += static_cast<char>(0xC0 | (c >> 6));
				out += static_cast<char>(0x80 | (c & 0x3F));
			}
			else if (c < 0x10000)
			{
				out += static_cast<char>(0xE0 | (c >> 12));
				out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (c & 0x3F));
			}
			else
			{
				out += static_cast<char>(0xF0 | (c >> 18));
				out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
				out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (c & 0x3F));
			}
		}

		bool in(Char c, Char first, Char last) { return c >= first && c <= last; }

		bool arabicMark(Char c)
		{
			return in(c, 0x0610, 0x061A) || in(c, 0x064B, 0x065F) || c == 0x0670 || in(c, 0x06D6, 0x06DC)
				|| in(c, 0x06DF, 0x06E4) || in(c, 0x06E7, 0x06E8) || in(c, 0x06EA, 0x06ED) || in(c, 0x08D3, 0x08E1)
				|| in(c, 0x08E3, 0x08FF);
		}

		// Bidi_Class for the scripts and symbols the interface uses; anything else is L.
		BidiClass classOf(Char c)
		{
			if (c == '\n' || c == '\r' || in(c, 0x1C, 0x1E) || c == 0x85 || c == 0x2029)
				return B;
			if (c == '\t' || c == 0x0B || c == 0x1F)
				return S;
			if (c == ' ' || c == 0x0C || c == 0x1680 || in(c, 0x2000, 0x200A) || c == 0x2028 || c == 0x205F || c == 0x3000)
				return WS;
			if (c < 0x20 || in(c, 0x7F, 0x9F) || c == 0xAD || in(c, 0x200B, 0x200D) || in(c, 0x2060, 0x2064)
				|| c == 0xFEFF || in(c, 0x202A, 0x202E) || in(c, 0x2066, 0x2069))
				return BN; // explicit formatting characters are removed (X9) like boundary neutrals
			if (c == 0x200E)
				return L;
			if (c == 0x200F)
				return R;
			if (c == 0x061C)
				return AL;
			if (in(c, '0', '9') || c == 0xB2 || c == 0xB3 || c == 0xB9 || in(c, 0x06F0, 0x06F9) || c == 0x2070
				|| in(c, 0x2074, 0x2079) || in(c, 0x2080, 0x2089) || in(c, 0xFF10, 0xFF19))
				return EN;
			if (c == '+' || c == '-' || c == 0x207A || c == 0x207B || c == 0x208A || c == 0x208B || c == 0xFB29
				|| c == 0xFE62 || c == 0xFE63 || c == 0xFF0B || c == 0xFF0D)
				return ES;
			if (c == '#' || c == '$' || c == '%' || in(c, 0xA2, 0xA5) || c == 0xB0 || c == 0xB1 || c == 0x058F
				|| c == 0x0609 || c == 0x060A || c == 0x066A || in(c, 0x20A0, 0x20CF) || in(c, 0x2030, 0x2034)
				|| c == 0x212E || c == 0x2213 || c == 0xFE5F || c == 0xFE69 || c == 0xFE6A || in(c, 0xFF03, 0xFF05)
				|| c == 0xFFE0 || c == 0xFFE1 || c == 0xFFE5 || c == 0xFFE6)
				return ET;
			if (c == ',' || c == '.' || c == '/' || c == ':' || c == 0xA0 || c == 0x060C || c == 0x202F || c == 0x2044
				|| c == 0xFE50 || c == 0xFE52 || c == 0xFE55 || c == 0xFF0C || c == 0xFF0E || c == 0xFF0F || c == 0xFF1A)
				return CS;
			if (in(c, 0x0600, 0x0605) || in(c, 0x0660, 0x0669) || c == 0x066B || c == 0x066C || c == 0x06DD
				|| in(c, 0x0890, 0x0891) || c == 0x08E2)
				return AN;
			if (in(c, 0x0300, 0x036F) || in(c, 0x0483, 0x0489) || in(c, 0x0591, 0x05BD) || c == 0x05BF
				|| in(c, 0x05C1, 0x05C2) || in(c, 0x05C4, 0x05C5) || c == 0x05C7 || arabicMark(c)
				|| in(c, 0x1AB0, 0x1AFF) || in(c, 0x1DC0, 0x1DFF) || in(c, 0x20D0, 0x20F0) || in(c, 0xFE00, 0xFE0F)
				|| in(c, 0xFE20, 0xFE2F) || in(c, 0x3099, 0x309A) || c == 0xFB1E)
				return NSM;
			if (c == 0x0606 || c == 0x0607 || c == 0x060E || c == 0x060F || c == 0x06DE || c == 0x06E9
				|| in(c, 0xFD3E, 0xFD3F))
				return ON;
			if (in(c, 0x0590, 0x05FF) || in(c, 0x07C0, 0x085F) || in(c, 0xFB1D, 0xFB4F) || in(c, 0x10800, 0x10FFF))
				return R;
			if (in(c, 0x0600, 0x07BF) || in(c, 0x0860, 0x08FF) || in(c, 0xFB50, 0xFDCF) || in(c, 0xFDF0, 0xFDFF)
				|| in(c, 0xFE70, 0xFEFE))
				return AL;
			if (c < 0x80)
				return (in(c, 'A', 'Z') || in(c, 'a', 'z')) ? L : ON;
			if (in(c, 0xA1, 0xBF) || c == 0xD7 || c == 0xF7 || in(c, 0x2010, 0x2027) || in(c, 0x2035, 0x2043)
				|| in(c, 0x2045, 0x205E) || in(c, 0x2190, 0x2BFF) || in(c, 0x2E00, 0x2E7F) || in(c, 0x3001, 0x3004)
				|| in(c, 0x3008, 0x3020) || c == 0x30FB || in(c, 0xFE30, 0xFE4F) || in(c, 0xFE51, 0xFE54)
				|| in(c, 0xFE56, 0xFE5E) || in(c, 0xFE60, 0xFE61) || in(c, 0xFE64, 0xFE68) || c == 0xFE6B
				|| in(c, 0xFF01, 0xFF02) || in(c, 0xFF06, 0xFF0A) || in(c, 0xFF1B, 0xFF20) || in(c, 0xFF3B, 0xFF40)
				|| in(c, 0xFF5B, 0xFF65) || in(c, 0xFFE2, 0xFFE4) || in(c, 0xFFE8, 0xFFEE))
				return ON;
			return L;
		}

		// Isolated presentation form and the number of forms (isolated, final, initial, medial).
		struct Shape { Char letter, isolated; int forms; };
		const Shape shapes[] = {
			{0x0621, 0xFE80, 1}, {0x0622, 0xFE81, 2}, {0x0623, 0xFE83, 2}, {0x0624, 0xFE85, 2}, {0x0625, 0xFE87, 2},
			{0x0626, 0xFE89, 4}, {0x0627, 0xFE8D, 2}, {0x0628, 0xFE8F, 4}, {0x0629, 0xFE93, 2}, {0x062A, 0xFE95, 4},
			{0x062B, 0xFE99, 4}, {0x062C, 0xFE9D, 4}, {0x062D, 0xFEA1, 4}, {0x062E, 0xFEA5, 4}, {0x062F, 0xFEA9, 2},
			{0x0630, 0xFEAB, 2}, {0x0631, 0xFEAD, 2}, {0x0632, 0xFEAF, 2}, {0x0633, 0xFEB1, 4}, {0x0634, 0xFEB5, 4},
			{0x0635, 0xFEB9, 4}, {0x0636, 0xFEBD, 4}, {0x0637, 0xFEC1, 4}, {0x0638, 0xFEC5, 4}, {0x0639, 0xFEC9, 4},
			{0x063A, 0xFECD, 4}, {0x0641, 0xFED1, 4}, {0x0642, 0xFED5, 4}, {0x0643, 0xFED9, 4}, {0x0644, 0xFEDD, 4},
			{0x0645, 0xFEE1, 4}, {0x0646, 0xFEE5, 4}, {0x0647, 0xFEE9, 4}, {0x0648, 0xFEED, 2}, {0x0649, 0xFEEF, 4},
			{0x064A, 0xFEF1, 4}, {0x0671, 0xFB50, 2}, {0x067E, 0xFB56, 4}, {0x0686, 0xFB7A, 4}, {0x0698, 0xFB8A, 2},
			{0x06A9, 0xFB8E, 4}, {0x06AF, 0xFB92, 4}, {0x06C0, 0xFBA4, 2}, {0x06CC, 0xFBFC, 4},
		};

		const Shape *shapeOf(Char c)
		{
			for (const Shape &shape : shapes)
				if (shape.letter == c)
					return &shape;
			return nullptr;
		}

		// Joining_Type: 'D' dual, 'R' right, 'C' join causing, 'T' transparent, 'U' none.
		char joiningOf(Char c)
		{
			if (arabicMark(c))
				return 'T';
			if (c == 0x0640 || c == 0x200D)
				return 'C';
			const Shape *shape = shapeOf(c);
			return !shape || shape->forms == 1 ? 'U' : shape->forms == 2 ? 'R' : 'D';
		}

		Char lamAlef(Char alef)
		{
			switch (alef)
			{
				case 0x0622: return 0xFEF5;
				case 0x0623: return 0xFEF7;
				case 0x0625: return 0xFEF9;
				case 0x0627: return 0xFEFB;
				default: return 0;
			}
		}

		std::vector<Char> shapeArabic(const std::vector<Char> &text)
		{
			auto neighbour = [&](size_t i, int step) -> char {
				for (long j = static_cast<long>(i) + step; j >= 0 && j < static_cast<long>(text.size()); j += step)
				{
					const char type = joiningOf(text[j]);
					if (type != 'T')
						return type;
				}
				return 'U';
			};
			std::vector<Char> result;
			result.reserve(text.size());
			for (size_t i = 0; i < text.size(); ++i)
			{
				const Shape *shape = shapeOf(text[i]);
				if (!shape || shape->forms == 1)
				{
					result.push_back(text[i]);
					continue;
				}
				const char before = neighbour(i, -1);
				const bool joinsBefore = before == 'D' || before == 'C';
				if (text[i] == 0x0644 && i + 1 < text.size() && lamAlef(text[i + 1]))
				{
					result.push_back(lamAlef(text[i + 1]) + (joinsBefore ? 1 : 0));
					++i;
					continue;
				}
				const char after = neighbour(i, 1);
				const bool joinsAfter = shape->forms == 4 && (after == 'D' || after == 'R' || after == 'C');
				const int form = joinsBefore ? (joinsAfter ? 3 : 1) : (joinsAfter ? 2 : 0);
				// Alef maksura's initial and medial forms live outside the FExx block.
				if (text[i] == 0x0649 && form >= 2)
					result.push_back(0xFBE8 + form - 2);
				else
					result.push_back(shape->isolated + form);
			}
			return result;
		}

		Char mirror(Char c)
		{
			static const Char pairs[][2] = {
				{'(', ')'}, {'[', ']'}, {'{', '}'}, {'<', '>'}, {0xAB, 0xBB}, {0x2039, 0x203A}, {0x2045, 0x2046},
				{0x207D, 0x207E}, {0x208D, 0x208E}, {0x2264, 0x2265}, {0x2329, 0x232A}, {0x3008, 0x3009},
				{0x300A, 0x300B}, {0x300C, 0x300D}, {0x300E, 0x300F}, {0x3010, 0x3011}, {0xFF08, 0xFF09},
				{0xFF1C, 0xFF1E}, {0xFF3B, 0xFF3D}, {0xFF5B, 0xFF5D},
			};
			for (const auto &pair : pairs)
			{
				if (c == pair[0])
					return pair[1];
				if (c == pair[1])
					return pair[0];
			}
			return c;
		}

		Char closingBracket(Char c)
		{
			switch (c)
			{
				case '(': return ')';
				case '[': return ']';
				case '{': return '}';
				case 0xFF08: return 0xFF09;
				case 0xFF3B: return 0xFF3D;
				case 0xFF5B: return 0xFF5D;
				default: return 0;
			}
		}

		// N0-N2 treat numbers as right-to-left.
		BidiClass strongForNeutrals(BidiClass type)
		{
			return type == EN || type == AN ? R : type;
		}
	}

	std::string visualOrder(const std::string &logical)
	{
		const std::vector<Char> source = decode(logical);
		if (std::none_of(source.begin(), source.end(), [](Char c) {
			// Like fribidi, invisible formatting characters are dropped even from left-to-right text.
			const BidiClass type = classOf(c);
			return type == R || type == AL || type == AN || type == BN || c == 0x200E;
		}))
			return logical;

		const std::vector<Char> text = shapeArabic(source);
		const size_t count = text.size();
		std::vector<BidiClass> original(count), types(count);
		for (size_t i = 0; i < count; ++i)
			original[i] = types[i] = classOf(text[i]);

		// P2-P3: the first strong character sets the paragraph direction.
		int base = 0;
		for (BidiClass type : original)
			if (type == L || type == R || type == AL)
			{
				base = type == L ? 0 : 1;
				break;
			}
		const BidiClass embedding = base ? R : L;

		// X9: boundary neutrals take no part in the remaining rules.
		std::vector<size_t> index;
		for (size_t i = 0; i < count; ++i)
			if (types[i] != BN)
				index.push_back(i);
		auto at = [&](size_t k) -> BidiClass & { return types[index[k]]; };
		const size_t n = index.size();

		// W1-W3
		for (size_t k = 0; k < n; ++k)
			if (at(k) == NSM)
				at(k) = k ? at(k - 1) : embedding;
		BidiClass lastStrong = embedding;
		for (size_t k = 0; k < n; ++k)
		{
			if (at(k) == L || at(k) == R || at(k) == AL)
				lastStrong = at(k);
			else if (at(k) == EN && lastStrong == AL)
				at(k) = AN;
		}
		for (size_t k = 0; k < n; ++k)
			if (at(k) == AL)
				at(k) = R;
		// W4
		for (size_t k = 1; k + 1 < n; ++k)
		{
			if (at(k) == ES && at(k - 1) == EN && at(k + 1) == EN)
				at(k) = EN;
			else if (at(k) == CS && at(k - 1) == at(k + 1) && (at(k - 1) == EN || at(k - 1) == AN))
				at(k) = at(k - 1);
		}
		// W5
		for (size_t k = 0; k < n; ++k)
		{
			if (at(k) != ET)
				continue;
			size_t end = k;
			while (end < n && at(end) == ET)
				++end;
			if ((k > 0 && at(k - 1) == EN) || (end < n && at(end) == EN))
				for (size_t j = k; j < end; ++j)
					at(j) = EN;
			k = end - 1;
		}
		// W6-W7
		for (size_t k = 0; k < n; ++k)
			if (at(k) == ES || at(k) == ET || at(k) == CS)
				at(k) = ON;
		lastStrong = embedding;
		for (size_t k = 0; k < n; ++k)
		{
			if (at(k) == L || at(k) == R)
				lastStrong = at(k);
			else if (at(k) == EN && lastStrong == L)
				at(k) = L;
		}

		// N0: paired brackets take the direction of their content or context.
		std::vector<std::pair<size_t, size_t>> brackets;
		{
			std::vector<std::pair<Char, size_t>> open;
			for (size_t k = 0; k < n; ++k)
			{
				if (at(k) != ON)
					continue;
				const Char c = text[index[k]];
				if (closingBracket(c))
				{
					if (open.size() == 63)
						break;
					open.push_back({closingBracket(c), k});
				}
				else
					for (size_t depth = open.size(); depth-- > 0;)
						if (open[depth].first == c)
						{
							brackets.push_back({open[depth].second, k});
							open.resize(depth);
							break;
						}
			}
			std::sort(brackets.begin(), brackets.end());
		}
		for (const auto &pair : brackets)
		{
			BidiClass found = ON;
			for (size_t k = pair.first + 1; k < pair.second; ++k)
			{
				const BidiClass type = strongForNeutrals(at(k));
				if (type == embedding)
				{
					found = embedding;
					break;
				}
				if (type == L || type == R)
					found = type;
			}
			if (found == ON)
				continue;
			if (found != embedding)
			{
				BidiClass before = embedding;
				for (size_t k = pair.first; k-- > 0;)
				{
					const BidiClass type = strongForNeutrals(at(k));
					if (type == L || type == R)
					{
						before = type;
						break;
					}
				}
				found = before == found ? found : embedding;
			}
			for (size_t k : {pair.first, pair.second})
			{
				at(k) = found;
				for (size_t j = k + 1; j < n && original[index[j]] == NSM; ++j)
					at(j) = found;
			}
		}

		// N1-N2
		for (size_t k = 0; k < n; ++k)
		{
			if (at(k) != ON && at(k) != WS && at(k) != S && at(k) != B)
				continue;
			size_t end = k;
			while (end < n && (at(end) == ON || at(end) == WS || at(end) == S || at(end) == B))
				++end;
			const BidiClass before = k ? strongForNeutrals(at(k - 1)) : embedding;
			const BidiClass after = end < n ? strongForNeutrals(at(end)) : embedding;
			const BidiClass resolved = before == after ? before : embedding;
			for (size_t j = k; j < end; ++j)
				at(j) = resolved;
			k = end - 1;
		}

		// I1-I2, with boundary neutrals taking the level of what precedes them.
		std::vector<int> levels(count, base);
		for (size_t i = 0; i < count; ++i)
		{
			if (types[i] == BN)
			{
				levels[i] = i ? levels[i - 1] : base;
				continue;
			}
			if (base == 0)
				levels[i] = types[i] == R ? 1 : types[i] == AN || types[i] == EN ? 2 : 0;
			else
				levels[i] = types[i] == L || types[i] == EN || types[i] == AN ? 2 : 1;
		}
		// L1: separators, and whitespace before them or at the line end, return to the base level.
		bool trailing = true;
		for (size_t i = count; i-- > 0;)
		{
			const BidiClass type = original[i];
			if (type == S || type == B)
			{
				levels[i] = base;
				trailing = true;
			}
			else if (trailing && (type == WS || type == BN))
				levels[i] = base;
			else
				trailing = false;
		}

		// L4 then L2.
		std::vector<Char> visual(text);
		for (size_t i = 0; i < count; ++i)
			if (levels[i] & 1)
				visual[i] = mirror(visual[i]);
		std::vector<size_t> order(count);
		for (size_t i = 0; i < count; ++i)
			order[i] = i;
		const int highest = count ? *std::max_element(levels.begin(), levels.end()) : 0;
		for (int level = highest; level >= 1; --level)
			for (size_t i = 0; i < count;)
			{
				if (levels[order[i]] < level)
				{
					++i;
					continue;
				}
				size_t end = i;
				while (end < count && levels[order[end]] >= level)
					++end;
				std::reverse(order.begin() + i, order.begin() + end);
				i = end;
			}

		// Like fribidi, keep combining marks after their base in right-to-left runs.
		for (size_t i = 0; i < count; ++i)
		{
			if (original[order[i]] != NSM || !(levels[order[i]] & 1))
				continue;
			// Boundary neutrals between a mark and its base are dropped from the output anyway.
			size_t end = i;
			while (end < count && (original[order[end]] == NSM || original[order[end]] == BN) && (levels[order[end]] & 1))
				++end;
			std::reverse(order.begin() + i, order.begin() + std::min(end + 1, count));
			i = end;
		}

		std::string result;
		result.reserve(logical.size());
		for (size_t i : order)
			if (original[i] != BN && text[i] != 0x200E && text[i] != 0x200F && text[i] != 0x061C)
				encode(visual[i], result);
		return result;
	}
}
