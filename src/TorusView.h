// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef GLOB2_TORUS_VIEW_H
#define GLOB2_TORUS_VIEW_H
#include "TorusPicking.h"
#include <SDL.h>
class Game;

// Presentation-only state. Never serialized or sent to other players.
class TorusView
{
  public:
    TorusView();
    ~TorusView();
    bool active() const { return target || amount > 0; }
    bool enabled() const { return target; }
    bool available() const;
    void toggle();
    void resetCamera();
    bool event(const SDL_Event &event, int width, int &viewportX, int &viewportY);
    bool pick(int x, int y, int &worldPixelX, int &worldPixelY) const;
    void setViewport(int x, int y);
    void draw(Game &game, int team, unsigned options, int &viewportX, int &viewportY, int width, int height);

  private:
    static constexpr int meshColumns = 160, meshRows = 160;
    std::vector<TorusPicking::Vertex> vertices;
    mutable int cachedPickX = -1, cachedPickY = -1;
    mutable bool cachedPickFound = false;
    mutable TorusPicking::Hit cachedPick;
    float pickU = 0, pickV = 0;
    int pickWidth = 0, pickHeight = 0;
    bool target, dragging;
    float amount, zoom;
    float travelU, travelV;
    float cameraU = 0, cameraV = 0, cameraZoom = 1;
    float surfaceScaleX, surfaceScaleY;
    int baseViewportX, baseViewportY, worldW, worldH;
    int atlasW, atlasH;
    Uint32 lastFrame;
    std::vector<unsigned char> discoveryPixels;
    unsigned texture, visibility, framebuffer, material;
    unsigned meshBuffer, indexBuffer;
    float meshKey[8];
    bool failed;
    int originX, originY;
    float focusU, focusV;
    TorusView(const TorusView &);
    TorusView &operator=(const TorusView &);
};
#endif
