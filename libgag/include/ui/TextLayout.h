// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Theme.h"
#include <string>
#include <vector>

namespace GAGGUI::ui
{
// Layout measures text through this interface so it can run without fonts.
class TextMeasurer
{
  public:
	virtual ~TextMeasurer() = default;
	virtual int width(FontRole role, const std::string &text) const = 0;
	virtual int lineHeight(FontRole role) const = 0;
};

// Fixed-advance measurer for tests: every glyph is `glyphWidth` wide.
class FixedTextMeasurer : public TextMeasurer
{
  public:
	explicit FixedTextMeasurer(int glyphWidth = 8, int height = 16)
		: glyphWidth(glyphWidth), height(height)
	{
	}
	int width(FontRole, const std::string &text) const override;
	int lineHeight(FontRole) const override { return height; }

  private:
	int glyphWidth, height;
};

// UTF-8 glyph stepping shared by every text control.
std::size_t nextGlyph(const std::string &text, std::size_t at);
std::size_t previousGlyph(const std::string &text, std::size_t at);
std::size_t glyphCount(const std::string &text);

// Word wrap that also breaks unspaced runs and keeps CJK closing punctuation
// with its preceding character. Explicit newlines always break.
std::vector<std::string> wrapText(const TextMeasurer &measurer, FontRole role,
								  const std::string &text, int width);
// Truncate with an ellipsis when the text is wider than `width`.
std::string ellipsize(const TextMeasurer &measurer, FontRole role, const std::string &text,
					  int width);

struct TextBlock
{
	std::vector<std::string> lines;
	int width = 0, height = 0;
};
TextBlock layoutText(const TextMeasurer &measurer, FontRole role, const std::string &text,
					 int maxWidth, int lineGap);
} // namespace GAGGUI::ui
