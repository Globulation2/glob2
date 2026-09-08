// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <ViewportTransform.h>
#include <vector>

namespace GAGCore
{
struct ResponsiveMenu
{
    ViewRect content;
    std::vector<ViewRect> buttons;
    double offset = 0, maximumOffset = 0;
    static ResponsiveMenu calculate(ViewRect safe, size_t count, double minimumWidth,
                                    double requestedOffset = 0, double scale = 1, double minimumHeight = 48)
    {
        ResponsiveMenu result;
        const double gap = 8 * scale, rowHeight = std::max(48.0, minimumHeight) * scale;
        const double width = std::max(0.0, safe.w - 2 * gap);
        result.content = {safe.x + gap, safe.y + 56 * scale, width,
                          std::max(0.0, safe.h - 64 * scale)};
        const int columns = width >= 2 * minimumWidth + gap ? 2 : 1;
        const size_t rows = (count + columns - 1) / columns;
        const double extent = rows ? rows * (rowHeight + gap) - gap : 0;
        result.maximumOffset = std::max(0.0, extent - result.content.h);
        result.offset = std::clamp(requestedOffset, 0.0, result.maximumOffset);
        const double buttonWidth = (width - (columns - 1) * gap) / columns;
        for (size_t i = 0; i < count; ++i)
            result.buttons.push_back({result.content.x + (i % columns) * (buttonWidth + gap),
                result.content.y + (i / columns) * (rowHeight + gap) - result.offset,
                buttonWidth, rowHeight});
        return result;
    }
    int hit(ViewPoint point) const
    {
        if (!content.contains(point)) return -1;
        for (size_t i = 0; i < buttons.size(); ++i)
            if (buttons[i].contains(point)) return static_cast<int>(i);
        return -1;
    }
};
}
