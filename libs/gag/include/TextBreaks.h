// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>

namespace GAGCore
{
// Catalog-authored optional word boundaries. Keep them for wrapping, but never
// submit them to the rasterizer: font fallback must not turn a hint into ink.
inline constexpr const char *softWordBreak = "\xe2\x80\x8b";
inline std::string withoutSoftWordBreaks(const std::string &text)
{
    std::string result;
    result.reserve(text.size());
    std::size_t start = 0;
    for (auto at = text.find(softWordBreak); at != std::string::npos;
         at = text.find(softWordBreak, start)) {
        result.append(text, start, at - start);
        start = at + 3;
    }
    result.append(text, start, std::string::npos);
    return result;
}
} // namespace GAGCore
