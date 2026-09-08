// SPDX-License-Identifier: GPL-3.0-or-later
#include "EditorGenerateScreen.h"
#include "MapEdit.h"
#include "MapGenerator.h"
namespace {
GAGCore::CooperativeTask generate(MapEdit& editor, MapGenerationDescriptor descriptor, Uint32 seed)
{
    MapGenerator generator;
    if (!(co_await generator.generateMapTask(editor.game, descriptor, seed))) co_return false;
    editor.mapHasBeenModified();
    editor.regenerateGameHeader();
    co_return true;
}
}
EditorGenerateScreen::EditorGenerateScreen(MapGenerationDescriptor descriptor, Uint32 seed)
    : EditorLoadScreen([descriptor, seed](MapEdit& editor) { return generate(editor, descriptor, seed); }, "[Generating map]", 8) {}
