// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SkinMesh.h>
#include <array>
#include <memory>
namespace GAGCore { class GraphicContext; class DrawableSurface; }

// Developer-only proof: team zero's units use original-derived live meshes.
// Assets are view-owned, never part of simulation state or persisted game data.
class ColonySkinPreview
{
public:
    ColonySkinPreview();
    ~ColonySkinPreview();
    bool draw(GAGCore::GraphicContext &gfx, int type, int team, int action,
              int direction, int delta, float x, float y);
    bool drawSwarm(GAGCore::GraphicContext &gfx, int team, float x, float y,
                   float width, float height);
private:
    GAGCore::SkinMesh swarm;
    bool ready = false;
    std::array<GAGCore::SkinMesh, 7> clips;
    std::unique_ptr<GAGCore::DrawableSurface> texture;
};
