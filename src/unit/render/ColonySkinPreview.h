// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SkinMesh.h>
#include "online/SwarmMeshCatalog.h"
#include <array>
#include <memory>
#include <optional>
struct PresentationFrame;
class FogFade;
namespace Online { class SkinDownloads; class SkinSprites; struct AuthorizedSkin; }
namespace GAGCore { class GraphicContext; class DrawableSurface; }

// Live colony geometry and authorized per-team appearance (layout colony-v2:
// a 512x512 colour atlas plus a 512x512 material-id map per team, one 256x256
// quadrant per model). Assets are view-owned, never part of simulation state
// or persisted game data.
class ColonySkinPreview
{
public:
    ColonySkinPreview();
    ~ColonySkinPreview();
    void setDownloads(std::unique_ptr<Online::SkinDownloads> downloads);
    void poll();
    void setVisible(bool value) { visible = value; }
    void prepare(GAGCore::GraphicContext &gfx, const PresentationFrame &scene, int left, int top,
                 int right, int bottom, int viewportX, int viewportY,
                 int localTeam, std::uint32_t visibleTeams, bool wholeMap, float unitMotion, bool drawUnits = true, bool drawBuildings = true, const FogFade *fogFade = nullptr);
    std::optional<std::uint32_t> buildingColor(int team) const;
    bool draw(GAGCore::GraphicContext &gfx, int type, int team, int action,
              int direction, int delta, float x, float y, GAGCore::DrawableSurface *shadow = nullptr, std::uint8_t alpha = 255);
    bool drawSwarm(GAGCore::GraphicContext &gfx, int team, float x, float y,
                   float width, float height, std::uint8_t alpha = 255);
private:
    static unsigned unitClip(int type, int action);
    const GAGCore::SkinMesh *unitMesh(int type, int action) const;
    // The team's chosen swarm mesh, or null when it is unavailable.
    const GAGCore::SkinMesh *swarmMesh(int team) const;
    static std::uint8_t unitRegion(int type);
    // Installs both surfaces for a team, or neither when either is not 512x512.
    bool install(int team, std::unique_ptr<GAGCore::DrawableSurface> texture,
                 std::unique_ptr<GAGCore::DrawableSurface> material);
    void uninstall(int team);
    void prepareSkin(Online::AuthorizedSkin appearance, const std::string &paintPath, const std::string &materialPath);
    bool loadMeshes(const std::string &root, bool installed = false);
    bool loadInstalledMeshes();
    struct PreparedSkin;
    std::shared_ptr<void> assetLifetime = std::make_shared<int>(0);
    std::array<GAGCore::AssetLoader::Handle<PreparedSkin>, 32> preparingSkins;
    std::unique_ptr<Online::SkinDownloads> downloads;
    std::unique_ptr<Online::SkinSprites> sprites;
    std::array<std::unique_ptr<GAGCore::DrawableSurface>,32> textures;
    std::array<std::unique_ptr<GAGCore::DrawableSurface>,32> materials;
    std::array<std::optional<std::uint32_t>,32> colors;
    std::array<int,32> swarmChoice{};
    std::array<unsigned,32> swarmAngles{};
    mutable std::array<GAGCore::SkinMesh,32> orientedSwarms;
    bool attemptedMeshes = false;
    std::array<GAGCore::SkinMesh, Online::SWARM_MESHES.size()> swarms;
    bool visible = true;
    bool ready = false;
    std::array<GAGCore::SkinMesh, 7> clips;
};
