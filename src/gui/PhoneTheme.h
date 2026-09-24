// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <GraphicContext.h>
#include "FrontendTheme.h"

namespace PhoneTheme
{
inline const GAGCore::Color ink{36,69,49};
inline const GAGCore::Color paper{240,241,223};
inline const GAGCore::Color field{249,250,240};
inline const GAGCore::Color selected{228,199,121};
inline const GAGCore::Color border{190,203,176};
inline constexpr double textScale=1.15;
// Scope font color so drawing a phone panel never restyles a desktop widget.
class TextStyle
{
    GAGCore::Font* font;
public:
    explicit TextStyle(GAGCore::Font* font):font(font) { font->pushStyle(GAGCore::Font::Style(GAGCore::Font::STYLE_NORMAL,ink)); }
    ~TextStyle() { font->popStyle(); }
};
}
