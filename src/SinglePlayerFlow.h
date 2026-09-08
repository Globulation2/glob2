// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <ScreenStack.h>
#include <memory>
#include <string>
class Engine;

// Lives alongside its stack until the flow finishes. Owns navigation only;
// simulation and file formats remain in Engine.
class SinglePlayerFlow
{
public:
    explicit SinglePlayerFlow(GAGGUI::ScreenStack& screens) : screens(screens) {}
    void custom();
    void load();
    void replay(const std::string& filename);
private:
    GAGGUI::ScreenStack& screens;
    void launch(std::unique_ptr<Engine> engine, int result, bool repeatCustom);
};
