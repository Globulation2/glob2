// SPDX-License-Identifier: GPL-3.0-or-later
#include "ColonySkinPreview.h"
#include "UnitConsts.h"
#include "UnitAnimation.h"
#include <GraphicContext.h>
#include <cstdlib>
#include <iostream>

ColonySkinPreview::ColonySkinPreview()
{
    const char *directory = std::getenv("GLOB2_SKIN_PREVIEW_DIR");
    if (!directory || !*directory) return;
    const std::string root(directory);
    const char *names[] = {"worker-walk", "worker-swim", "worker-harvest",
                           "warrior-walk", "warrior-swim", "warrior-fight", "explorer-fly"};
    for (unsigned i = 0; i < clips.size(); ++i)
    {
        std::string error;
        if (!clips[i].load(root + "/" + names[i] + ".gsk", error))
        {
            std::cerr << "Colony skin preview: " << names[i] << ": " << error << '\n';
            return;
        }
    }
    std::string swarmError;
    if (!swarm.load(root + "/swarm.gsk", swarmError))
        std::cerr << "Colony skin preview swarm: " << swarmError << '\n';
    texture = std::make_unique<GAGCore::DrawableSurface>(root + "/paint.png");
    ready = texture->getW() == 256 && texture->getH() == 256;
    if (!ready) std::cerr << "Colony skin preview needs a 256x256 paint.png\n";
}
ColonySkinPreview::~ColonySkinPreview() = default;

bool ColonySkinPreview::draw(GAGCore::GraphicContext &gfx, int type, int team,
                            int action, int direction, int delta, float x, float y)
{
    if (!ready || team != 0 || direction < 0 || direction > 8 || delta < 0 || delta > 255)
        return false;
    unsigned clip;
    if (type == EXPLORER && (action == STOP_FLY || action == FLY)) clip = 6;
    else if (type == WORKER || type == WARRIOR)
    {
        switch (action)
        {
            case STOP_WALK: case WALK: clip = 0; break;
            case STOP_SWIM: case SWIM: clip = 1; break;
            case BUILD: case HARVEST:
                if (type != WORKER) return false;
                clip = 2; break;
            case ATTACK_SPEED:
                if (type != WARRIOR) return false;
                clip = 2; break;
            default: return false;
        }
        if (type == WARRIOR) clip += 3;
    }
    else return false;
    const float size = clips[clip].logicalSize;
    const float offset = (size - 32) / 2;
    return gfx.drawSkinMesh(clips[clip], unitAnimationFrame(0, direction, delta),
                            *texture, x-offset, y-offset, size, size);
}

bool ColonySkinPreview::drawSwarm(GAGCore::GraphicContext &gfx, int team,
                                 float x, float y, float width, float height)
{
    return ready && team == 0 && swarm.identity &&
        gfx.drawSkinMesh(swarm, 0, *texture, x, y, width, height);
}
