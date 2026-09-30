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

#include "Glob2Test.h"
#include "Campaign.h"

#include <cstdio>
#include <string>

namespace {

struct Fixtures
{
	glob2test::TempDir dir;
	std::string valid = (dir.path / "valid.txt").string();
	std::string empty = (dir.path / "empty.txt").string();
	std::string garbage = (dir.path / "garbage.txt").string();
	std::string version0 = (dir.path / "v0.txt").string();
	std::string versionHi = (dir.path / "v999.txt").string();
	std::string missing = (dir.path / "does_not_exist.txt").string();
	Fixtures();
};

Fixtures::Fixtures()
{
	using glob2test::writeFile;
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
	writeFile(this->valid, valid);

	writeFile(empty, "");

	writeFile(garbage, "this is not a campaign file at all }} { ;; \xff\xfe\x00 random");

	const std::string v0 =
		"versionMinor = 0;\n"
		"campaignName = \"ShouldNotLoad\";\n"
		"playerName = \"\";\n"
		"maps\n"
		"{\n"
		"\tmapNum = 0;\n"
		"}\n";
	writeFile(version0, v0);

	const std::string vHi =
		"versionMinor = 999;\n"
		"campaignName = \"FromTheFuture\";\n"
		"playerName = \"\";\n"
		"maps\n"
		"{\n"
		"\tmapNum = 0;\n"
		"}\n";
	writeFile(versionHi, vHi);
	// missing: deliberately not created.
}

void expectRejected(const char* tag, const std::string& path)
{
	Campaign c;
	CHECK_MESSAGE(!c.load(path), (std::string(tag) + ": load must fail"));
}

}  // namespace

TEST_SUITE("CampaignLoad")
{
	TEST_CASE("load distinguishes valid campaigns from missing or broken files")
	{
		// TextStream's parser logs to stderr on malformed input.
		glob2test::CapturedStderr quiet;
		glob2test::ToolkitScope toolkit;
		Fixtures fixtures;

		Campaign valid;
		REQUIRE(valid.load(fixtures.valid));
		CHECK_EQ(valid.getName(), "TestCampaign");
		CHECK_EQ(valid.getMapCount(), 2u);

		expectRejected("missing", fixtures.missing);
		expectRejected("empty", fixtures.empty);
		expectRejected("garbage", fixtures.garbage);
		expectRejected("version0", fixtures.version0);
		expectRejected("version+", fixtures.versionHi);
	}
}
