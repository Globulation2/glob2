// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Stubs for symbols referenced by Campaign.o that the load-path harness
// doesn't actually exercise. Campaign::save() calls glob2NameToFilename()
// to build a save path; the real definition lives in MapHeader.cpp, which
// would drag in Map / Game / etc. The harness never invokes save(), so a
// stub satisfies the linker without that transitive surface.

#include <string>

std::string glob2NameToFilename(const std::string& /*dir*/,
                                const std::string& /*name*/,
                                const std::string& /*extension*/)
{
	return std::string();
}
