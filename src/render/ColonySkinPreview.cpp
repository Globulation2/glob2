// SPDX-License-Identifier: GPL-3.0-or-later
#include "ColonySkinPreview.h"
#include "UnitConsts.h"
#include "UnitAnimation.h"
#include "UnitMotion.h"
#include "scene/Scene.h"
#include "BuildingType.h"
#include "IntBuildingType.h"
#include <GraphicContext.h>
#include <cstdlib>
#include <iostream>
#include <Toolkit.h>
#include <FileManager.h>
#include <StreamBackend.h>
#include "online/SkinDownloads.h"
#include <ctime>

ColonySkinPreview::ColonySkinPreview()
{
    const char *directory = std::getenv("GLOB2_SKIN_PREVIEW_DIR");
    if (!directory || !*directory) return;
    const std::string root(directory);
    ready = loadMeshes(root);
    textures[0] = std::make_unique<GAGCore::DrawableSurface>(root + "/paint.png");
    if (textures[0]->getW()!=256 || textures[0]->getH()!=256) textures[0].reset();
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
    std::string swarmError;
    if (!load(swarm, root + "/swarm.gsk", swarmError))
        std::cerr << "Colony skin preview swarm: " << swarmError << '\n';
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
    for(auto &texture:textures)texture.reset();
    for(auto &color:colors)color.reset();
}
void ColonySkinPreview::poll()
{
    if(!downloads)return;
    downloads->poll(static_cast<std::int64_t>(std::time(nullptr)));
    for(int team:downloads->takeRemoved()) { textures[team].reset(); colors[team].reset(); }
    for(auto &entry:downloads->takeReady())
    {
        if(!ready&&!attemptedMeshes){attemptedMeshes=true;ready=loadInstalledMeshes();}
        auto texture=std::make_unique<GAGCore::DrawableSurface>(entry.path);
        if(texture->getW()!=256||texture->getH()!=256)continue;
        textures[entry.skin.team]=std::move(texture);
        colors[entry.skin.team]=entry.skin.buildingColor;
    }
}
std::optional<std::uint32_t> ColonySkinPreview::buildingColor(int team) const
{
    return visible&&team>=0&&team<32 ? colors[team] : std::nullopt;
}
ColonySkinPreview::~ColonySkinPreview() = default;

bool ColonySkinPreview::draw(GAGCore::GraphicContext &gfx, int type, int team,
                            int action, int direction, int delta, float x, float y, GAGCore::DrawableSurface *shadow)
{
    if (!visible || !ready || team < 0 || team >= 32 || !textures[team] || direction < 0 || direction > 8 || delta < 0 || delta > 255)
        return false;
    const auto *mesh = unitMesh(type, action);
    if (!mesh) return false;
    const float size = mesh->logicalSize;
    const float offset = (size - 32) / 2;
    return gfx.drawSkinMesh(*mesh, unitAnimationFrame(0, direction, delta),
                            *textures[team], x-offset, y-offset, size, size, shadow);
}

bool ColonySkinPreview::drawSwarm(GAGCore::GraphicContext &gfx, int team,
                                 float x, float y, float width, float height)
{
    return visible && ready && team >= 0 && team < 32 && textures[team] && swarm.identity &&
        gfx.drawSkinMesh(swarm, 0, *textures[team], x, y, width, height);
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
    int localTeam, std::uint32_t visibleTeams, bool wholeMap, float unitMotion)
{
    std::vector<GAGCore::SkinMeshRequest> requests;
    if (visible && ready)
    {
        const auto &map = scene.map;
        const auto &entities = scene.entities;
        for (int y=top-1; y<=bottom; ++y)
            for (int x=left-1; x<=right; ++x)
            {
                const int mx=x+viewportX, my=y+viewportY;
                for (auto gid : {map.getGroundUnit(mx,my), map.getAirUnit(mx,my)})
                {
                    const auto *unit = entities.unit(gid);
                    if (!unit || unit->team<0 || unit->team>=32 || !textures[unit->team]) continue;
                    if (!wholeMap && !map.isFOWDiscovered(mx,my,visibleTeams) &&
                        !map.isFOWDiscovered(mx-unit->dx,my-unit->dy,visibleTeams)) continue;
                    const auto *mesh = unitMesh(unit->typeNum,unit->action);
                    if (mesh && unit->direction>=0 && unit->direction<=8 && unit->delta>=0 && unit->delta<=255)
                        requests.push_back({mesh, static_cast<unsigned>(unitAnimationFrame(0,unit->direction,drawnUnitDelta(*unit,unitMotion))), textures[unit->team].get()});
                }
                const auto *building = entities.building(map.getBuilding(mx,my));
                if (!building || building->team<0 || building->team>=32 || !textures[building->team] ||
                    !swarm.identity || building->type->isBuildingSite ||
                    building->type->shortTypeNum != IntBuildingType::SWARM_BUILDING) continue;
                if (wholeMap || building->team==localTeam || (building->seenByMask&visibleTeams) ||
                    map.isFOWDiscovered(mx,my,visibleTeams))
                    requests.push_back({&swarm,0,textures[building->team].get()});
            }
    }
    gfx.prepareSkinMeshes(requests);
}
