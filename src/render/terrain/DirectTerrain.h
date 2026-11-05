// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <SDL3/SDL_stdinc.h>
#include <cstddef>
#include <memory>

class SceneMap;
namespace GAGCore { class GraphicContext; }

// Owns the native terrain atlas for one map and graphics generation.
// Legacy GL publishes visible coverage incrementally over the overview.
class DirectTerrain
{
public:
    DirectTerrain();
    ~DirectTerrain();

    bool draw(GAGCore::GraphicContext &, const SceneMap &, int left, int top,
              int right, int bottom, int viewportX, int viewportY,
              Uint32 visibleTeams, bool wholeMap, int animationTime);
    bool preparing() const;
    bool needsBackdrop(GAGCore::GraphicContext &, const SceneMap &, int left, int top, int right, int bottom) const;
    std::size_t textureBytes() const;
    std::size_t uploadBytes() const;
    unsigned draws() const;
    struct DiscoveryMetrics { std::size_t rebuilt = 0; };
    DiscoveryMetrics discoveryMetrics() const;

private:
    struct State;
    std::unique_ptr<State> state;
};
