// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The source and configuration identity of the running test executable, as JSON.
// Native builds read <executable>.provenance.json, which scons/test_provenance.py
// writes after linking, so a commit that changes no compiled input relinks nothing.
// Browser and mobile builds compile the identity in (GLOB2_TEST_PROVENANCE_EMBEDDED).
#ifdef GLOB2_TEST_PROVENANCE_EMBEDDED
#include <glob2/TestBuildProvenance.h>
#endif
#include <SDL3/SDL_filesystem.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace glob2test
{
inline std::string buildProvenanceJson([[maybe_unused]] const char *argv0)
{
#ifdef GLOB2_TEST_PROVENANCE_EMBEDDED
	return GLOB2_TEST_PROVENANCE_JSON;
#else
	const char *base = SDL_GetBasePath();
	if (!base || !argv0)
		return "{}";
	const std::string name = std::filesystem::path(argv0).filename().string();
	for (const std::string &candidate : {name + ".provenance.json", name + ".exe.provenance.json"})
	{
		std::ifstream input(std::filesystem::path(base) / candidate);
		if (!input)
			continue;
		std::ostringstream text;
		text << input.rdbuf();
		std::string json = text.str();
		while (!json.empty() && (json.back() == '\n' || json.back() == '\r'))
			json.pop_back();
		return json.empty() ? "{}" : json;
	}
	// An empty identity makes evidence checkers report missing provenance.
	return "{}";
#endif
}
}
