// SPDX-License-Identifier: GPL-3.0-or-later
#include <ui/TextLayout.h>
#include <algorithm>
#include <TextBreaks.h>

namespace GAGGUI::ui
{
namespace
{
bool continuation(unsigned char c) { return (c & 0xc0) == 0x80; }
// Closing punctuation stays with the preceding glyph; opening brackets with the next.
const std::string closingPunctuation = "。、，．！？：；）］｝〉》」』】〕〗〙〛";
const std::string openingPunctuation = "（［｛〈《「『【〔〖〘〚";
} // namespace

// Malformed/unknown font segmentation must not fall back to splitting marks.
std::vector<std::size_t> shapingClusterEnds(const TextMeasurer &measurer, FontRole role,
                                           const std::string &text)
{
    if (text.empty()) return {};
    auto ends = measurer.clusterEnds(role, text);
    std::size_t previous = 0;
    for (const auto end : ends) {
        if (end <= previous || end > text.size() ||
            (end < text.size() && continuation(static_cast<unsigned char>(text[end]))))
            return {text.size()};
        previous = end;
    }
    if (ends.empty() || ends.back() != text.size()) ends.push_back(text.size());
    return ends;
}

int FixedTextMeasurer::width(FontRole, const std::string &text) const
{
	return int(glyphCount(text)) * glyphWidth;
}

std::size_t nextGlyph(const std::string &text, std::size_t at)
{
	if (at >= text.size())
		return text.size();
	std::size_t next = at + 1;
	while (next < text.size() && continuation(static_cast<unsigned char>(text[next])))
		++next;
	return next;
}

std::size_t previousGlyph(const std::string &text, std::size_t at)
{
	if (at == 0)
		return 0;
	std::size_t previous = std::min(at, text.size()) - 1;
	while (previous > 0 && continuation(static_cast<unsigned char>(text[previous])))
		--previous;
	return previous;
}

std::size_t glyphCount(const std::string &text)
{
	std::size_t count = 0;
	for (std::size_t at = 0; at < text.size(); at = nextGlyph(text, at))
		++count;
	return count;
}

std::vector<std::size_t> TextMeasurer::clusterEnds(FontRole, const std::string &text) const
{
    std::vector<std::size_t> ends;
    for (std::size_t at = 0; at < text.size();) {
        at = nextGlyph(text, at);
        ends.push_back(at);
    }
    return ends;
}

std::vector<std::string> wrapText(const TextMeasurer &measurer, FontRole role,
								  const std::string &text, int width)
{
	width = std::max(1, width);
	std::vector<std::string> lines;
	std::string line, word;
	bool pendingSpace = true;
	auto flush = [&]
	{
		lines.push_back(line);
		line.clear();
	};
	auto appendClusters = [&](const std::string &run)
	{
		std::size_t at = 0;
		for (const auto end : shapingClusterEnds(measurer, role, run))
		{
			const std::string glyph = run.substr(at, end - at);
			if (!line.empty() && measurer.width(role, line + glyph) > width)
			{
				const auto lineEnds = shapingClusterEnds(measurer, role, line);
				const std::size_t last = lineEnds.size() > 1 ? lineEnds[lineEnds.size() - 2] : 0;
				const std::string tail = line.substr(last);
				if (last > 0 && (closingPunctuation.find(glyph) != std::string::npos ||
								 openingPunctuation.find(tail) != std::string::npos))
				{
					line.resize(last);
					flush();
					line = tail;
				}
				else
					flush();
			}
			line += glyph;
			at = end;
		}
	};
	auto placeWord = [&]
	{
		if (word.empty())
			return;
		const std::string separator = pendingSpace ? " " : "";
		if (!line.empty() && measurer.width(role, line + separator + word) > width)
			flush();
		if (!line.empty())
			line += separator;
		if (measurer.width(role, word) <= width && (line.empty() || measurer.width(role, line + word) <= width))
			line += word;
		else
		{
			if (!line.empty() && line.back() == ' ')
				line.pop_back();
			if (pendingSpace && !line.empty() && measurer.width(role, line + " ") <= width)
				line += ' ';
			appendClusters(word);
		}
		word.clear();
	};
	for (std::size_t i = 0; i <= text.size(); ++i)
	{
		if (text.compare(i, 3, GAGCore::softWordBreak) == 0)
		{
			if (!word.empty())
			{
				placeWord();
				pendingSpace = false;
			}
			i += 2;
			continue;
		}
		const char c = i < text.size() ? text[i] : '\n';
		// A hyphen inside a word is a break opportunity after it ("Discord-|Community"),
		// so a long compound wraps there instead of mid-syllable. A minus sign or a
		// numeric range ("-5", "1-2") stays together.
		if (c == '-' && !word.empty() && word.back() != '-' && i + 1 < text.size() &&
			text[i + 1] != ' ' && text[i + 1] != '\n' && text[i + 1] != '-' &&
			!(text[i + 1] >= '0' && text[i + 1] <= '9'))
		{
			word += c;
			placeWord();
			pendingSpace = false;
			continue;
		}
		if (c == ' ' || c == '\n')
		{
			placeWord();
			pendingSpace = true;
			if (c == '\n' && i < text.size())
				flush();
		}
		else
			word += c;
	}
	if (!line.empty() || lines.empty())
		flush();
	return lines;
}

bool rightToLeftParagraph(const std::string &text)
{
	for (std::size_t at = 0; at < text.size();)
	{
		const auto lead = static_cast<unsigned char>(text[at]);
		const std::size_t length = lead < 0x80 ? 1 : lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : lead >= 0xc0 ? 2 : 1;
		char32_t cp = length == 1 ? lead : lead & (0x7f >> length);
		for (std::size_t i = 1; i < length && at + i < text.size(); ++i)
			cp = (cp << 6) | (static_cast<unsigned char>(text[at + i]) & 0x3f);
		at += length;
		// Hebrew, Arabic, Syriac, Thaana, NKo, Samaritan, Mandaic and their
		// presentation forms, plus the supplementary right-to-left blocks.
		if ((cp >= 0x0590 && cp <= 0x08ff) || (cp >= 0xfb1d && cp <= 0xfdff) ||
			(cp >= 0xfe70 && cp <= 0xfefe) || (cp >= 0x10800 && cp <= 0x10fff) ||
			(cp >= 0x1e800 && cp <= 0x1efff))
			return true;
		const bool latin = (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z');
		// Other letters: Latin-1 and later alphabets, Indic, CJK and the rest,
		// skipping combining marks, general punctuation, symbols and CJK punctuation.
		const bool otherLetter = cp >= 0xc0 && cp != 0xd7 && cp != 0xf7 &&
			!(cp >= 0x0300 && cp <= 0x036f) && !(cp >= 0x2000 && cp <= 0x2bff) &&
			!(cp >= 0x3000 && cp <= 0x303f) && !(cp >= 0xfe00 && cp <= 0xfe6f) &&
			!(cp >= 0xff00 && cp <= 0xff20);
		if (latin || otherLetter)
			return false;
	}
	return false;
}

std::string ellipsize(const TextMeasurer &measurer, FontRole role, const std::string &text,
					  int width)
{
	if (text.find(GAGCore::softWordBreak) != std::string::npos)
		return ellipsize(measurer, role, GAGCore::withoutSoftWordBreaks(text), width);
	if (measurer.width(role, text) <= width)
		return text;
	static const std::string ellipsis = "…";
	// A slot too narrow for even the ellipsis shows nothing rather than overflow.
	if (measurer.width(role, ellipsis) > width)
		return {};
	const auto ends = shapingClusterEnds(measurer, role, text);
	std::size_t count = ends.size();
	std::string value = text;
	while (!value.empty() && measurer.width(role, value + ellipsis) > width)
	{
		--count;
		value.resize(count ? ends[count - 1] : 0);
	}
	return value + ellipsis;
}

TextBlock layoutText(const TextMeasurer &measurer, FontRole role, const std::string &text,
					 int maxWidth, int lineGap)
{
	TextBlock block;
	block.lines = wrapText(measurer, role, text, maxWidth);
	const int height = measurer.lineHeight(role);
	for (const auto &line : block.lines)
		block.width = std::max(block.width, measurer.width(role, line));
	// An indivisible cluster can be wider than its slot; expose the real extent.
	block.height = int(block.lines.size()) * height + int(block.lines.size() - 1) * lineGap;
	return block;
}
} // namespace GAGGUI::ui
