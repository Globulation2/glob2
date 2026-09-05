// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Behaviour-equivalence harness for src/Campaign.cpp::load.
//
// Verifies that Campaign::load correctly distinguishes valid campaigns from
// missing / empty / garbage / bogus-version files. Pre-fix tree silently
// returns true for the four broken cases; post-fix tree returns false.
//
// Pattern follows WinningConditionsHarness: deterministic golden text on
// stdout, cppunit-free, behaviour-preserving cleanups verified by diff.
//
// Self-contained: synthesizes its own campaign fixture files in /tmp so the
// run is independent of the real campaigns/ directory.

#include "Campaign.h"
#include "Toolkit.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

const char* kTmpValid     = "/tmp/glob2_campaign_harness_valid.txt";
const char* kTmpEmpty     = "/tmp/glob2_campaign_harness_empty.txt";
const char* kTmpGarbage   = "/tmp/glob2_campaign_harness_garbage.txt";
const char* kTmpVersion0  = "/tmp/glob2_campaign_harness_v0.txt";
const char* kTmpVersionHi = "/tmp/glob2_campaign_harness_v999.txt";
const char* kMissing      = "/tmp/glob2_campaign_harness_does_not_exist.txt";

void writeFile(const char* path, const std::string& content)
{
	FILE* f = std::fopen(path, "wb");
	if (!f) { std::fprintf(stderr, "harness: cannot open %s\n", path); std::exit(2); }
	if (!content.empty())
		std::fwrite(content.data(), 1, content.size(), f);
	std::fclose(f);
}

void writeFixtures()
{
	const std::string valid =
		"versionMinor = 84;\n"
		"campaignName = \"TestCampaign\";\n"
		"playerName = \"Tester\";\n"
		"maps\n"
		"{\n"
		"\tmapNum = 2;\n"
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
		"\t\t\tdescription = \"desc1\";\n"
		"\t\t\tcompleted = 0;\n"
		"\t\t}\n"
		"\t}\n"
		"\t1\n"
		"\t{\n"
		"\t\tCampaignMap\n"
		"\t\t{\n"
		"\t\t\tmapName = \"Map2\";\n"
		"\t\t\tmapFileName = \"fake2.map\";\n"
		"\t\t\tisLocked = 1;\n"
		"\t\t\tunlockedBy\n"
		"\t\t\t{\n"
		"\t\t\t\tsize = 1;\n"
		"\t\t\t\t0\n"
		"\t\t\t\t{\n"
		"\t\t\t\t\tunlockedBy = \"Map1\";\n"
		"\t\t\t\t}\n"
		"\t\t\t}\n"
		"\t\t\tdescription = \"desc2\";\n"
		"\t\t\tcompleted = 0;\n"
		"\t\t}\n"
		"\t}\n"
		"}\n"
		"description = \"campaign description\";\n";
	writeFile(kTmpValid, valid);

	writeFile(kTmpEmpty, "");

	writeFile(kTmpGarbage, "this is not a campaign file at all }} { ;; \xff\xfe\x00 random");

	const std::string v0 =
		"versionMinor = 0;\n"
		"campaignName = \"ShouldNotLoad\";\n"
		"playerName = \"\";\n"
		"maps\n"
		"{\n"
		"\tmapNum = 0;\n"
		"}\n";
	writeFile(kTmpVersion0, v0);

	const std::string vHi =
		"versionMinor = 999;\n"
		"campaignName = \"FromTheFuture\";\n"
		"playerName = \"\";\n"
		"maps\n"
		"{\n"
		"\tmapNum = 0;\n"
		"}\n";
	writeFile(kTmpVersionHi, vHi);

	// kMissing: deliberately not created.
	std::remove(kMissing);
}

void runCase(const char* tag, const char* path)
{
	Campaign c;
	const bool ok = c.load(path);
	std::printf("%-12s ok=%d name=\"%s\" maps=%zu\n",
	            tag, ok ? 1 : 0, c.getName().c_str(), c.getMapCount());
}

}  // namespace

int main(int /*argc*/, char* /*argv*/[])
{
	// Silence the parser's cerr noise so the golden output stays deterministic.
	// TextStream's parser logs to stderr on malformed input; we don't want that
	// in the diff.
	std::freopen("/dev/null", "w", stderr);

	GAGCore::Toolkit::init("glob2");
	writeFixtures();

	std::printf("# CampaignLoadHarness golden output\n");
	runCase("valid",    kTmpValid);
	runCase("missing",  kMissing);
	runCase("empty",    kTmpEmpty);
	runCase("garbage",  kTmpGarbage);
	runCase("version0", kTmpVersion0);
	runCase("version+", kTmpVersionHi);

	GAGCore::Toolkit::close();
	return 0;
}
