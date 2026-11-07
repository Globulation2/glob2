// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace GAGGUI
{
class ScreenStack;
}

// The map editor's entry point, so menus do not link the editor themselves. Defined in
// EditorMainMenu.cpp; a compile_profile=core build, which leaves the editor out, uses
// the fallback in src/app/CompileProfile.cpp.
namespace EditorEntry
{
// Pushes the editor's main menu; false when this build has no editor.
bool open(GAGGUI::ScreenStack &screens);
}
