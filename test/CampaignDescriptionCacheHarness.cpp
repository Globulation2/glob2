// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Regression harness for the CampaignSelectorScreen description re-parse
// smell: every LIST_ELEMENT_SELECTED event constructed a Campaign and
// re-parsed the entire campaign file from disk just to display its
// description string.
//
// The campaign file format stores the description as the *last* field and
// TextInputStream parses the whole file up front in its constructor, so
// there is no cheaper "read only the description" path — the fix is
// CampaignDescriptionCache, which parses each file at most once per cache
// instance and remembers the result.
//
// This harness proves:
//   1. The cache returns the correct description for a valid campaign file.
//   2. An unreadable (missing) file yields "" — identical to what
//      Campaign::load leaves behind on failure.
//   3. Rewriting the file with a different description does NOT change a
//      repeated lookup — proof the file is not re-parsed per request.
//   4. A fresh cache instance sees the new on-disk content — proof the
//      cache is per-instance (per-screen), not hidden global state.
//
// Pre-fix tree: this harness fails to LINK because CampaignDescriptionCache
// does not exist. That is the "broken before" signal.
// Post-fix tree: builds, runs, exits 0, prints a deterministic golden line
// stream for diff-based verification.

#include "Glob2Test.h"
#include "Campaign.h"

#include <cstdio>
#include <string>

namespace {


// Minimal valid version-84 campaign file with a given campaign-level
// description (the last field in the format).
std::string campaignFileText(const std::string& description)
{
	return
		"versionMinor = 84;\n"
		"campaignName = \"CacheCampaign\";\n"
		"playerName = \"Tester\";\n"
		"maps\n"
		"{\n"
		"\tmapNum = 1;\n"
		"\t0\n"
		"\t{\n"
		"\t\tCampaignMap\n"
		"\t\t{\n"
		"\t\t\tmapName = \"Map1\";\n"
		"\t\t\tmapFileName = \"fake1.map\";\n"
		"\t\t\tisLocked = 0;\n"
		"\t\t\tunlockedBy\n"
		"\t\t\t{\n"
		"\t\t\t\tsize = 0;\n"
		"\t\t\t}\n"
		"\t\t\tdescription = \"map desc\";\n"
		"\t\t\tcompleted = 0;\n"
		"\t\t}\n"
		"\t}\n"
		"}\n"
		"description = \"" + description + "\";\n";
}

}  // namespace

TEST_SUITE("CampaignDescriptionCache")
{
TEST_CASE("descriptions are parsed once per cache instance")
{
	// Campaign::load logs to stderr for the missing-file case.
	glob2test::CapturedStderr quiet;
	glob2test::ToolkitScope toolkit;
	glob2test::TempDir dir;
	const std::string valid = (dir.path / "valid.txt").string();
	const std::string missing = (dir.path / "does_not_exist.txt").string();
	glob2test::writeFile(valid, campaignFileText("first description"));

	CampaignDescriptionCache cache;

	// 1. Valid file: the campaign-level description comes back.
	const std::string& first = cache.getDescription(valid);
	GLOB2_CHECK(first == "first description",
	       "cache should return the campaign-level description of a valid file");

	// 2. Unreadable file: empty description, same as Campaign::load failure.
	const std::string& gone = cache.getDescription(missing);
	GLOB2_CHECK(gone.empty(), "unreadable file should yield an empty description");

	// 3. Rewrite the file; a repeated lookup must serve the cached value —
	// this is the observable proof that the file is not re-parsed per call.
	glob2test::writeFile(valid, campaignFileText("second description"));
	const std::string& again = cache.getDescription(valid);
	GLOB2_CHECK(again == "first description",
	       "repeated lookup must not re-parse the file (cache miss = bug)");

	// 4. A fresh cache (a new selector screen) sees the current file content.
	CampaignDescriptionCache freshCache;
	const std::string& fresh = freshCache.getDescription(valid);
	GLOB2_CHECK(fresh == "second description",
	       "a new cache instance must read the current on-disk content");

}
}
