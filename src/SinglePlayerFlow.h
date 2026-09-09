// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <ScreenStack.h>
#include "GameLoadScreen.h"
#include "RecoveryStore.h"
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
    void recover();
    void load();
    void replay(const std::string& filename);
private:
    GAGGUI::ScreenStack& screens;
    void recoverCandidate(std::shared_ptr<std::vector<RecoveryStore::Record>> records, size_t index);
    void launch(GameLoadScreen::Initializer initialize, bool repeatCustom);
};
