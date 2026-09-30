// SPDX-License-Identifier: GPL-3.0-or-later
#include <ui/TextLayout.h>
#include <algorithm>

namespace GAGGUI::ui
{
namespace
{
bool continuation(unsigned char c) { return (c & 0xc0) == 0x80; }
// Closing punctuation stays with the preceding glyph; opening brackets with the next.
const std::string closingPunctuation = "。、，．！？：；）］｝〉》」』】〕〗〙〛";
const std::string openingPunctuation = "（［｛〈《「『【〔〖〘〚";
} // namespace

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

std::vector<std::string> wrapText(const TextMeasurer &measurer, FontRole role,
								  const std::string &text, int width)
{
	width = std::max(1, width);
	std::vector<std::string> lines;
	std::string line, word;
	auto flush = [&]
	{
		lines.push_back(line);
		line.clear();
	};
	auto appendGlyphs = [&](const std::string &run)
	{
		for (std::size_t at = 0; at < run.size();)
		{
			const std::size_t end = nextGlyph(run, at);
			const std::string glyph = run.substr(at, end - at);
			if (!line.empty() && measurer.width(role, line + glyph) > width)
			{
				const std::size_t last = previousGlyph(line, line.size());
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
		if (!line.empty() && measurer.width(role, line + " " + word) > width)
			flush();
		if (!line.empty())
			line += ' ';
		if (measurer.width(role, word) <= width && (line.empty() || measurer.width(role, line + word) <= width))
			line += word;
		else
		{
			if (!line.empty() && line.back() == ' ')
				line.pop_back();
			if (!line.empty() && measurer.width(role, line + " ") <= width)
				line += ' ';
			appendGlyphs(word);
		}
		word.clear();
	};
	for (std::size_t i = 0; i <= text.size(); ++i)
	{
		const char c = i < text.size() ? text[i] : '\n';
		if (c == ' ' || c == '\n')
		{
			placeWord();
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

std::string ellipsize(const TextMeasurer &measurer, FontRole role, const std::string &text,
					  int width)
{
	if (measurer.width(role, text) <= width)
		return text;
	static const std::string ellipsis = "…";
	std::string value = text;
	while (!value.empty() && measurer.width(role, value + ellipsis) > width)
		value.resize(previousGlyph(value, value.size()));
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
	block.width = std::min(block.width, std::max(1, maxWidth));
	block.height = int(block.lines.size()) * height + int(block.lines.size() - 1) * lineGap;
	return block;
}
} // namespace GAGGUI::ui
