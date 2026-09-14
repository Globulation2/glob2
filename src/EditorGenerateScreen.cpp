// SPDX-License-Identifier: GPL-3.0-or-later
#include "EditorGenerateScreen.h"
#include "MapEdit.h"
#include "GenerationService.h"
#include <CooperativeTask.h>
namespace {
GAGCore::CooperativeTask generate(MapEdit& editor, GenerationRequest descriptor, Uint32 seed)
{
    co_await GAGCore::CooperativeTask::checkpoint("[Generating map]");
    GenerationService generator;
    descriptor.seed = generator.bestSeed(descriptor, seed);
    if (!generator.generate(editor.game, descriptor)) co_return false;
    editor.mapHasBeenModified();
    editor.regenerateGameHeader();
    co_return true;
}
}
EditorGenerateScreen::EditorGenerateScreen(GenerationRequest descriptor, Uint32 seed, GAGCore::CooperativeSlice slice)
    : EditorLoadScreen([descriptor, seed](MapEdit& editor) { return generate(editor, descriptor, seed); }, "[Generating map]", std::move(slice)) {}

void EditorGenerateScreen::onTimer(Uint32 tick)
{
    // Present the progress screen and admit cancellation before starting a roll.
    if (!presented) { presented = true; return; }
    EditorLoadScreen::onTimer(tick);
}
