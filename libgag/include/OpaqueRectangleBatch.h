// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace GAGCore
{
class GraphicContext;

// A narrow scope containing only drawFilledRect and drawRect calls. Opaque
// fills share bounded OpenGL/SDL submissions. OpenGL outlines flush the fills
// to preserve order; software surfaces retain their ordinary drawing path.
// Scopes cannot nest, change clip/transform/context or draw other primitive types.
// Submission errors propagate on normal exit; unwinding discards pending fills.
class OpaqueRectangleBatch
{
public:
    explicit OpaqueRectangleBatch(GraphicContext *context);
    ~OpaqueRectangleBatch() noexcept(false);
    OpaqueRectangleBatch(const OpaqueRectangleBatch&) = delete;
    OpaqueRectangleBatch& operator=(const OpaqueRectangleBatch&) = delete;
private:
    bool active = false;
};
}
