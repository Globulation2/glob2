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

#include "Campaign.h"
#include "Toolkit.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

int failures = 0;

#define EXPECT(cond, msg)                                                  \
	do {                                                                   \
		if (!(cond)) {                                                     \
			std::fprintf(stderr, "FAIL: %s  (%s:%d)\n",                    \
			             (msg), __FILE__, __LINE__);                       \
			++failures;                                                    \
		}                                                                  \
	} while (0)

// Fixture files live under $TMPDIR (falling back to /tmp) so the harness
// can run inside sandboxes that only allow the per-session temp directory.
std::string tmpPath(const char* baseName)
{
	const char* dir = std::getenv("TMPDIR");
	std::string path = (dir && *dir) ? dir : "/tmp";
	if (path.back() != '/')
		path += '/';
	return path + baseName;
}

void writeFile(const std::string& path, const std::string& content)
{
	FILE* f = std::fopen(path.c_str(), "wb");
	if (!f) { std::fprintf(stderr, "harness: cannot open %s\n", path.c_str()); std::exit(2); }
	if (!content.empty())
		std::fwrite(content.data(), 1, content.size(), f);
	std::fclose(f);
}

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

int main(int /*argc*/, char* /*argv*/[])
{
	// Silence Campaign::load's cerr noise (missing-file case) so the golden
	// output stays deterministic.
	std::freopen("/dev/null", "w", stderr);

	GAGCore::Toolkit::init("glob2");

	std::printf("# CampaignDescriptionCacheHarness golden output\n");

	const std::string valid = tmpPath("glob2_desc_cache_valid.txt");
	const std::string missing = tmpPath("glob2_desc_cache_does_not_exist.txt");
	writeFile(valid, campaignFileText("first description"));
	std::remove(missing.c_str());

	CampaignDescriptionCache cache;

	// 1. Valid file: the campaign-level description comes back.
	const std::string& first = cache.getDescription(valid);
	std::printf("valid    -> \"%s\"\n", first.c_str());
	EXPECT(first == "first description",
	       "cache should return the campaign-level description of a valid file");

	// 2. Unreadable file: empty description, same as Campaign::load failure.
	const std::string& gone = cache.getDescription(missing);
	std::printf("missing  -> \"%s\"\n", gone.c_str());
	EXPECT(gone.empty(), "unreadable file should yield an empty description");

	// 3. Rewrite the file; a repeated lookup must serve the cached value —
	// this is the observable proof that the file is not re-parsed per call.
	writeFile(valid, campaignFileText("second description"));
	const std::string& again = cache.getDescription(valid);
	std::printf("cached   -> \"%s\"\n", again.c_str());
	EXPECT(again == "first description",
	       "repeated lookup must not re-parse the file (cache miss = bug)");

	// 4. A fresh cache (a new selector screen) sees the current file content.
	CampaignDescriptionCache freshCache;
	const std::string& fresh = freshCache.getDescription(valid);
	std::printf("fresh    -> \"%s\"\n", fresh.c_str());
	EXPECT(fresh == "second description",
	       "a new cache instance must read the current on-disk content");

	std::remove(valid.c_str());
	GAGCore::Toolkit::close();

	std::printf("result: %d failure(s)\n", failures);
	return failures == 0 ? 0 : 1;
}
