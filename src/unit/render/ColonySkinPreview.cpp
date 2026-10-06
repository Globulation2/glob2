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
#include "online/SkinSprites.h"
#include <filesystem>
#include "online/SkinViewTransforms.h"
#include <SDL3/SDL.h>
#include <ctime>
#include <stdexcept>

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

struct ColonySkinPreview::PreparedSkin {
    std::shared_ptr<const GAGCore::AssetImage> paint, material;
    std::array<bool, 4> hairy{};
};

ColonySkinPreview::ColonySkinPreview()
{
    const char *directory = std::getenv("GLOB2_SKIN_PREVIEW_DIR");
    if (!directory || !*directory) return;
    const std::string root(directory);
    if (globalContainer && globalContainer->gfx && (globalContainer->gfx->getOptionFlags() & GAGCore::GraphicContext::USEGPU)
        && GAGCore::ApplicationHost::assetPackageReady("skins")) ready = loadMeshes(root);
    // The material map is optional here: a missing one previews as all glossy
    // (a zero-filled surface); a present one must still be 512x512.
    auto material = GAGCore::loadSkinMaterialMap(root + "/material.webp");
    if (!material) material = std::make_unique<GAGCore::DrawableSurface>(AtlasSize, AtlasSize);
    if (!install(0, std::make_unique<GAGCore::DrawableSurface>(root + "/paint.webp"), std::move(material)))
        std::cerr << "Colony skin preview: paint.webp and material.webp must be " << AtlasSize << "x" << AtlasSize << '\n';
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
    if(sprites)sprites->remove(team);
    preparingSkins[team].cancel();
    preparingSkins[team] = {};
    textures[team].reset();
    materials[team].reset();
    hairy[team] = {};
    colors[team].reset();
    swarmChoice[team] = 0;
    swarmAngles[team] = 0;
    orientedSwarms[team] = {};
}
bool ColonySkinPreview::loadMeshes(const std::string &root, bool installed)
{
    const char *names[] = {"worker-walk", "worker-swim", "worker-harvest",
                           "warrior-walk", "warrior-swim", "warrior-fight", "explorer-fly"};
    auto &loader = GAGCore::Toolkit::assets();
    std::vector<GAGCore::AssetLoader::Handle<GAGCore::SkinMesh>> requests;
    auto path = [&](std::string file) { return installed ? file : std::filesystem::absolute(file).string(); };
    for (const auto *name : names) requests.push_back(GAGCore::requestSkinMesh(loader, path(root + "/" + name + ".gsk")));
    for (const auto &swarm : Online::SWARM_MESHES) requests.push_back(GAGCore::requestSkinMesh(loader, path(root + "/" + std::string(swarm.file))));
    std::array<GAGCore::SkinMesh, 7> replacement;
    for (unsigned i = 0; i < replacement.size(); ++i) {
        auto mesh = loader.wait(requests[i]);
        if (!mesh) { std::cerr << "Colony skin preview: " << names[i] << ": " << requests[i].error() << '\n'; return false; }
        replacement[i] = *mesh;
    }
    clips = std::move(replacement);
    for (unsigned i = 0; i < swarms.size(); ++i)
        if (auto mesh = loader.wait(requests[clips.size() + i])) swarms[i] = *mesh;
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
    sprites.reset();
    downloads=std::move(value);
    if(downloads && globalContainer && globalContainer->gfx && !(globalContainer->gfx->getOptionFlags() & GAGCore::GraphicContext::USEGPU)) {
        downloads->setSoftware(true);
        sprites=std::make_unique<Online::SkinSprites>(downloads->storage(),downloads->origin(),downloads->fetchStarter());
    }
    for(int team=0;team<32;++team)uninstall(team);
}
void ColonySkinPreview::prepareSkin(Online::AuthorizedSkin appearance,
                                    const std::string &paintPath, const std::string &materialPath)
{
    const int team = appearance.team;
    if (team < 0 || team >= 32) return;
    preparingSkins[team].cancel();
    auto &loader = GAGCore::Toolkit::assets();
    auto paint = loader.requestImage(paintPath, GAGCore::AssetLoader::Priority::Interactive);
    auto material = loader.requestImage(materialPath, GAGCore::AssetLoader::Priority::Interactive);
    // Only immutable image preparation is shared. Authorization and chosen
    // appearance belong to each subscriber, including refreshed tickets.
    const auto key = "colony-skin:" + std::to_string(paintPath.size()) + ':' + paintPath + materialPath;
    auto prepared = loader.request<PreparedSkin>(key, {paint.dependency(), material.dependency()},
        [paint, material] {
            auto result = std::make_shared<PreparedSkin>();
            result->paint = paint.get();
            result->material = material.get();
            const auto *p = result->paint->surface, *m = result->material->surface;
            if (p->w != AtlasSize || p->h != AtlasSize || m->w != AtlasSize || m->h != AtlasSize)
                throw std::runtime_error("Invalid colony skin dimensions");
            for (int y = 0; y < AtlasSize; ++y) {
                const auto *row = reinterpret_cast<const Uint32*>(static_cast<const Uint8*>(m->pixels) + y * m->pitch);
                for (int x = 0; x < AtlasSize; ++x) {
                    const auto id = row[x] & 255;
                    if (id > 3 || row[x] != (0xff000000u | id << 16 | id << 8 | id))
                        throw std::runtime_error("Invalid colony material id");
                    if (id == 3)
                        result->hairy[(y >= AtlasSize/2 ? 2 : 0) + (x >= AtlasSize/2 ? 1 : 0)] = true;
                }
            }
            return result;
        }, 0, GAGCore::AssetLoader::Priority::Interactive);
    preparingSkins[team] = prepared;
    loader.onReady<PreparedSkin>(prepared, assetLifetime,
        [this, team, appearance = std::move(appearance)](std::shared_ptr<const PreparedSkin> skin) {
            preparingSkins[team] = {};
            if (!skin || std::time(nullptr) >= appearance.expiresAt) return;
            auto prepare = [](const GAGCore::AssetImage& image) -> std::unique_ptr<GAGCore::DrawableSurface> {
                try {
                    auto surface = GAGCore::DrawableSurface::fromAssetImage(image);
                    surface->prepareTexture();
                    return surface;
                } catch (const std::exception& error) {
                    std::cerr << "Colony skin preview: " << error.what() << '\n';
                    return {};
                }
            };
            auto paint = prepare(*skin->paint), material = prepare(*skin->material);
            if (!paint || !material) return; // retain the last complete appearance
            textures[team] = std::move(paint);
            materials[team] = std::move(material);
            hairy[team] = skin->hairy;
            colors[team] = appearance.buildingColor;
            swarmChoice[team] = appearance.swarmMesh;
            swarmAngles[team] = appearance.swarmViewAngle;
            orientedSwarms[team] = {};
        });
}
void ColonySkinPreview::poll()
{
    if (downloads)
    {
        downloads->poll(static_cast<std::int64_t>(std::time(nullptr)));
        for (int team : downloads->takeRemoved()) uninstall(team);
        for (auto &entry : downloads->takeReady())
            if (sprites) {
                sprites->install(entry.skin);
                colors[entry.skin.team] = entry.skin.buildingColor;
                swarmChoice[entry.skin.team] = entry.skin.swarmMesh;
                swarmAngles[entry.skin.team] = entry.skin.swarmViewAngle;
                orientedSwarms[entry.skin.team] = {};
            } else {
                prepareSkin(std::move(entry.skin), entry.path, entry.materialPath);
            }
    }
    if(sprites && visible)sprites->poll();
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
ColonySkinPreview::~ColonySkinPreview() {
    assetLifetime.reset();
    for (auto &request : preparingSkins) request.cancel();
}

bool ColonySkinPreview::draw(GAGCore::GraphicContext &gfx, int type, int team,
                            int action, int direction, int delta, float x, float y, GAGCore::DrawableSurface *shadow, std::uint8_t alpha)
{
    if (!visible || (!sprites && (!ready || team<0 || team>=32 || !textures[team])) || direction < 0 || direction > 8 || delta < 0 || delta > 255)
        return false;
    if(sprites) {
        const unsigned clip=unitClip(type,action);
        if(clip>=8)return false;
        const float size=Online::SkinSpriteLogicalSizes[clip],offset=(size-32)/2;
        return sprites->draw(gfx,team,clip,unitAnimationFrame(0,direction,delta),x-offset,y-offset,size,size,shadow,alpha);
    }
    const auto *mesh = unitMesh(type, action);
    if (!mesh) return false;
    const float size = mesh->logicalSize;
    const float offset = (size - 32) / 2;
    return gfx.drawSkinMesh(*mesh, unitAnimationFrame(0, direction, delta),
                            *textures[team], *materials[team], unitRegion(type),
                            x-offset, y-offset, size, size, shadow, alpha);
}

bool ColonySkinPreview::drawSwarm(GAGCore::GraphicContext &gfx, int team,
                                 float x, float y, float width, float height, std::uint8_t alpha)
{
    if (!visible) return false;
    if(sprites)return sprites->draw(gfx,team,7,0,x,y,width,height,nullptr,alpha);
    if (!ready || team < 0 || team >= 32 || !textures[team]) return false;
    const auto *mesh = swarmMesh(team);
    // Whichever mesh is chosen, the swarm is painted from the swarm quadrant.
    return mesh && gfx.drawSkinMesh(*mesh, 0, *textures[team], *materials[team], GAGCore::SkinRegionSwarm,
                                    x, y, width, height, nullptr, alpha);
}

const GAGCore::SkinMesh *ColonySkinPreview::swarmMesh(int team) const
{
    const auto &mesh = swarms[swarmChoice[team]];
    if (!mesh.identity) return nullptr;
    if (!swarmAngles[team]) return &mesh;
    auto &oriented = orientedSwarms[team];
    if (!oriented.identity) {
        const auto &view = Online::SkinViews[swarmChoice[team]];
        oriented = mesh.rotatedView(swarmAngles[team], view.inverse, view.projection, view.normals);
    }
    return oriented.identity ? &oriented : nullptr;
}

std::uint8_t ColonySkinPreview::unitRegion(int type)
{
    return type == WARRIOR ? GAGCore::SkinRegionWarrior :
           type == EXPLORER ? GAGCore::SkinRegionExplorer : GAGCore::SkinRegionWorker;
}

unsigned ColonySkinPreview::unitClip(int type, int action)
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
                if (type != WORKER) return 8;
                clip = 2; break;
            case ATTACK_SPEED:
                if (type != WARRIOR) return 8;
                clip = 2; break;
            default: return 8;
        }
        if (type == WARRIOR) clip += 3;
    }
    else return 8;
    return clip;
}
const GAGCore::SkinMesh *ColonySkinPreview::unitMesh(int type,int action) const
{
    const auto clip=unitClip(type,action);
    return clip<clips.size()?&clips[clip]:nullptr;
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
                    building->type->presentation.skinSlot != "swarm") continue;
                const auto *swarm = swarmMesh(building->team);
                if (swarm && (wholeMap || building->team==localTeam || (building->seenByMask&visibleTeams) ||
                    map.isFOWDiscovered(mx,my,visibleTeams)))
                    requests.push_back({swarm,0,textures[building->team].get(),materials[building->team].get(),GAGCore::SkinRegionSwarm});
            }
    }
    gfx.prepareSkinMeshes(requests);
}
