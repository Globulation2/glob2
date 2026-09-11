// SPDX-License-Identifier: GPL-3.0-or-later
#include "SinglePlayerFlow.h"
#include "Engine.h"
#include "CustomGameScreen.h"
#include "ChooseMapScreen.h"
#include "GameSessionScreen.h"
#include "MessageScreen.h"
#include <Toolkit.h>
#include <StringTable.h>

void SinglePlayerFlow::launch(GameLoadScreen::Initializer initialize, bool repeatCustom, std::shared_ptr<void> mapFile)
{
    // The stack destroys the choosing screen before this loader reads its map,
    // so mapFile lives until the loader's own entry is released.
    screens.push(std::make_unique<GameLoadScreen>(std::move(initialize)),
        [this, repeatCustom, mapFile = std::move(mapFile)](GAGGUI::Screen& screen, int result) {
            if (result == 1)
                screens.push(std::make_unique<GameSessionScreen>(screens, static_cast<GameLoadScreen&>(screen).takeEngine()),
                    [this, repeatCustom](GAGGUI::Screen&, int) { if (repeatCustom) custom(); });
            else if (result == 2) {
                auto& strings = *GAGCore::Toolkit::getStringTable();
                screens.push(std::make_unique<MessageScreen>(strings.getString("[ERROR_CANT_LOAD_MAP]"),
                    std::vector<std::string>{strings.getString("[ok]")}),
                    [this, repeatCustom](GAGGUI::Screen&, int) { if (repeatCustom) custom(); });
            } else if (repeatCustom) custom();
        });
}

void SinglePlayerFlow::custom()
{
    screens.push(std::make_unique<CustomGameScreen>(screens), [this](GAGGUI::Screen& screen, int result) {
        if (result != CustomGameScreen::OK) return;
        auto& selected = static_cast<CustomGameScreen&>(screen);
        launch([map = selected.getMapHeader(), players = selected.getGameHeader(), team = selected.getSelectedColor(0),
                speed = selected.selectedSpeed(), source = selected.sourceFile()](Engine& engine) {
            return engine.initCustomTask(map, players, team, speed, source);
        }, true, selected.releaseSnapshot());
    });
}

void SinglePlayerFlow::load()
{
    screens.push(std::make_unique<ChooseMapScreen>("games", "game", true, "replays", "replay", false),
        [this](GAGGUI::Screen& screen, int result) {
            if (result != ChooseMapScreen::OK) return;
            auto& selected = static_cast<ChooseMapScreen&>(screen);
            const bool replay = selected.getSelectedType() == ChooseMapScreen::REPLAY;
            const auto filename = replay ? selected.getMapHeader().getFileName(false, true) : selected.getMapHeader().getFileName();
            launch([filename, replay](Engine& engine) {
                return replay ? engine.loadReplayTask(filename) : engine.initCustomTask(filename);
            }, false);
        });
}

void SinglePlayerFlow::replay(const std::string& filename)
{
    launch([filename](Engine& engine) { return engine.loadReplayTask(filename); }, false);
}
