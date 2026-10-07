// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef GLOB2_TORUS_VIEW_H
#define GLOB2_TORUS_VIEW_H
#include "TorusPicking.h"
#include "TorusTextureTiles.h"
#include "DynamicClouds.h"
#include <SDL3/SDL.h>
#include "render/MapRenderState.h"
class Game;

// Presentation-only state. Never serialized or sent to other players.
class TorusView
{
  public:
    TorusView();
    ~TorusView();
    // Manual mode stays flat until toggled; automatic mode can follow movement.
    bool active() const { return target || moving || amount > 0; }
    // Switched on by hand: the ring is the map view, at the 2D camera's zoom.
    bool enabled() const { return target; }
    bool overviewSettled() const { return amount == 1; }
    bool available() const;
    // Drop camera, picking and GPU state before loading another game.
    void reset();
    void toggle();
    bool pick(int x, int y, int &worldPixelX, int &worldPixelY) const;
    void setViewport(int x, int y);
    // The viewport moved without the focus moving: the camera zoomed about it.
    void rebaseViewport(int x, int y);
    void notifyMove();
    void stopMoving() { moving = false; }
    void setPointerHeld(bool held) { pointerHeld = held; }
    // Middle-button panning is a held gesture even between motion events.
    void setPanHeld(bool held) { panHeld = held; }
    // False requests the ordinary 2D renderer on this same frame. The ring
    // shows its focus at `flatZoom`, the 2D camera's zoom, so both views share
    // one zoom and one level of detail.
    bool draw(Game &game, int team, unsigned options, int &viewportX, int &viewportY, int width,
              int height, float flatZoom = 1, float fractionX = 0, float fractionY = 0);

  private:
    void releaseMaterials();
    bool prepareRenderTarget();
    void updateClouds(int time);
    static constexpr int meshColumns = 160, meshRows = 160;
    std::vector<TorusPicking::Vertex> vertices, cloudVertices;
    mutable int cachedPickX = -1, cachedPickY = -1;
    mutable bool cachedPickFound = false;
    mutable TorusPicking::Hit cachedPick;
    float pickU = 0, pickV = 0;
    int pickWidth = 0, pickHeight = 0;
    bool target;
    // An automatic reveal pulls back to the whole ring, whatever the zoom.
    bool wholeRing = false;
    bool moving = false, pointerHeld = false, panHeld = false;
    Uint32 lastMove = 0;
    float amount;
    float travelU, travelV;
    float cameraU = 0, cameraV = 0;
    float viewAspect = 1.6f, ringAspect = 0, ringMapAspect = 0;
    float ringWidth = 1, ringHeight = 1, focusGain = 1;
    int baseViewportX, baseViewportY, worldW, worldH;
    std::vector<TorusTextureTiles::Tile> tiles;
    int pixelsPerCell = 32;
    // Private test seams: simulate smaller hardware and allocation pressure.
    int textureLimit = 2048, allocationPixelLimit = 0;
    float tileOffsetU = -100, tileOffsetV = -100;
    bool tileMeshDirty = true;
    Uint32 lastFrame;
    DynamicClouds clouds;
    // Map animation state for games drawn without a GameGUI (whose view owns it).
    MapRenderState standaloneRender;
    std::valarray<unsigned char> cloudPixels;
    int cloudW = 0, cloudH = 0;
    SDL_GLContext graphicsContext = nullptr;
    unsigned graphicsGeneration = 0;
    unsigned cloudTexture, framebuffer, material;
    unsigned meshBuffer, cloudBuffer, indexBuffer;
    unsigned tileBuffer = 0;
    float meshKey[9];
    bool failed;
    int originX, originY;
    float focusU, focusV;
    TorusView(const TorusView &) = delete;
    TorusView &operator=(const TorusView &) = delete;
};
#endif
