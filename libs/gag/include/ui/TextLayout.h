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
	// Positive UTF-8 byte ends of indivisible shaping clusters. Test measurers
	// default to code points; production adapters use the actual shaped font.
	virtual std::vector<std::size_t> clusterEnds(FontRole role, const std::string &text) const;
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

// UTF-8 code-point stepping used by existing text controls. Shaping-cluster
// boundaries for wrapping/ellipsis are queried separately through the measurer.
std::size_t nextGlyph(const std::string &text, std::size_t at);
std::size_t previousGlyph(const std::string &text, std::size_t at);
std::size_t glyphCount(const std::string &text);

// Validated shaping-cluster byte ends. Invalid font segmentation conservatively
// leaves the complete run intact rather than splitting combining marks.
std::vector<std::size_t> shapingClusterEnds(const TextMeasurer &measurer, FontRole role,
										  const std::string &text);

// Word wrap that breaks unspaced runs at shaping-cluster boundaries and keeps CJK closing punctuation
// with its preceding character. U+200B hints prefer word boundaries without
// inserting visible spaces. Explicit newlines always break. A cluster wider
// than the available width remains whole and can overflow; layout reports that width.
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
