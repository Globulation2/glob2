// SPDX-License-Identifier: GPL-3.0-or-later
#include "GameLoadScreen.h"
#include "Engine.h"
#include "Utilities.h"
#include <GUIText.h>
#include <GUIButton.h>
#include <Toolkit.h>
#include <StringTable.h>
#include <iostream>
GameLoadScreen::GameLoadScreen(Initializer initialize)
    : previousRng(getSyncRandState()), engine(std::make_unique<Engine>())
{
    auto& strings = *GAGCore::Toolkit::getStringTable();
    status = new GAGGUI::Text(0, 180, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "standard", strings.getString("[Loading headers]"));
    addWidget(status);
    addWidget(new GAGGUI::TextButton(230, 340, 180, 40, ALIGN_SCREEN_CENTERED,
        ALIGN_SCREEN_CENTERED, "menu", strings.getString("[Cancel]"), 0, 27));
    task.emplace(initialize(*engine));
}
GameLoadScreen::~GameLoadScreen()
{
    task.reset();
    if (!accepted) engine->cancelInitialization();
    engine.reset();
    if (!accepted) setSyncRandState(previousRng);
}
std::unique_ptr<Engine> GameLoadScreen::takeEngine()
{
    if (!task->result()) throw std::logic_error("Cannot accept a failed game load");
    accepted = true;
    task.reset();
    return std::move(engine);
}
void GameLoadScreen::onTimer(Uint32)
{
    try {
        for (unsigned checkpoint = 0; checkpoint < 8; ++checkpoint)
            if (task->advance()) { endExecute(task->result() ? 1 : 2); return; }
        const char* stage = task->stage();
        if (*stage) status->setText(GAGCore::Toolkit::getStringTable()->getString(stage));
    } catch (const std::exception& error) {
        std::cerr << "Game initialization failed: " << error.what() << '\n';
        endExecute(2);
    }
}
void GameLoadScreen::onAction(GAGGUI::Widget*, GAGGUI::Action action, int, int)
{
    if (action == GAGGUI::BUTTON_RELEASED || action == GAGGUI::BUTTON_SHORTCUT) endExecute(0);
}
