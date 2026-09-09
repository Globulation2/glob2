// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapEditorScreen.h"
#include "MapEdit.h"
#include "MessageScreen.h"
#include "FertilityScreen.h"
#include "EditorLoadScreen.h"
#include <Toolkit.h>
#include <StringTable.h>
#include <stdexcept>

MapEditorScreen::MapEditorScreen(GAGGUI::ScreenStack& screens, std::unique_ptr<MapEdit> editor)
    : screens(screens), editor(std::move(editor))
{
    if (!this->editor) throw std::invalid_argument("Map editor screen requires an editor");
}
MapEditorScreen::~MapEditorScreen() = default;
bool MapEditorScreen::usesResponsiveViewport() const { return editor->usesPhone(); }
void MapEditorScreen::updateExecution(Uint32 tick)
{
    if (!isExecutionRunning()) return;
    lastFrame = tick;
    if (!started) { editor->beginEditing(); started = true; }
    const bool running = editor->advanceEditing(input, tick);
    input.clear();
    if (!running) { endExecute(editor->editingReturnCode()); return; }
    const auto replacement = editor->takeLoadRequest();
    if (!replacement.empty()) {
        editor->suspendInput();
        screens.push(std::make_unique<EditorLoadScreen>(replacement), [this](GAGGUI::Screen& loading, int result) {
            if (result == 1) {
                editor = static_cast<EditorLoadScreen&>(loading).takeEditor();
                started = false;
                input.clear();
            } else if (result == 2) {
                auto& strings = *GAGCore::Toolkit::getStringTable();
                screens.push(std::make_unique<MessageScreen>(strings.getString("[ERROR_CANT_LOAD_MAP]"),
                    std::vector<std::string>{strings.getString("[ok]")}));
            }
        });
    } else if (editor->needsFertility()) {
        editor->suspendInput();
        screens.push(std::make_unique<FertilityScreen>(editor->game.map),
            [this](GAGGUI::Screen&, int result) {
                if (!editor->finishFertility(result == 1)) {
                    auto& strings = *GAGCore::Toolkit::getStringTable();
                    screens.push(std::make_unique<MessageScreen>(strings.getString("[ERROR_CANT_SAVE_MAP]"),
                        std::vector<std::string>{strings.getString("[ok]")}));
                }
            });
    } else if (editor->needsQuitDecision()) {
        editor->suspendInput();
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
void MapEditorScreen::cancelExecutionInput()
{
    input.clear();
    editor->suspendInput();
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

void MapEditorScreen::viewportResized(int oldWidth, int oldHeight, int width, int height)
{
    editor->suspendInput();
    input.clear();
    editor->viewportResized(oldWidth, oldHeight, width, height);
}

void MapEditorScreen::suspendExecution()
{
    editor->suspendInput();
    input.clear();
}
