#ifndef GLOB2_TORUS_VIEW_H
#define GLOB2_TORUS_VIEW_H
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
    void setViewport(int x, int y);
    void draw(Game &game, int team, unsigned options, int &viewportX, int &viewportY, int width, int height);
private:
    bool target, dragging;
    float amount, zoom;
    float travelU, travelV;
    float surfaceScaleX, surfaceScaleY;
    int baseViewportX, baseViewportY, worldW, worldH;
    int atlasW, atlasH;
    Uint32 lastFrame, lastCapture;
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
