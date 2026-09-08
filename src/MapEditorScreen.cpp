// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapEditorScreen.h"
#include "MapEdit.h"
#include "MessageScreen.h"
#include <Toolkit.h>
#include <StringTable.h>
#include <stdexcept>

MapEditorScreen::MapEditorScreen(GAGGUI::ScreenStack& screens, std::unique_ptr<MapEdit> editor)
    : screens(screens), editor(std::move(editor))
{
    if (!this->editor) throw std::invalid_argument("Map editor screen requires an editor");
}
MapEditorScreen::~MapEditorScreen() = default;
void MapEditorScreen::updateExecution(Uint32 tick)
{
    if (!isExecutionRunning()) return;
    lastFrame = tick;
    if (!started) { editor->beginEditing(); started = true; }
    const bool running = editor->advanceEditing(input, tick);
    input.clear();
    if (!running) { endExecute(editor->editingReturnCode()); return; }
    if (editor->needsQuitDecision()) {
        auto& strings = *GAGCore::Toolkit::getStringTable();
        screens.push(std::make_unique<MessageScreen>(strings.getString("[save before quit?]"),
            std::vector<std::string>{strings.getString("[Yes]"), strings.getString("[No]"), strings.getString("[Cancel]")}),
            [this](GAGGUI::Screen&, int choice) { editor->resolveQuitDecision(choice); });
    }
}
void MapEditorScreen::handleExecutionEvent(SDL_Event event)
{
    if (isExecutionRunning()) input.push_back(event);
}
void MapEditorScreen::drawExecution()
{
    if (started && isExecutionRunning()) editor->drawEditing();
}
Uint32 MapEditorScreen::executionDelay(Uint32 now, Uint32)
{
    const Uint32 elapsed = now - lastFrame;
    return elapsed < 33 ? 33 - elapsed : 0;
}
