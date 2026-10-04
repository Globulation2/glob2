// SPDX-License-Identifier: GPL-3.0-or-later
#include "ColonySkinPreview.h"
#include "render/FogFade.h"
#include "GlobalContainer.h"
#include "UnitConsts.h"
#include "UnitAnimation.h"
#include "UnitMotion.h"
#include "render/scene/Scene.h"
#include "BuildingType.h"
#include "IntBuildingType.h"
#include <GraphicContext.h>
#include <ApplicationHost.h>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <Toolkit.h>
#include <FileManager.h>
#include <StreamBackend.h>
#include "online/SkinDownloads.h"
#include <SDL3/SDL.h>
#include <ctime>

namespace
{
constexpr int AtlasSize = 512;
bool atlasSized(GAGCore::DrawableSurface &surface)
{
    return surface.getSDLSurface() && surface.getW() == AtlasSize && surface.getH() == AtlasSize;
}
// Which model quadrants contain hairy (id 3) texels.
std::array<bool,4> hairyRegions(GAGCore::DrawableSurface &material)
{
    std::array<bool,4> result{};
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> rgba(
        SDL_ConvertSurface(material.getSDLSurface(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    if (!rgba || !SDL_LockSurface(rgba.get())) return result;
    const auto *pixels = static_cast<const Uint8 *>(rgba->pixels);
    for (int y = 0; y < AtlasSize; ++y)
        for (int x = 0; x < AtlasSize; ++x)
            if (pixels[y*rgba->pitch + x*4] == 3)
                result[(y >= AtlasSize/2 ? 2 : 0) + (x >= AtlasSize/2 ? 1 : 0)] = true;
    SDL_UnlockSurface(rgba.get());
    return result;
}
}

ColonySkinPreview::ColonySkinPreview()
{
    const char *directory = std::getenv("GLOB2_SKIN_PREVIEW_DIR");
    if (!directory || !*directory) return;
    const std::string root(directory);
    if (globalContainer && globalContainer->gfx && (globalContainer->gfx->getOptionFlags() & GAGCore::GraphicContext::USEGPU)
        && GAGCore::ApplicationHost::assetPackageReady("skins")) ready = loadMeshes(root);
    // The material map is optional here: a missing one previews as all glossy
    // (a zero-filled surface); a present one must still be 512x512.
    auto material = GAGCore::loadSkinMaterialMap(root + "/material.png");
    if (!material) material = std::make_unique<GAGCore::DrawableSurface>(AtlasSize, AtlasSize);
    if (!install(0, std::make_unique<GAGCore::DrawableSurface>(root + "/paint.png"), std::move(material)))
        std::cerr << "Colony skin preview: paint.png and material.png must be " << AtlasSize << "x" << AtlasSize << '\n';
    if (const char *swarm = std::getenv("GLOB2_SKIN_PREVIEW_SWARM"))
    {
        const int index = Online::swarmMeshIndex(swarm);
        if (index < 0) std::cerr << "Colony skin preview: unknown swarm mesh " << swarm << '\n';
        swarmChoice[0] = std::max(0, index);
    }
}
bool ColonySkinPreview::install(int team, std::unique_ptr<GAGCore::DrawableSurface> texture,
                                std::unique_ptr<GAGCore::DrawableSurface> material)
{
    if (team < 0 || team >= 32 || !texture || !material || !atlasSized(*texture) || !atlasSized(*material))
        return false;
    hairy[team] = hairyRegions(*material);
    textures[team] = std::move(texture);
    materials[team] = std::move(material);
    return true;
}
void ColonySkinPreview::uninstall(int team)
{
    textures[team].reset();
    materials[team].reset();
    hairy[team] = {};
    colors[team].reset();
    swarmChoice[team] = 0;
}
bool ColonySkinPreview::loadMeshes(const std::string &root, bool installed)
{
    auto load = [&](GAGCore::SkinMesh &mesh, const std::string &path, std::string &error) {
        if (!installed) return mesh.load(path, error);
        std::unique_ptr<GAGCore::StreamBackend> input(GAGCore::Toolkit::getFileManager()->openInputStreamBackend(path));
        if (!input) { error="cannot open skin mesh"; return false; }
        return mesh.load(*input, error);
    };
    const char *names[] = {"worker-walk", "worker-swim", "worker-harvest",
                           "warrior-walk", "warrior-swim", "warrior-fight", "explorer-fly"};
    for (unsigned i = 0; i < clips.size(); ++i)
    {
        std::string error;
        if (!load(clips[i], root + "/" + names[i] + ".gsk", error))
        {
            std::cerr << "Colony skin preview: " << names[i] << ": " << error << '\n';
            return false;
        }
    }
    // A missing swarm mesh only leaves that choice on the classic sprite.
    for (unsigned i = 0; i < swarms.size(); ++i)
    {
        std::string error;
        const std::string file(Online::SWARM_MESHES[i].file);
        if (!load(swarms[i], root + "/" + file, error))
            std::cerr << "Colony skin preview " << file << ": " << error << '\n';
    }
    return true;
}
bool ColonySkinPreview::loadInstalledMeshes()
{
    if (const char *root=std::getenv("GLOB2_SKIN_PREVIEW_DIR"))
        if (*root) return loadMeshes(root);
    return loadMeshes("data/skins/colony-v1", true);
}
void ColonySkinPreview::setDownloads(std::unique_ptr<Online::SkinDownloads> value)
{
    downloads=std::move(value);
    for(int team=0;team<32;++team)uninstall(team);
}
void ColonySkinPreview::poll()
{
    if (downloads)
    {
        downloads->poll(static_cast<std::int64_t>(std::time(nullptr)));
        for (int team : downloads->takeRemoved()) uninstall(team);
        for (auto &entry : downloads->takeReady())
            if (install(entry.skin.team, std::make_unique<GAGCore::DrawableSurface>(entry.path),
                        GAGCore::loadSkinMaterialMap(entry.materialPath)))
            {
                colors[entry.skin.team]=entry.skin.buildingColor;
                swarmChoice[entry.skin.team]=entry.skin.swarmMesh;
            }
    }
    if (visible && !ready && !attemptedMeshes && globalContainer && globalContainer->gfx &&
        (globalContainer->gfx->getOptionFlags() & GAGCore::GraphicContext::USEGPU) &&
        std::any_of(textures.begin(), textures.end(), [](const auto &texture) { return bool(texture); }))
    {
        GAGCore::ApplicationHost::requestAssetPackage("skins");
        if (GAGCore::ApplicationHost::assetPackageReady("skins"))
        {
            attemptedMeshes=true;
            ready=loadInstalledMeshes();
        }
    }
}
std::optional<std::uint32_t> ColonySkinPreview::buildingColor(int team) const
{
    return visible&&team>=0&&team<32 ? colors[team] : std::nullopt;
}
ColonySkinPreview::~ColonySkinPreview() = default;

bool ColonySkinPreview::draw(GAGCore::GraphicContext &gfx, int type, int team,
                            int action, int direction, int delta, float x, float y, GAGCore::DrawableSurface *shadow, std::uint8_t alpha)
{
    if (!visible || !ready || team < 0 || team >= 32 || !textures[team] || direction < 0 || direction > 8 || delta < 0 || delta > 255)
        return false;
    const auto *mesh = unitMesh(type, action);
    if (!mesh) return false;
    const float size = mesh->logicalSize;
    const float offset = (size - 32) / 2;
    return gfx.drawSkinMesh(*mesh, unitAnimationFrame(0, direction, delta),
                            *textures[team], *materials[team], unitRegion(type),
                            x-offset, y-offset, size, size, shadow, alpha);
}

bool ColonySkinPreview::drawSwarm(GAGCore::GraphicContext &gfx, int team,
                                 float x, float y, float width, float height)
{
    if (!visible || !ready || team < 0 || team >= 32 || !textures[team]) return false;
    const auto *mesh = swarmMesh(team);
    // Whichever mesh is chosen, the swarm is painted from the swarm quadrant.
    return mesh && gfx.drawSkinMesh(*mesh, 0, *textures[team], *materials[team], GAGCore::SkinRegionSwarm,
                                    x, y, width, height);
}

const GAGCore::SkinMesh *ColonySkinPreview::swarmMesh(int team) const
{
    const auto &mesh = swarms[swarmChoice[team]];
    return mesh.identity ? &mesh : nullptr;
}

std::uint8_t ColonySkinPreview::unitRegion(int type)
{
    return type == WARRIOR ? GAGCore::SkinRegionWarrior :
           type == EXPLORER ? GAGCore::SkinRegionExplorer : GAGCore::SkinRegionWorker;
}

const GAGCore::SkinMesh *ColonySkinPreview::unitMesh(int type, int action) const
{
    unsigned clip;
    if (type == EXPLORER && (action == STOP_FLY || action == FLY)) clip = 6;
    else if (type == WORKER || type == WARRIOR)
    {
        switch (action)
        {
            case STOP_WALK: case WALK: clip = 0; break;
            case STOP_SWIM: case SWIM: clip = 1; break;
            case BUILD: case HARVEST:
                if (type != WORKER) return nullptr;
                clip = 2; break;
            case ATTACK_SPEED:
                if (type != WARRIOR) return nullptr;
                clip = 2; break;
            default: return nullptr;
        }
        if (type == WARRIOR) clip += 3;
    }
    else return nullptr;
    return &clips[clip];
}
void ColonySkinPreview::prepare(GAGCore::GraphicContext &gfx, const Scene &scene,
    int left, int top, int right, int bottom, int viewportX, int viewportY,
    int localTeam, std::uint32_t visibleTeams, bool wholeMap, float unitMotion, bool drawUnits, bool drawBuildings, const FogFade *fogFade)
{
    std::vector<GAGCore::SkinMeshRequest> requests;
    if (visible && ready && (drawUnits || drawBuildings))
    {
        const auto &map = scene.map;
        const auto &entities = scene.entities;
        for (int y=top-1; y<=bottom; ++y)
            for (int x=left-1; x<=right; ++x)
            {
                const int mx=x+viewportX, my=y+viewportY;
                if (drawUnits) for (auto gid : {map.getGroundUnit(mx,my), map.getAirUnit(mx,my)})
                {
                    const auto *unit = entities.unit(gid);
                    if (!unit || unit->team<0 || unit->team>=32 || !textures[unit->team]) continue;
                    if (!wholeMap)
                    {
                        if (fogFade && fogFade->active())
                        {
                            if (std::min(fogFade->level(mx,my), fogFade->level(mx-unit->dx,my-unit->dy)) == FogFade::FOGGED) continue;
                        }
                        else if (!map.isFOWDiscovered(mx,my,visibleTeams) &&
                                 !map.isFOWDiscovered(mx-unit->dx,my-unit->dy,visibleTeams)) continue;
                    }
                    const auto *mesh = unitMesh(unit->typeNum,unit->action);
                    if (mesh && unit->direction>=0 && unit->direction<=8 && unit->delta>=0 && unit->delta<=255)
                        requests.push_back({mesh, static_cast<unsigned>(unitAnimationFrame(0,unit->direction,drawnUnitDelta(*unit,unitMotion))), textures[unit->team].get(), materials[unit->team].get(), unitRegion(unit->typeNum)});
                }
                if (!drawBuildings) continue;
                const auto *building = entities.building(map.getBuilding(mx,my));
                if (!building || building->team<0 || building->team>=32 || !textures[building->team] ||
                    building->type->isBuildingSite ||
                    building->type->shortTypeNum != IntBuildingType::SWARM_BUILDING) continue;
                const auto *swarm = swarmMesh(building->team);
                if (swarm && (wholeMap || building->team==localTeam || (building->seenByMask&visibleTeams) ||
                    map.isFOWDiscovered(mx,my,visibleTeams)))
                    requests.push_back({swarm,0,textures[building->team].get(),materials[building->team].get(),GAGCore::SkinRegionSwarm});
            }
    }
    gfx.prepareSkinMeshes(requests);
}
