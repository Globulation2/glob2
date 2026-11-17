// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_rect.h>
#include "render/AnimationClock.h"
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

    // Programmable devices without streamed textures: one native direct-terrain
    // packet for the view.
    bool draw(GAGCore::GraphicContext &, const SceneMap &, int left, int top,
              int right, int bottom, int viewportX, int viewportY,
              Uint32 visibleTeams, bool wholeMap, AnimationTime animationTime);
    bool preparing() const;
    // A device or budget limit stopped direct terrain for this map.
    bool failed() const;
    // failed(), for this map: another map starts again.
    bool failedFor(const SceneMap &) const;
    // Legacy GL, at every zoom: artwork near the display density and a
    // coverage mask per visible mixed cell, built in the background (a mixed
    // cell draws a straight split between its corners until its mask lands). Returns false
    // until the map's per-cell tables are ready. A complete prepare (editor,
    // captures) builds the tables and every visible mask now and waits for
    // streamed HD artwork.
    bool prepare(GAGCore::GraphicContext &, const SceneMap &, int left, int top,
                 int right, int bottom, int viewportX, int viewportY,
                 Uint32 visibleTeams, bool wholeMap, AnimationTime animationTime, bool complete = false);
    std::span<const SDL_Rect> preparedRegions() const;
    // Game loading: builds the map's per-cell tables and its whole-map base
    // masks now, so the first frame finds them (SDL GPU and Legacy GL).
    void preload(GAGCore::GraphicContext &, const SceneMap &);
    // Draw the prepared view, with the time and view prepare used.
    void drawPrepared(GAGCore::GraphicContext &, int viewportX, int viewportY,
                      bool wholeMap, AnimationTime animationTime);
    // Mixed visible cells still waiting for their masks after the last prepare.
    std::size_t pendingHdMasks() const;
    // Mixed cells of the whole map whose low-density base mask is still missing.
    std::size_t pendingBaseMasks() const;
    // Visible mixed cells the last prepare left to a corner split: no mask at
    // the view's density, the previous one or the base.
    std::size_t splitCells() const;
    // Whether this build has the Legacy GL pass. Browser builds drive Legacy
    // GL through Emscripten's WebGL2 bridge, which it does not support; their
    // Legacy GL views compose pages instead.
    static bool legacyPassAvailable();
    // Whether this graphics context's device draws the same streamed pass
    // (prepare/drawPrepared) through streamed textures: SDL GPU and Programmable GL.
    static bool streamedPassAvailable(GAGCore::GraphicContext &);
    // Nothing of this map can draw yet: show the overview.
    bool needsBackdrop(GAGCore::GraphicContext &, const SceneMap &) const;
    std::size_t textureBytes() const;
    // How long the mask atlas a zoom retired stays allocated as a spare once
    // it has nothing left to show. A run of zooms reuses it; a still view
    // gives its memory back.
    unsigned spareLifetimeMs = 10000;
    std::size_t uploadBytes() const;
    unsigned draws() const;
    // Times a device lost this view's streamed textures and they started again.
    unsigned restartCount() const;
    struct DiscoveryMetrics { std::size_t rebuilt = 0; };
    DiscoveryMetrics discoveryMetrics() const;
    // The last prepare's visible variants: how many draw the resident
    // low-resolution art (absent from the drawing atlas, as during a zoom) and
    // how many records point at a tile the atlas does not hold for them (stale:
    // another variant's art; always zero). lowArtBytes: that art's texture.
    struct ArtMetrics { std::size_t visible = 0, absent = 0, stale = 0, lowArtBytes = 0; };
    ArtMetrics artMetrics() const;
    //! Texture units the last Legacy GL draw bound to its 1x1 placeholder
    //! because their surface had no texture yet (the base before it is built).
    unsigned placeholderBindings() const;

private:
    void prepareHdMasks(const SceneMap &, int left, int top, int right, int bottom,
                        int viewportX, int viewportY, Uint32 visibleTeams, bool wholeMap, int tile, bool complete);
    bool drawInternal(GAGCore::GraphicContext &, const SceneMap &, int left, int top,
                      int right, int bottom, int viewportX, int viewportY,
                      Uint32 visibleTeams, bool wholeMap, AnimationTime animationTime,
                      bool complete, bool prepareOnly);
    struct State;
    std::unique_ptr<State> state;
    unsigned restarts = 0;
};
