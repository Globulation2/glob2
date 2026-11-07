// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Engine-test decorator for cases that use code a compile_profile=core build leaves out
// (docs/development/build-options.md#core-compile-profile). Entries whose whole file needs
// it use `require` in test/tests.py instead. A skipped case is not listed, so the runner
// leaves it out of the run:
//     TEST_CASE("Castor resumes" * glob2test::needs({"ai:castor"}))
#include "AIRegistry.h"
#include "CompileProfile.h"
#include "Glob2Test.h"
#include <initializer_list>
#include <string_view>

namespace glob2test
{
// Whether this build has an optional group of scons/sources.py: "generators", "editor",
// "ai:castor", "ai:cortex" or "ai:cabino".
inline bool built(std::string_view group)
{
	if (group == "generators")
		return CompileProfile::hasGenerators();
	if (group == "editor")
		return CompileProfile::hasEditor();
	if (group == "ai:castor")
		return AIRegistry::available(AI::CASTOR);
	if (group == "ai:cortex")
		return AIRegistry::available(AI::CORTEX);
	if (group == "ai:cabino")
		return AIRegistry::available(AI::CABINO);
	return false;
}

inline doctest::skip needs(std::initializer_list<std::string_view> groups)
{
	for (auto group : groups)
		if (!built(group))
			return doctest::skip(true);
	return doctest::skip(false);
}
} // namespace glob2test
