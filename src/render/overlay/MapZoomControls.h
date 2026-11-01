// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <MapCamera.h>
#include "GlobalContainer.h"
#include "ui/LibraryText.h"
#include <algorithm>
#include <array>
#include <ui/PositionedControls.h>
#include <cmath>
#include <string>

// Keep drawing and hit testing together. Longer translations can use the unused
// readout tail, but the bar never grows past its original 144 pixels or the sidebar.
struct MapZoomControlsLayout
{
    int x, y, width, height, middleWidth, readoutGap;
    int plus() const { return 24 + middleWidth; }
    int readout() const { return plus() + 24 + readoutGap; }
};

inline MapZoomControlsLayout mapZoomControlsLayout(const MapCamera &camera, bool sidebar, bool softwareMap)
{
    auto *gfx = globalContainer->gfx;
    auto *font = globalContainer->littleFont;
    const int x = sidebar ? int(camera.width) + 8 : 8;
    const int width = std::clamp(gfx->getW() - x - 8, 0, 144);
    if (width < 92) return {x, gfx->getH() - 4, width, 0, 44, 0};
    const std::string fit = Glob2UI::libraryText("Fit");
    const std::string readout = softwareMap || gfx->canDrawStretchedSprite() ? "100%" : Glob2UI::libraryText("GL only");
    const int gap = std::min(8, width - 92);
    // Reserve a fixed tail from a representative readout, not the current zoom.
    // Both middle captions contribute, so +/- hit boxes stay still after a click.
    const int tail = std::min(font->getStringWidth(readout) + 4, width - 92 - gap);
    const int middle = std::min(std::max(44, std::max(font->getStringWidth(fit), font->getStringWidth("100%")) + 10),
                                width - 48 - gap - tail);
    const int height = std::max(22, std::max({font->getStringHeight(fit), font->getStringHeight(readout),
                                            font->getStringHeight("-"), font->getStringHeight("+")}) + 6);
    return {x, gfx->getH() - height - 4, width, height, middle, gap};
}

// Shared translated geometry for painting, human hit testing and inspection.
inline std::array<GAGGUI::ui::Rect, 3> mapZoomControlBounds(
    const MapCamera &camera, bool sidebar = false, bool softwareMap = false)
{
    const auto box = mapZoomControlsLayout(camera, sidebar, softwareMap);
    return {{{box.x, box.y, 24, box.height},
             {box.x + 24, box.y, box.middleWidth, box.height},
             {box.x + box.plus(), box.y, 24, box.height}}};
}

// Fixed screen-space controls shared by gameplay, replays, and the editor.
inline void drawMapZoomControls(const MapCamera &camera, bool sidebar = false, bool softwareMap = false)
{
    auto *gfx = globalContainer->gfx;
    auto *font = globalContainer->littleFont;
    const auto box = mapZoomControlsLayout(camera, sidebar, softwareMap);
    if (!box.height) return;
    gfx->setClipRect(box.x, box.y, box.width, box.height);
    gfx->drawFilledRect(box.x, box.y, box.width, box.height, 20, 25, 35, 230);
    for (const auto &button : mapZoomControlBounds(camera, sidebar, softwareMap))
        gfx->drawRect(button.x, button.y, button.w, button.h, 120, 130, 150);
    auto caption = [&](int left, int width, int pad, const std::string &text)
    {
        const int rightPad = std::min(pad, 4);
        if (width <= pad + rightPad) return;
        const int textWidth = font->getStringWidth(text), textHeight = font->getStringHeight(text);
        const float scale = std::min(1.f, float(width - pad - rightPad) / std::max(1, textWidth));
        const SDL_Rect clip{box.x + left, box.y, width, box.height};
        const float top = scale == 1.f && box.height == 22 ? box.y + 3.f : box.y + (box.height - textHeight * scale) / 2.f;
        gfx->setUITransform(scale, float(box.x + left + pad), top, &clip);
        gfx->drawString(0, 0, font, text);
        gfx->setUITransform();
    };
    caption(0, 24, 8, "-");
    // At 100% the middle button offers the other fixed stop: the whole map.
    caption(24, box.middleWidth, 6, camera.zoom == 1.0 ? Glob2UI::libraryText("Fit") : "100%");
    caption(box.plus(), 24, 8, "+");
    caption(box.readout(), box.width - box.readout(), 0,
        (softwareMap || gfx->canDrawStretchedSprite())
            ? std::to_string(int(std::round(camera.zoom * 100))) + "%"
            : Glob2UI::libraryText("GL only"));
    gfx->setClipRect();
}

// The zoom holds the view's centre still, or the screen point (anchorX, anchorY) when given.
inline bool clickMapZoomControls(MapCamera &camera, int x, int y, bool sidebar = false, bool softwareMap = false,
                                 double anchorX = -1, double anchorY = -1)
{
    const auto box = mapZoomControlsLayout(camera, sidebar, softwareMap);
    const GAGGUI::ui::Point point{x, y};
    if (!GAGGUI::ui::Rect{box.x, box.y, box.width, box.height}.contains(point))
        return false;
    const auto buttons = mapZoomControlBounds(camera, sidebar, softwareMap);
    for (int i = 0; i < 3; ++i)
    if (buttons[i].contains(point) && (softwareMap || globalContainer->gfx->canDrawStretchedSprite()))
    {
        const double zoom = i == 0 ? camera.zoom / 1.1
                          : i == 2 ? camera.zoom * 1.1
                          : camera.zoom == 1.0 ? camera.minimumZoom() : 1.0;
        if (anchorX < 0 || anchorY < 0)
            anchorX = camera.width / 2, anchorY = camera.height / 2;
        camera.setZoom(zoom, anchorX, anchorY);
    }
    return true;
}

inline std::vector<GAGGUI::ui::PositionedControl> mapZoomControls(
    const MapCamera &camera, const std::string &prefix, std::function<void(int)> activate,
    bool sidebar = false, bool softwareMap = false)
{
    using namespace GAGGUI::ui;
    const auto bounds=mapZoomControlBounds(camera,sidebar,softwareMap);
    std::vector<PositionedControl> controls;
    const char *keys[]={"out","fit","in"};
    const std::string labels[]={"Zoom out", camera.zoom == 1.0 ? Glob2UI::libraryText("Fit") : "100%", "Zoom in"};
    for(int i=0;i<3;++i)
    {
        PositionedControl c;
        c.key=prefix+"/"+keys[i];c.label=labels[i];c.bounds=bounds[i];
        c.enabled=softwareMap || globalContainer->gfx->canDrawStretchedSprite();
        c.tap=[activate,i](Point){activate(i);};
        controls.push_back(std::move(c));
    }
    return controls;
}
