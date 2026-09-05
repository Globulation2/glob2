// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef GLOB2_WORLD3D_PLACEMENT_H
#define GLOB2_WORLD3D_PLACEMENT_H
#include "World3DCamera.h"

namespace World3DPlacement {
struct Point { float x, y; };
// Source sprite frame 1 is northwest. With its sprite turntable removed,
// workers/explorers face -X; warriors face +Y (their turntable started at 90deg).
inline float modelYawDegrees(int dx,int dy,bool warrior) {
 return std::atan2(static_cast<float>(dy),static_cast<float>(dx))*180.f/3.14159265f
        -(warrior?90.f:180.f);
}

// Match Game::drawUnit's integer movement interpolation, then choose the
// nearest torus copy of the interpolated tile center (not its destination).
inline float unitAxis(int tile, int direction, int phase, bool moving,
                      int viewport, int mapSize, float viewCenter) {
 const float step = moving ? std::floor(direction * (255-phase) / 8.f) : 0.f;
 return World3DCamera::wrappedOffset(tile + .5f - step/32.f,
                                     viewport, mapSize, viewCenter/32.f) * 32.f;
}

// Offset only the artwork, halfway from the footprint center to its
// camera-facing edge. Follow the view direction so rotation adds no sideways drift.
inline Point buildingAnchor(float x, float y, float w, float h, float yaw) {
 const float sx=std::sin(yaw),sy=std::cos(yaw);
 const float distanceX=std::abs(sx)>.00001f?w*.5f/std::abs(sx):1e9f;
 const float distanceY=std::abs(sy)>.00001f?h*.5f/std::abs(sy):1e9f;
 const float offset=.5f*std::fmin(distanceX,distanceY);
 return {x+w*.5f+sx*offset,y+h*.5f+sy*offset};
}
}
#endif
