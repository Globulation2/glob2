// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_rect.h>
#include <cstddef>
#include <memory>
#include <span>

class SceneMap;
namespace GAGCore { class GraphicContext; }

// Owns one view's terrain atlas and Legacy GL animation sources for a map and
// graphics generation. Visible coverage publishes incrementally over the overview.
class DirectTerrain
{
public:
    DirectTerrain();
    ~DirectTerrain();

    // Legacy GL may restrict the draw to tile rectangles with missing HD pages.
    bool draw(GAGCore::GraphicContext &, const SceneMap &, int left, int top,
              int right, int bottom, int viewportX, int viewportY,
              Uint32 visibleTeams, bool wholeMap, int animationTime,
              std::span<const SDL_Rect> regions = {});
    bool preparing() const;
    // Prepare a small visible-source atlas for per-frame HD animation.
    // Returns false until the native map metadata and coverage are ready.
    // everyCell (experimental, GLOB2_TERRAIN_DIRECT_HD): draw every visible
    // cell in this pass, with full-detail masks for mixed cells, instead of
    // only the animated ones over the static pages.
    bool prepareAnimation(GAGCore::GraphicContext &, const SceneMap &, int left, int top,
                          int right, int bottom, int viewportX, int viewportY,
                          Uint32 visibleTeams, bool wholeMap, int animationTime,
                          bool everyCell = false);
    std::span<const SDL_Rect> animationRegions() const;
    // Draw after the static page cells, with the time/view used by prepareAnimation.
    void drawAnimation(GAGCore::GraphicContext &, int viewportX, int viewportY,
                       bool wholeMap, int animationTime);
    // Whether the experimental every-cell HD pass is requested.
    static bool directHdRequested();
    // Mixed visible cells still drawn with native masks after the last prepare.
    std::size_t pendingHdMasks() const;
    bool needsBackdrop(GAGCore::GraphicContext &, const SceneMap &, int left, int top, int right, int bottom) const;
    std::size_t textureBytes() const;
    std::size_t uploadBytes() const;
    unsigned draws() const;
    struct DiscoveryMetrics { std::size_t rebuilt = 0; };
    DiscoveryMetrics discoveryMetrics() const;

private:
    void prepareHdMasks(const SceneMap &, int left, int top, int right, int bottom,
                        int viewportX, int viewportY, Uint32 visibleTeams, bool wholeMap, int tile,
                        std::span<const SDL_Rect> only = {});
    bool drawInternal(GAGCore::GraphicContext &, const SceneMap &, int left, int top,
                      int right, int bottom, int viewportX, int viewportY,
                      Uint32 visibleTeams, bool wholeMap, int animationTime,
                      std::span<const SDL_Rect>, bool prepareOnly);
    struct State;
    std::unique_ptr<State> state;
};
