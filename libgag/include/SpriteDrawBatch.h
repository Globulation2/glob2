// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace GAGCore
{
class GraphicContext;
class Sprite;

// An OpenGL/portable SDL batch for one sprite family on the presentation thread.
// Quads sharing a texture and alpha can share a submission when intervening
// draws do not overlap them.
// Overlapping draws retain their order, including standalone HD and native
// atlas frames. Software surfaces and dynamic team-color sprites keep their
// ordinary path. Referenced surfaces must stay alive throughout the scope.
// Within this scope, draw only this sprite; do not change clip, transform,
// context or artwork. Scopes cannot nest. Keep the scope on the stack.
// Submission errors propagate on normal exit; unwinding discards pending draws.
class SpriteDrawBatch
{
public:
    SpriteDrawBatch(GraphicContext *context, Sprite *sprite);
    ~SpriteDrawBatch() noexcept(false);
    SpriteDrawBatch(const SpriteDrawBatch&) = delete;
    SpriteDrawBatch& operator=(const SpriteDrawBatch&) = delete;
private:
    bool active = false;
};
}
