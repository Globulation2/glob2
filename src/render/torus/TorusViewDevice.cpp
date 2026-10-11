// SPDX-License-Identifier: GPL-3.0-or-later
#include "TorusView.h"
#include "TorusGeometry.h"
#include "render/SoftwareTerrainCache.h"
#include "Game.h"
#include "GameGUI.h"
#include "GlobalContainer.h"
#include <GraphicContext.h>
#include <RenderDevice.h>
#include <RenderStateScope.h>
#include <PerformanceTelemetry.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>

using namespace GAGCore;
struct TorusDeviceState
{
    RenderResources *resources = nullptr;
    unsigned ownerGeneration = 0;
    std::uint64_t registryIdentity=0;
    std::uint64_t revision = 0;
    std::vector<TextureHandle> tileHandles;
    std::vector<TextureSnapshot> tileTargets;
    TextureHandle cloud;
    BufferHandle ground, cloudMesh;
    GeometrySnapshot groundGeometry, cloudGeometry;
    // Backing targets are hidden while a bounded capture cycle is assembled.
    // The device retains the completed main image during these presentation turns.
    std::shared_ptr<const PresentationFrame> captureFrame;
    Game::ViewState captureView;
    BuildingGuiStateMap captureBuildings;
    std::size_t captureTile = 0;
    int captureX = 0, captureY = 0;
    float captureZoom = 1;
    int captureTeam = 0;
    unsigned captureOptions = 0;
    bool capturePaused = false;
    bool advancePreviews = true;
};
namespace
{
class CaptureSkinScope
{
    MapRenderState &capture;
    MapRenderState *source;
public:
    CaptureSkinScope(MapRenderState &state, MapRenderState *sourceRender)
        : capture(state), source(sourceRender)
    {
        if (source) capture.swapSkinPreview(*source);
    }
    ~CaptureSkinScope() { if (source) capture.swapSkinPreview(*source); }
};
class SourceMapPhaseScope
{
    MapRenderState &render;
    unsigned options;
    bool paused;
    int initialAnimation;
    int exceptions;
public:
    SourceMapPhaseScope(MapRenderState &state,unsigned drawOptions,bool animationsPaused)
        : render(state),options(drawOptions),paused(animationsPaused),
          initialAnimation(state.animationTime),exceptions(std::uncaught_exceptions())
    { if(!paused)++render.animationTime; }
    ~SourceMapPhaseScope()
    {
        // The caller draws the ordinary map after a fatal torus exception.
        // Leave that fallback to advance the source phases once for this turn.
        if(std::uncaught_exceptions()>exceptions)render.animationTime=initialAnimation;
        else Game::finishMapCapture(render,options,paused);
    }
};
constexpr int cloudGridLimit = 128;
constexpr float pi = 3.14159265358979323846f;
float clamp(float x, float a, float b) { return std::max(a, std::min(b, x)); }
float smooth(float x) { x = clamp(x, 0, 1); return x * x * (3 - 2 * x); }
float mix(float a, float b, float t) { return a + (b - a) * t; }
struct SkyPoint { float x, y, z, brightness; int size; };
const std::vector<SkyPoint> &skyPoints(bool haze)
{
    static std::vector<SkyPoint> stars, clouds;
    auto &points = haze ? clouds : stars;
    if (!points.empty())
        return points;
    unsigned seed = haze ? 81991u : 1729u;
    auto random = [&]()
    {
        seed = seed * 1664525u + 1013904223u;
        return (seed >> 8) / 16777216.0f;
    };
    for (int i = 0; i < (haze ? 1700 : 5200); ++i)
    {
        float longitude = random() * 2 * pi;
        float latitude = haze ? (random() + random() + random() - 1.5f) * 0.10f : random() * 2 - 1;
        float radius = std::sqrt(std::max(0.0f, 1 - latitude * latitude));
        float x = radius * std::cos(longitude), y = latitude, z = radius * std::sin(longitude);
        points.push_back({x * 0.82f - y * 0.572f, x * 0.572f + y * 0.82f, z, random(),
                          i % 29 == 0 ? 2 : (i % 5 == 0 ? 1 : 0)});
    }
    return points;
}
}
void TorusView::releaseDeviceMaterials()
{
    if (!deviceState) return;
    auto gfx = globalContainer ? globalContainer->gfx : nullptr;
    if (gfx && gfx->renderResources() == deviceState->resources && gfx->renderResources()->instanceIdentity()==deviceState->registryIdentity) {
        auto &resources = *deviceState->resources;
        for (auto handle : deviceState->tileHandles) resources.erase(handle);
        resources.erase(deviceState->cloud); resources.erase(deviceState->ground); resources.erase(deviceState->cloudMesh);
    }
    deviceState.reset();
}
bool TorusView::draw(const PresentationFrame &frame, GameGUI *gui, int team, unsigned options, int &vx, int &vy,
                     int width, int height, float flatZoom, float fractionX, float fractionY)
{
    auto gfx = globalContainer->gfx;
    if (gfx && gfx->renderDevice()) {
        if (!gfx->renderDevice()->capabilities().torus) { reset(); return false; }
        try { return drawDevice(frame, gui, team, options, vx, vy, width, height, flatZoom, fractionX, fractionY); }
        catch (const std::exception &error) {
            // Allocation or restoration failure falls back to the ordinary map
            // in this frame; retry is allowed after a device-generation change.
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Torus rendering failed: %s", error.what());
            releaseDeviceMaterials(); failed = true; graphicsGeneration = gfx->getGLContextGeneration();
            target = moving = false; amount = 0; return false;
        }
    }
    return drawLegacy(frame, gui, team, options, vx, vy, width, height, flatZoom, fractionX, fractionY);
}
bool TorusView::drawDevice(const PresentationFrame &frame, GameGUI *gui, int team, unsigned options, int &vx, int &vy,
                           int width, int height, float flatZoom, float fractionX, float fractionY)
{
    PERF_SCOPE_TIME(Torus);
    if (!active() || !available())
    {
        reset();
        return false;
    }
    Uint32 now = SDL_GetTicks();
    float dt = std::min(0.1f, float(now - lastFrame) / 1000.0f);
    lastFrame = now;
    const bool automatic = globalContainer->settings.automaticTorus;
    // Movement only reports tile changes, so a held pan that pauses looks
    // identical to a finished one. Hold the overview out until the button goes up.
    if (!automatic || (!panHeld && now - lastMove > 250))
        moving = false;
    const bool pullBack = target || moving;
    if (!pullBack && amount == 0)
    {
        reset();
        return false;
    }
    if (!amount && pullBack)
    {
        // Put the current viewport center on the front of the torus. Preserve
        // its sub-tile offset, so even the first/last frame matches normal 2D.
        TorusGeometry::MapFocus focus =
            TorusGeometry::mapFocus(frame.map.getW(), frame.map.getH(), vx, vy, width, height);
        if (!worldW || !worldH)
        {
            originX = focus.originX;
            originY = focus.originY;
        }
        focusU = (((vx - originX) & frame.map.getMaskW()) + fractionX / 32.0f + width / (64.0f * flatZoom)) / frame.map.getW();
        focusV = (((vy - originY) & frame.map.getMaskH()) + fractionY / 32.0f + (height + 16) / (64.0f * flatZoom)) / frame.map.getH();
        baseViewportX = vx;
        baseViewportY = vy;
        worldW = frame.map.getW();
        worldH = frame.map.getH();
        travelU = travelV = cameraU = cameraV = 0;
    }
    // Manual switching is symmetric. Automatic movement unfolds gradually,
    // then returns quickly; pointer gestures keep their current projection.
    const float pace = automatic && !target ? (moving ? 4.0f : 0.45f) : 1.8f;
    if (!pointerHeld)
        amount = clamp(amount + (pullBack ? dt : -dt) / pace, 0, 1);
    // The ordinary map and minimap track the same destination as the torus.
    // Ease the sub-tile remainder away while returning to the tile-based 2D camera.
    if (!pullBack && !pointerHeld)
    {
        float settle = amount == 0 ? 1 : 1 - std::exp(-16 * dt);
        travelU = mix(travelU, std::round(travelU * worldW) / worldW, settle);
        travelV = mix(travelV, std::round(travelV * worldH) / worldH, settle);
    }
    // Input remains the logical map destination. Smooth the rendered focus for
    // the surface and picking; the sky shares only its horizontal movement.
    cameraU = TorusGeometry::follow(cameraU, travelU, dt, true);
    cameraV = TorusGeometry::follow(cameraV, travelV, dt, true);
    if (amount == 0)
    {
        cameraU = travelU;
        cameraV = travelV;
    }
    auto destination =
        TorusGeometry::destination(baseViewportX, baseViewportY, travelU, travelV, worldW, worldH);
    vx = destination.x;
    vy = destination.y;
    float pull = smooth(amount);
    float roll = smooth(amount);
    float aspect = float(frame.map.getW()) / frame.map.getH();
    const TorusGeometry::Shape shape(aspect);
    float anchorU = focusU + cameraU, anchorV = focusV + cameraV;
    // The ring keeps one attitude on screen: the surface slides around the
    // tube as the view pans, and turns about the axis as it scrolls.
    viewAspect = float(width) / std::max(1, height - 16);
    const float ringV = TorusGeometry::overviewLatitude(viewAspect, shape);
    float cameraDistance = TorusGeometry::hoverDistance(ringV);
    // Keep the focused landscape at screen center throughout the transition.
    // The ring's silhouette is measured once per view shape in camera units,
    // with how far the folded surface spreads the map at its focus.
    if (ringAspect != viewAspect || ringMapAspect != aspect)
    {
        float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
        const auto project = [&](float du, float dv)
        {
            auto p = TorusGeometry::overviewPoint(du, dv, 1, ringV, viewAspect, shape);
            float w = 1 - p.z / cameraDistance;
            return TorusGeometry::Point{p.x / w, p.y / w, 0};
        };
        for (int j = 0; j <= 40; ++j)
            for (int i = 0; i <= 40; ++i)
            {
                auto p = project(i / 40.0f - 0.5f, TorusGeometry::meshOffset(j / 40.0f, ringV, shape));
                minX = std::min(minX, p.x);
                maxX = std::max(maxX, p.x);
                minY = std::min(minY, p.y);
                maxY = std::max(maxY, p.y);
            }
        ringWidth = maxX - minX;
        ringHeight = maxY - minY;
        focusGain = (project(0.001f, 0).x - project(-0.001f, 0).x) / 0.002f;
        ringAspect = viewAspect;
        ringMapAspect = aspect;
    }
    // The ring shows its focus at the 2D camera's zoom: folding the map
    // neither zooms in nor out, and the wheel means the same in both views.
    // Only an automatic reveal pulls back, to the whole ring.
    const float mapPixels = frame.map.getW() * 32.0f;
    const float fit = 0.9f * std::min(width / ringWidth, (height - 16) / ringHeight);
    float scale = mapPixels * flatZoom / focusGain;
    // Fully zoomed out the whole ring is in view. The flat view of a very
    // wide map stops zooming out before its ring would fit, so that ring is
    // drawn smaller throughout by what it lacks there.
    const float minimumZoom = std::min(1.0f, std::max(width / mapPixels, height / (frame.map.getH() * 32.0f)));
    scale *= std::min(1.0f, fit * focusGain / (mapPixels * minimumZoom));
    if (wholeRing)
        scale = std::min(scale, fit);
    float sx = std::exp(mix(std::log(mapPixels * flatZoom / (8 * pi)), std::log(scale), pull));
    float sy = sx * TorusGeometry::verticalScale(focusU, focusV, roll, aspect);
    // The zoom the focus is seen at this frame decides how the map is drawn:
    // zoomed out, the ring carries the same strategic overview as the 2D view.
    const float shownZoom = std::exp(mix(std::log(flatZoom), std::log(scale * focusGain / mapPixels), pull));

    auto gfx = globalContainer->gfx;
    auto &resources = *gfx->renderResources();
    auto &device = *gfx->renderDevice();
    if (!deviceState || deviceState->resources != &resources || deviceState->ownerGeneration != gfx->getGLContextGeneration()) {
        releaseDeviceMaterials();
        deviceState = std::make_shared<TorusDeviceState>(); deviceState->resources = &resources;
        deviceState->ownerGeneration = gfx->getGLContextGeneration(); deviceState->registryIdentity=resources.instanceIdentity(); tiles.clear(); tileMeshDirty = true; failed = false;
    }
    auto &state = *deviceState;
    if (state.tileTargets.empty()) {
        device.trimRetiredResources();
        const int limit = textureLimit > 0 ? std::min(textureLimit, device.capabilities().maximumTextureSize) : device.capabilities().maximumTextureSize;
        const int captureDensity = SoftwareTerrainCache::capturePixelsPerCell(worldW, worldH);
        for (pixelsPerCell = captureDensity; pixelsPerCell >= 1; pixelsPerCell /= 2) {
            auto candidate = TorusTextureTiles::layout(worldW * 32, worldH * 32, limit, pixelsPerCell);
            // Preserve requested density whenever the actual class and aggregate
            // budgets allow it. Count the complete layout before allocating any
            // tile. Map capture is 2D: only RGBA color is allocated, with no
            // depth or second atlas. The completed main image covers progress.
            // Reserve bounded authored source chains and upload staging separately.
            constexpr std::size_t targetBudget = 128u * 1024 * 1024;
            constexpr std::size_t deviceBudget = 256u * 1024 * 1024;
            constexpr std::size_t uploadReserve = 8u * 1024 * 1024;
            // The largest HD authored terrain block is ~22.4 MiB including
            // mip levels. Keep additional glyph/sprite admission headroom.
            constexpr std::size_t sourceReserve = 32u * 1024 * 1024;
            std::size_t count = 0;
            for (const auto &tile : candidate) count += std::size_t(tile.textureW) * tile.textureH;
            const auto resident = std::min(deviceBudget, device.telemetry().resourceBytes);
            const auto deviceHeadroom = deviceBudget - resident;
            const auto logicalHeadroom = resources.byteLimit() - std::min(resources.byteLimit(), resources.retainedBytes());
            bool okay = !candidate.empty() && count <= targetBudget / 4 && count <= logicalHeadroom / 4 &&
                deviceHeadroom >= uploadReserve + sourceReserve &&
                count <= (deviceHeadroom - uploadReserve - sourceReserve) / 4 &&
                (!allocationPixelLimit || count <= std::size_t(allocationPixelLimit));
            if (!okay) continue;
            for (const auto &tile : candidate) {
                auto surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>(SDL_CreateSurface(tile.textureW, tile.textureH, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
                if (!surface) { okay = false; break; }
                SDL_FillSurfaceRect(surface.get(), nullptr, SDL_MapSurfaceRGBA(surface.get(), 0, 0, 0, 255));
                TextureDescriptor descriptor; descriptor.budgetClass=TextureDescriptor::BudgetClass::TerrainPage; descriptor.alpha=TextureDescriptor::Alpha::Opaque;
                auto handle = resources.create(surface.get(), ++state.revision, descriptor);
                if (!handle) { okay = false; break; }
                state.tileHandles.push_back(handle); state.tileTargets.push_back(resources.snapshot(handle));
            }
            if (okay) { tiles = std::move(candidate); break; }
            for (auto handle : state.tileHandles) resources.erase(handle);
            state.tileHandles.clear(); state.tileTargets.clear();
        }
        if (state.tileTargets.empty()) { failed = true; graphicsGeneration = gfx->getGLContextGeneration(); target = moving = false; amount = 0; return false; }
    }
    auto &sourceRender=gui ? gui->view.render : standaloneRender;
    const bool sourcePaused=gui && gui->gamePaused;
    SourceMapPhaseScope phase(sourceRender,options,sourcePaused);
    const unsigned captureOptions = options | Game::DRAW_NO_CLOUD_LAYER | Game::DRAW_TILED_CAPTURE;
    if (state.captureFrame && (state.captureFrame->map.identity() != frame.map.identity() ||
        state.captureTeam != team || state.captureOptions != captureOptions)) {
        state.captureFrame.reset();
        state.captureView.scene = nullptr;
    }
    // A single full-density tile can contain more than General's 64 MiB of
    // source mip chains. Admit one bounded region per host turn instead of
    // replaying the same too-large prefix after every budget deferral.
    if (!state.captureFrame) {
        state.captureFrame = std::make_shared<PresentationFrame>(frame);
        state.captureZoom = shownZoom;
        state.captureTeam = team;
        state.captureOptions = captureOptions;
        state.capturePaused = gui && gui->gamePaused;
        state.captureTile = 0; state.captureX = state.captureY = 0;
        state.advancePreviews = true;
        auto &captureRender = state.captureView.render;
        // prepareSceneMapFrame advances the frozen capture once. The source
        // phase already advanced for this host turn and keeps advancing on holds.
        captureRender.animationTime = sourceRender.animationTime-(sourcePaused?0:1);
        captureRender.areaAnimationTick = sourceRender.areaAnimationTick;
        captureRender.unitMotion = sourceRender.unitMotion;
        captureRender.minimumZoom = sourceRender.minimumZoom;
        captureRender.zonesEmphasised = sourceRender.zonesEmphasised;
        captureRender.fogFade = sourceRender.fogFade;
        if (gui) {
            state.captureBuildings = gui->buildingGuiState;
        }
        state.captureView.scene = state.captureFrame.get();
        CaptureSkinScope skin(captureRender, gui ? &sourceRender : nullptr);
        Game::prepareSceneMapFrame(*state.captureFrame, state.captureTeam, state.captureView,
                                  state.captureOptions, state.capturePaused);
    }
    const auto &captureFrame = *state.captureFrame;
    const auto &tile = tiles[state.captureTile];
    const int captureW = tile.w + 64, captureH = tile.h + 64;
    // Bound each region to 1024 target pixels per axis. The whole-capture
    // sampling plan selects the same source density in every region; lower
    // density therefore admits larger logical regions without more texture bytes.
    const int regionPoints = pixelsPerCell == 32 ? 512 : 1024;
    const int regionPixels = regionPoints * 32 / pixelsPerCell;
    constexpr int cullHalo = 64;
    const SDL_Rect region{state.captureX, state.captureY,
        std::min(regionPixels, captureW-state.captureX), std::min(regionPixels, captureH-state.captureY)};
    const SDL_Rect culling{std::max(0,region.x-cullHalo), std::max(0,region.y-cullHalo),
        std::min(captureW,region.x+region.w+cullHalo)-std::max(0,region.x-cullHalo),
        std::min(captureH,region.y+region.h+cullHalo)-std::max(0,region.y-cullHalo)};
    gfx->setClipRect();
    if (!gfx->beginDeviceTarget(state.tileTargets[state.captureTile], captureW*state.captureZoom, captureH*state.captureZoom))
        return true; // Admission deferred: retain the same target and region cursor.
    try {
        CaptureSkinScope skin(state.captureView.render, gui ? &sourceRender : nullptr);
        const SDL_Rect pixels{region.x*pixelsPerCell/32,region.y*pixelsPerCell/32,
            region.w*pixelsPerCell/32,region.h*pixelsPerCell/32};
        if (!gfx->setDeviceTargetScissor(&pixels)) throw std::runtime_error("Torus physical clipping unavailable");
        // Replaying an unfinished region starts from clear pixels, so a prior
        // submitted prefix cannot blend translucent sprites twice.
        DrawPacket clear; clear.kind=DrawPacket::Kind::Clear; clear.clearColor=true;
        clear.clearValue={0,0,0,1};gfx->submitDevicePacket(std::move(clear));
        gfx->setRenderTargetScale(float(pixelsPerCell)/(32*state.captureZoom));
        const SDL_Rect clip{0,0,int(std::ceil(captureW*state.captureZoom)),int(std::ceil(captureH*state.captureZoom))};
        MapTransformScope mapPass(*gfx,state.captureZoom,0,0,clip);
        const int tx=(originX+tile.x/32-1)&captureFrame.map.getMaskW();
        const int ty=(originY+tile.y/32-1)&captureFrame.map.getMaskH();
        Game::drawSceneMapRegion(captureFrame,culling.x,culling.y,culling.w,culling.h,0,0,tx,ty,
            state.captureTeam,state.captureView,state.captureOptions,nullptr,
            gui ? &state.captureBuildings : nullptr,state.capturePaused,cloudGridLimit,true,captureW,captureH);
    } catch (...) {
        gfx->setDeviceTargetScissor(nullptr);gfx->endDeviceTarget();gfx->setRenderTargetScale(0);throw;
    }
    gfx->setDeviceTargetScissor(nullptr);gfx->endDeviceTarget();gfx->setRenderTargetScale(0);
    if (!gfx->flushDeviceFrame()) return true;
    state.captureX += region.w;
    if (state.captureX >= captureW) {state.captureX=0;state.captureY+=region.h;}
    if (state.captureY >= captureH) {state.captureY=0;++state.captureTile;}
    if (state.captureTile < tiles.size()) {
        gfx->deferDeviceFrame();
        return true;
    }
    // UI overlays are captured together after terrain/entity regions, so a
    // moving pointer or changed tool cannot leave different overlays in tiles.
    if (gui) {
        const auto *previousScene=gui->view.scene;
        std::swap(gui->view.render,state.captureView.render);
        gui->view.scene=state.captureFrame.get();
        gui->toolManager.setDrawnScene(state.captureFrame.get());
        bool overlayComplete=true;
        try {
            for (std::size_t index=0;index<tiles.size();++index) {
                const auto &overlayTile=tiles[index];
                const int w=overlayTile.w+64,h=overlayTile.h+64;
                if(!gfx->beginDeviceTarget(state.tileTargets[index],w*state.captureZoom,h*state.captureZoom)) {
                    overlayComplete=false;
                    break;
                }
                try {
                    gfx->setRenderTargetScale(float(pixelsPerCell)/(32*state.captureZoom));
                    const SDL_Rect clip{0,0,int(std::ceil(w*state.captureZoom)),int(std::ceil(h*state.captureZoom))};
                    MapTransformScope mapPass(*gfx,state.captureZoom,0,0,clip);
                    gui->drawTorusOverlays(captureFrame,(originX+overlayTile.x/32-1)&captureFrame.map.getMaskW(),
                        (originY+overlayTile.y/32-1)&captureFrame.map.getMaskH(),w,h,state.advancePreviews);
                } catch(...) {gfx->endDeviceTarget();throw;}
                gfx->endDeviceTarget();state.advancePreviews=false;
            }
        } catch(...) {
            gui->view.scene=previousScene;std::swap(gui->view.render,state.captureView.render);
            gui->toolManager.setDrawnScene(previousScene);gfx->setRenderTargetScale(0);throw;
        }
        gui->view.scene=previousScene;std::swap(gui->view.render,state.captureView.render);
        gui->toolManager.setDrawnScene(previousScene);
        if(!overlayComplete) {
            state.captureTile=0;state.captureX=state.captureY=0;
            gfx->setRenderTargetScale(0);
            return true;
        }
    }
    if (!gfx->flushDeviceFrame()) {
        // An overlay prefix may already have blended into the capture. Rebuild
        // the same immutable frame from clean regions instead of painting it twice.
        state.captureTile=0;state.captureX=state.captureY=0;
        return true;
    }
    Game::finishMapCapture(state.captureView,state.captureOptions,state.capturePaused);
    state.captureFrame.reset();state.captureView.scene=nullptr;
    gfx->setRenderTargetScale(0);gfx->setClipRect();
    const bool drawClouds = globalContainer->settings.clouds;
    if (drawClouds) {
        int gridW, gridH;
        clouds.computeWorld(worldW, worldH, gui ? gui->mapAnimationTime() : standaloneRender.animationTime, cloudPixels, gridW, gridH, cloudGridLimit);
        auto surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>(SDL_CreateSurface(gridW, gridH, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
        if (!surface) throw std::runtime_error(SDL_GetError());
        for (int y = 0; y < gridH; ++y) for (int x = 0; x < gridW; ++x) {
            auto *pixel = static_cast<Uint8 *>(surface->pixels) + y * surface->pitch + x * 4;
            pixel[0] = pixel[1] = pixel[2] = 255; pixel[3] = cloudPixels[y * gridW + x];
        }
        TextureDescriptor descriptor; descriptor.addressU = descriptor.addressV = TextureDescriptor::Address::Repeat;
        if (state.cloud) resources.erase(state.cloud);
        state.cloud = resources.create(surface.get(), ++state.revision, descriptor);
        if (!state.cloud) throw std::runtime_error("Torus cloud resource budget exhausted");
        cloudW = gridW; cloudH = gridH;
    }
    DrawPacket depthClear; depthClear.kind = DrawPacket::Kind::Clear; depthClear.clearDepth = true;
    gfx->submitDevicePacket(depthClear);
    DrawPacket clear; clear.kind = DrawPacket::Kind::Clear; clear.clearColor = true;
    clear.clearValue = {.025f, .037f, .06f, 1}; clear.clip = SDL_Rect{0, 16, gfx->getW(), height - 16};
    gfx->submitDevicePacket(clear);
    // Portable particles retain the old physical point sizes. Driver-specific
    // GL_POINT_SMOOTH coverage is replaced by the shared circular AA material.
    int outputW, outputH; device.outputSize(outputW, outputH);
    const float physicalX = float(gfx->getW()) / outputW, physicalY = float(gfx->getH()) / outputH;
    const float skyYaw = -(anchorU - .5f) * 2 * pi;
    const float viewPitch = TorusGeometry::latitude(ringV, shape) + TorusGeometry::overviewTilt(ringV, roll, viewAspect, shape);
    for (int layer = 0; layer < 2; ++layer) {
        DrawPacket particle; particle.kind = DrawPacket::Kind::Triangles; particle.material = DrawPacket::Material::SmoothPoint;
        particle.blend = DrawPacket::Blend::Additive; particle.clip = clear.clip;
        for (const auto &point : skyPoints(layer == 0)) {
            TorusGeometry::Point projected;
            if (!TorusGeometry::projectSkyDirection({point.x, point.y, point.z}, {skyYaw, viewPitch}, roll, sx, sy, cameraDistance, projected)) continue;
            const float x = width * .5f + projected.x, y = (height + 16) * .5f + projected.y;
            if (x < -80 || x > gfx->getW() + 80 || y < -80 || y > height + 80) continue;
            const float size = layer == 0 ? 48 : point.size == 0 ? 1 : point.size == 1 ? 1.7f : 2.6f;
            const float a = size * physicalX * .5f, b = size * physicalY * .5f;
            const SDL_FColor tint = layer == 0 ? SDL_FColor{.28f, .33f, .48f, roll * (.003f + point.brightness * .008f)} :
                SDL_FColor{mix(.65f, 1, point.brightness), mix(.78f, .90f, point.brightness), mix(1, .72f, point.brightness), roll * (.20f + point.brightness * .65f)};
            const SDL_Vertex p{{x-a,y-b},tint,{0,0}}, q{{x+a,y-b},tint,{1,0}}, r{{x+a,y+b},tint,{1,1}}, t{{x-a,y+b},tint,{0,1}};
            particle.vertices.insert(particle.vertices.end(), {p,q,r,p,r,t});
        }
        if (!particle.vertices.empty()) gfx->submitDevicePacket(std::move(particle));
    }
    const int U = meshColumns, V = meshRows;
    pickU = anchorU; pickV = anchorV; pickWidth = width; pickHeight = height;
    const float cx = width * .5f, cy = (height + 16) * .5f, depthScale = 100;
    float key[9] = {roll, ringV, sx, sy, cx, cy, depthScale, cameraDistance, aspect};
    if (vertices.empty() || !state.cloudGeometry || std::memcmp(key, meshKey, sizeof(key))) {
        cachedPickX = cachedPickY = -1;
        vertices.resize((U + 1) * (V + 1)); cloudVertices.resize(vertices.size());
        // The cloud ring floats above the ground by a fixed share of the tube
        // radius; it settles onto the flat map as the fold opens.
        // Keep clouds outside the ground and inside the aperture on very tall maps.
        const float cloudHeight = std::min(0.039f * shape.tubeRadius,
                                          0.25f * (shape.majorRadius - shape.tubeRadius)) * roll;
        const float e = 0.0001f;
        for (int j = 0; j <= V; ++j)
            for (int i = 0; i <= U; ++i)
            {
                float du = float(i) / U - 0.5f;
                float dv = TorusGeometry::meshOffset(float(j) / V, ringV, shape);
                auto p = TorusGeometry::overviewPoint(du, dv, roll, ringV, viewAspect, shape);
                auto pu = TorusGeometry::overviewPoint(du + e, dv, roll, ringV, viewAspect, shape);
                auto pv = TorusGeometry::overviewPoint(du, dv + e, roll, ringV, viewAspect, shape);
                TorusGeometry::Point tu = TorusGeometry::subtract(pu, p), tv = TorusGeometry::subtract(pv, p);
                TorusGeometry::Point n = {tu.y * tv.z - tu.z * tv.y, tu.z * tv.x - tu.x * tv.z,
                                          tu.x * tv.y - tu.y * tv.x};
                float len = std::max(1e-12f, TorusGeometry::length(n));
                TorusGeometry::Point c = {p.x + n.x / len * cloudHeight, p.y + n.y / len * cloudHeight,
                                          p.z + n.z / len * cloudHeight};
                float nx = n.x / len, ry = n.y / len, rz = n.z / len;
                float light =
                    mix(1, 0.48f + 0.52f * clamp(-nx * 0.35f - ry * 0.45f + rz * 0.82f, 0, 1), roll);
                float w = 1 - p.z * roll / cameraDistance;
                vertices[j * (U + 1) + i] = {{cx * w + p.x * sx, cy * w + p.y * sy, p.z * depthScale, w},
                                             {light, light, light},
                                             {du, -dv},
                                             {nx, ry, rz}};
                float cw = 1 - c.z * roll / cameraDistance;
                cloudVertices[j * (U + 1) + i] = {{cx * cw + c.x * sx, cy * cw + c.y * sy, c.z * depthScale, cw},
                                                  {light, light, light},
                                                  {du, -dv},
                                                  {nx, ry, rz}};
            }

        std::memcpy(meshKey, key, sizeof(key)); tileMeshDirty = true;
        std::vector<std::uint32_t> indices;
        for (int j = 0; j < V; ++j) for (int i = 0; i < U; ++i) {
            const auto a = std::uint32_t(j * (U + 1) + i), b = a + U + 1;
            indices.insert(indices.end(), {a, b, a+1, a+1, b, b+1});
        }
        state.cloudGeometry.reset(); resources.erase(state.cloudMesh);
        state.cloudMesh = resources.createGeometry(GeometryLayout::World48, std::as_bytes(std::span(cloudVertices)), indices, ++state.revision);
        state.cloudGeometry = resources.snapshot(state.cloudMesh);
        if (!state.cloudGeometry) throw std::runtime_error("Torus cloud geometry budget exhausted");
    }
    if (tileMeshDirty || tileOffsetU != anchorU || tileOffsetV != 1-anchorV || !state.groundGeometry) {
        auto partitioned = TorusTextureTiles::partition(tiles, vertices, U, V, worldW*32, worldH*32, anchorU, 1-anchorV);
        for (auto &vertex : partitioned) vertex.uv[1] = 1 - vertex.uv[1]; // canonical top-left texture coordinates
        state.groundGeometry.reset(); resources.erase(state.ground);
        state.ground = resources.createGeometry(GeometryLayout::World48, std::as_bytes(std::span(partitioned)), {}, ++state.revision);
        state.groundGeometry = resources.snapshot(state.ground);
        if (!state.groundGeometry) throw std::runtime_error("Torus ground geometry budget exhausted");
        tileOffsetU = anchorU; tileOffsetV = 1-anchorV; tileMeshDirty = false;
    }
    WorldMaterial material;
    material.projection = {2.f/gfx->getW(),0,0,0, 0,-2.f/gfx->getH(),0,0, 0,0,-1.f/20000,0, -1,1,.5f,1};
    float lx = -1, ly = .25f, lz = .15f;
    const float ll = std::sqrt(lx*lx+ly*ly+lz*lz); lx/=ll; ly/=ll; lz=lz/ll+1;
    const float hl = std::sqrt(lx*lx+ly*ly+lz*lz);
    material.sunSpecular = {lx/hl,ly/hl,lz/hl,.18f*roll};
    for (std::size_t index = 0; index < tiles.size(); ++index) {
        const auto &tile = tiles[index];
        for (std::uint32_t offset = 0; offset < std::uint32_t(tile.count);) {
            const auto count = std::min<std::uint32_t>(65535, tile.count-offset);
            DrawPacket packet; packet.kind = DrawPacket::Kind::MeshTriangles; packet.material = DrawPacket::Material::WorldLit;
            packet.geometry = state.groundGeometry; packet.first = tile.first+offset; packet.count = count;
            packet.texture = state.tileTargets[index]; packet.linear = true; packet.blend = DrawPacket::Blend::Opaque;
            packet.depth = DrawPacket::Depth::LessEqual; packet.depthWrite = true; packet.clip = clear.clip; packet.world = material;
            gfx->submitDevicePacket(std::move(packet)); offset += count;
        }
    }
    if (drawClouds && state.cloud) {
        DrawPacket cloud; cloud.kind = DrawPacket::Kind::MeshTriangles; cloud.material = DrawPacket::Material::Cloud;
        cloud.geometry = state.cloudGeometry; cloud.indexed = true; cloud.count = U*V*6;
        cloud.texture = resources.snapshot(state.cloud); cloud.linear = true; cloud.blend = DrawPacket::Blend::SourceOver;
        cloud.depth = DrawPacket::Depth::LessEqual; cloud.depthWrite = false; cloud.clip = clear.clip; cloud.world = material;
        cloud.world.uvTransform = {1,-1,anchorU+float(originX)/worldW+.5f/cloudW,anchorV+float(originY)/worldH+.5f/cloudH};
        gfx->submitDevicePacket(std::move(cloud));
    }
    return true;
}
