// SPDX-License-Identifier: GPL-3.0-or-later
#include "EditorLoadScreen.h"
#include "MapEdit.h"
#include "Utilities.h"
#include <GUIText.h>
#include <GUIButton.h>
#include <Toolkit.h>
#include <StringTable.h>
#include <iostream>
EditorLoadScreen::EditorLoadScreen(const std::string& filename)
    : EditorLoadScreen([filename](MapEdit& editor) { return editor.loadTask(filename); }, "[Loading headers]") {}
EditorLoadScreen::EditorLoadScreen(Initializer initialize, const char* caption)
    : previousRng(getSyncRandState()), editor(std::make_unique<MapEdit>())
{
    auto& strings = *GAGCore::Toolkit::getStringTable();
    status = new GAGGUI::Text(0, 180, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "standard",
        strings.getString(caption));
    addWidget(status);
    addWidget(new GAGGUI::TextButton(230, 340, 180, 40, ALIGN_SCREEN_CENTERED,
        ALIGN_SCREEN_CENTERED, "menu", strings.getString("[Cancel]"), 0, 27));
    task.emplace(initialize(*editor));
}
EditorLoadScreen::~EditorLoadScreen()
{
    task.reset();
    editor.reset();
    if (!accepted) setSyncRandState(previousRng);
}
std::unique_ptr<MapEdit> EditorLoadScreen::takeEditor()
{
    if (!task->result()) throw std::logic_error("Cannot accept failed editor load");
    accepted = true;
    task.reset();
    return std::move(editor);
}
void EditorLoadScreen::onTimer(Uint32)
{
    try {
        if (task->advance()) { endExecute(task->result() ? 1 : 2); return; }
        status->setText(GAGCore::Toolkit::getStringTable()->getString(task->stage()));
    } catch (const std::exception& error) {
        std::cerr << "Editor preparation failed: " << error.what() << '\n';
        endExecute(2);
    }
}
void EditorLoadScreen::onAction(GAGGUI::Widget*, GAGGUI::Action action, int, int)
{
    if (action == GAGGUI::BUTTON_RELEASED || action == GAGGUI::BUTTON_SHORTCUT) endExecute(0);
}
