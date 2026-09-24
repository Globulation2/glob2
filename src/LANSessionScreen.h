// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Glob2Screen.h"
#include "MapHeader.h"
#include <memory>
#include <optional>
class YOGClient;
namespace GAGGUI { class ScreenStack; }

// Owns an asynchronous LAN handshake and its lobby. The client is already
// connecting and may own an attached in-process server for hosting.
class LANSessionScreen : public Glob2Screen {
public:
    LANSessionScreen(GAGGUI::ScreenStack& screens, std::shared_ptr<YOGClient> client,
                     std::string username, std::optional<MapHeader> hostedMap = {});
    ~LANSessionScreen() override;
    void onTimer(Uint32 tick) override;
    void onAction(GAGGUI::Widget*, GAGGUI::Action, int, int) override;
private:
    enum class Stage { Greeting, Login, GameList, Lobby, Failed };
    void fail(const char* message);
    void enterLobby();
    GAGGUI::ScreenStack& screens;
    std::shared_ptr<YOGClient> client;
    std::string username;
    std::optional<MapHeader> hostedMap;
    Stage stage = Stage::Greeting;
    std::optional<Uint32> stageStarted;
};
