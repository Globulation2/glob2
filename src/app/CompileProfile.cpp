// SPDX-License-Identifier: GPL-3.0-or-later
#include "CompileProfile.h"
#include <stdexcept>
// Native builds generate this header; builders that compile every source may not.
#if __has_include(<glob2/CompileProfile.h>)
#include <glob2/CompileProfile.h>
#else
#define GLOB2_COMPILE_PROFILE "full"
#define GLOB2_PROFILE_HAS_GENERATORS 1
#define GLOB2_PROFILE_HAS_EDITOR 1
#endif
#if !GLOB2_PROFILE_HAS_EDITOR
#include "EditorEntry.h"
bool EditorEntry::open(GAGGUI::ScreenStack &) { return false; }
#endif

namespace CompileProfile
{
const char *name() { return GLOB2_COMPILE_PROFILE; }
bool isFull() { return std::string(GLOB2_COMPILE_PROFILE) == "full"; }
bool hasGenerators() { return GLOB2_PROFILE_HAS_GENERATORS; }
bool hasEditor() { return GLOB2_PROFILE_HAS_EDITOR; }
std::string missing(const std::string &what)
{
	return what + " is not compiled into this build (compile_profile=" + name() +
		   "); rebuild with compile_profile=full";
}
void requireFull(const std::string &role)
{
	if (!isFull())
		throw std::runtime_error(role + " needs the full simulation, which this build (compile_profile=" +
								 name() + ") leaves incomplete; rebuild with compile_profile=full");
}
}
