// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef GLOB2_TORUS_VIEW_H
#define GLOB2_TORUS_VIEW_H
#include "TorusPicking.h"
#include "Game.h"
#include "BuildingGuiState.h"
#include "DynamicClouds.h"
#include <SDL.h>

// Presentation-only state. Never serialized or sent to other players.
class TorusView
{
  public:
    TorusView();
    ~TorusView();
    // Drawn as a torus: switched on, pulled back by movement, or still unfolding.
    bool active() const { return target || moving || amount > 0; }
    // Switched on by hand: the 2D scrolling rests and the wheel zooms the ring.
    bool enabled() const { return target; }
    // The 2D view moved this frame: pull back slowly while it keeps moving.
    void notifyMove();
    bool available() const;
    // Drop camera, picking and GPU state before loading another game.
    void reset();
    void toggle();
    void resetCamera();
    bool event(const SDL_Event &event, int width, int &viewportX, int &viewportY);
    bool pick(int x, int y, int &worldPixelX, int &worldPixelY) const;
    void setViewport(int x, int y);
    // False requests the ordinary 2D renderer on this same frame.
    bool draw(Game &game, int team, unsigned options, int &viewportX, int &viewportY, int width,
              int height, Game::ViewState &view, const BuildingGuiStateMap *buildingGuiState);

  private:
    void releaseResources();
    bool prepareRenderTarget();
    void updateClouds();
    static constexpr int meshColumns = 160, meshRows = 160;
    std::vector<TorusPicking::Vertex> vertices;
    mutable int cachedPickX = -1, cachedPickY = -1;
    mutable bool cachedPickFound = false;
    mutable TorusPicking::Hit cachedPick;
    float pickU = 0, pickV = 0;
    int pickWidth = 0, pickHeight = 0;
    bool target, dragging;
    bool moving = false;
    Uint32 lastMove = 0;
    float amount, zoom;
    float travelU, travelV;
    float cameraU = 0, cameraV = 0, cameraZoom = 1;
    float surfaceScaleX, surfaceScaleY;
    int baseViewportX, baseViewportY, worldW, worldH;
    int atlasW, atlasH;
    Uint32 lastFrame;
    DynamicClouds clouds;
    std::valarray<unsigned char> cloudPixels;
    int cloudW = 0, cloudH = 0;
    SDL_GLContext graphicsContext = nullptr;
    unsigned graphicsGeneration = 0;
    unsigned texture, cloudTexture, framebuffer, material;
    unsigned meshBuffer, cloudBuffer, indexBuffer;
    float meshKey[8];
    bool failed;
    int originX, originY;
    float focusU, focusV;
    TorusView(const TorusView &) = delete;
    TorusView &operator=(const TorusView &) = delete;
};
#endif
