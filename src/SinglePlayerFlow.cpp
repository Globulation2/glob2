// SPDX-License-Identifier: GPL-3.0-or-later
#include "SinglePlayerFlow.h"
#include "Engine.h"
#include "CustomGameScreen.h"
#include "ChooseMapScreen.h"
#include "GameSessionScreen.h"

void SinglePlayerFlow::launch(std::unique_ptr<Engine> engine, int result, bool repeatCustom)
{
    if (result == GAGGUI::Screen::QUIT_APPLICATION) { screens.stop(); return; }
    if (result != Engine::EE_NO_ERROR) return;
    screens.push(std::make_unique<GameSessionScreen>(screens, std::move(engine)),
        [this, repeatCustom](GAGGUI::Screen&, int) { if (repeatCustom) custom(); });
}

void SinglePlayerFlow::custom()
{
    screens.push(std::make_unique<CustomGameScreen>(screens), [this](GAGGUI::Screen& screen, int result) {
        if (result != CustomGameScreen::OK) return;
        auto& selected = static_cast<CustomGameScreen&>(screen);
        auto engine = std::make_unique<Engine>();
        const int loaded = engine->initCustom(selected.getMapHeader(), selected.getGameHeader(), selected.getSelectedColor(0));
        launch(std::move(engine), loaded, true);
    });
}

void SinglePlayerFlow::load()
{
    screens.push(std::make_unique<ChooseMapScreen>("games", "game", true, "replays", "replay", false),
        [this](GAGGUI::Screen& screen, int result) {
            if (result != ChooseMapScreen::OK) return;
            auto& selected = static_cast<ChooseMapScreen&>(screen);
            auto engine = std::make_unique<Engine>();
            int loaded = Engine::EE_CANT_LOAD_MAP;
            if (selected.getSelectedType() == ChooseMapScreen::GAME)
                loaded = engine->initCustom(selected.getMapHeader().getFileName());
            else if (selected.getSelectedType() == ChooseMapScreen::REPLAY)
                loaded = engine->loadReplay(selected.getMapHeader().getFileName(false, true));
            launch(std::move(engine), loaded, false);
        });
}

void SinglePlayerFlow::replay(const std::string& filename)
{
    auto engine = std::make_unique<Engine>();
    const int loaded = engine->loadReplay(filename);
    launch(std::move(engine), loaded, false);
}
