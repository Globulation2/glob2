// SPDX-License-Identifier: GPL-3.0-or-later
// Experimental presentation only: never changes simulation or saved map coordinates.
#ifndef GLOB2_WORLD3D_H
#define GLOB2_WORLD3D_H
#include <SDL.h>
class Game;
class Map;
class Building;
class BuildingType;
namespace GAGCore { class Color; }
namespace World3D {
bool enabled();
bool active(const Map* map);
void configure(const Map* map, int width, int height);
void detach(const Map* map);
bool key(SDL_Keysym key);
void zoomBy(int steps);
void panBy(float screenDx,float screenDy,int* viewportX,int* viewportY);
void panFrom(float screenDx,float screenDy,int startX,int startY,int* viewportX,int* viewportY);
void groundToScreen(float x, float y, float z, float& sx, float& sy);
void screenToGround(float sx, float sy, float& x, float& y);
void mapToScreen(const Map& map, float x, float y, int vx, int vy, int* sx, int* sy);
void screenToMap(const Map& map, int sx, int sy, int vx, int vy, int* x, int* y, bool nearest=false);
void brushPreview(Game& game,int mx,int my,int vx,int vy,int figure,int originalX,int originalY,const GAGCore::Color& color);
void draw(Game& game, int vx, int vy, int localTeam, unsigned options);
Building* hoveredBuilding();
void preview(Game& game, BuildingType* type, int x, int y, int vx, int vy, int team, bool valid, unsigned char alpha=180);
}
#endif
