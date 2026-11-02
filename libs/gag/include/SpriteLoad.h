// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <AssetLoader.h>
#include <memory>
#include <string>

namespace GAGCore {
class Sprite;
// A frame-pumped sprite load. CPU work belongs to AssetLoader; adoption and
// publication belong to the presentation thread. No partially loaded Sprite escapes.
class SpriteLoad {
public:
    SpriteLoad(std::string name, bool allowVariableAtlas = false);
    ~SpriteLoad();
    bool poll(std::chrono::milliseconds budget = std::chrono::milliseconds(2));
    // Reuse the caller's deadline so nested polling does not reset its budget.
    bool pollUntil(std::chrono::steady_clock::time_point deadline);
    bool failed() const;
    std::string error() const;
    std::unique_ptr<Sprite> take();
    size_t completed() const;
    size_t total() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
