// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SkinMesh.h>
#include <array>
#include <memory>
#include <optional>
struct Scene;
namespace Online { class SkinDownloads; }
namespace GAGCore { class GraphicContext; class DrawableSurface; }

// Live colony geometry and authorized per-team appearance.
// Assets are view-owned, never part of simulation state or persisted game data.
class ColonySkinPreview
{
public:
    ColonySkinPreview();
    ~ColonySkinPreview();
    void setDownloads(std::unique_ptr<Online::SkinDownloads> downloads);
    void poll();
    void setVisible(bool value) { visible = value; }
    void prepare(GAGCore::GraphicContext &gfx, const Scene &scene, int left, int top,
                 int right, int bottom, int viewportX, int viewportY,
                 int localTeam, std::uint32_t visibleTeams, bool wholeMap);
    std::optional<std::uint32_t> buildingColor(int team) const;
    bool draw(GAGCore::GraphicContext &gfx, int type, int team, int action,
              int direction, int delta, float x, float y, GAGCore::DrawableSurface *shadow = nullptr);
    bool drawSwarm(GAGCore::GraphicContext &gfx, int team, float x, float y,
                   float width, float height);
private:
    const GAGCore::SkinMesh *unitMesh(int type, int action) const;
    bool loadMeshes(const std::string &root, bool installed = false);
    bool loadInstalledMeshes();
    std::unique_ptr<Online::SkinDownloads> downloads;
    std::array<std::unique_ptr<GAGCore::DrawableSurface>,32> textures;
    std::array<std::optional<std::uint32_t>,32> colors;
    bool attemptedMeshes = false;
    GAGCore::SkinMesh swarm;
    bool visible = true;
    bool ready = false;
    std::array<GAGCore::SkinMesh, 7> clips;
};
