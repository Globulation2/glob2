// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Glob2Test.h"

#include <string>

#include "Utilities.h"

// Regression tests for Utilities::stripPrefix / Utilities::stripSuffix, the
// helpers behind the LoadSaveScreen filename-to-display-name callbacks
// (replayFilenameToName in EndGameScreen.cpp, filenameToName in
// ScriptEditorScreen.cpp). Those callbacks used to do positional
// erase(0, 8) + erase(find(".ext")): a stray file in the directory whose
// name lacked the extension made find() return npos and erase(npos) throw
// std::out_of_range, crashing the dialog. The helpers must strip only when
// the affix actually matches and pass malformed names through unchanged.
class FilenameStripTest
{
public:

protected:
	void testStripPrefixMatches(void)
	{
		CHECK_EQ(std::string("My_Game.replay"), Utilities::stripPrefix("replays/My_Game.replay", "replays/"));
	}

	void testStripPrefixKeepsSubdirectory(void)
	{
		// FileList can recurse into subdirectories; only the listing root
		// is stripped, the relative path below it must survive.
		CHECK_EQ(std::string("old/My_Game.replay"), Utilities::stripPrefix("replays/old/My_Game.replay", "replays/"));
	}

	void testStripPrefixNoMatch(void)
	{
		CHECK_EQ(std::string("games/foo.game"), Utilities::stripPrefix("games/foo.game", "replays/"));
	}

	void testStripPrefixShorterThanPrefix(void)
	{
		// The old erase(0, 8) truncated names shorter than the prefix.
		CHECK_EQ(std::string("abc"), Utilities::stripPrefix("abc", "replays/"));
	}

	void testStripPrefixEmptyInputs(void)
	{
		CHECK_EQ(std::string(""), Utilities::stripPrefix("", "replays/"));
		CHECK_EQ(std::string("abc"), Utilities::stripPrefix("abc", ""));
	}

	void testStripSuffixMatches(void)
	{
		CHECK_EQ(std::string("My_Game"), Utilities::stripSuffix("My_Game.replay", ".replay"));
	}

	void testStripSuffixNoMatch(void)
	{
		// The crashing case: a stray non-.replay file in replays/ must
		// pass through instead of throwing std::out_of_range.
		CHECK_EQ(std::string("notes.txt"), Utilities::stripSuffix("notes.txt", ".replay"));
	}

	void testStripSuffixShorterThanSuffix(void)
	{
		CHECK_EQ(std::string("ab"), Utilities::stripSuffix("ab", ".replay"));
	}

	void testStripSuffixWholeString(void)
	{
		CHECK_EQ(std::string(""), Utilities::stripSuffix(".replay", ".replay"));
	}

	void testStripSuffixEmptyInputs(void)
	{
		CHECK_EQ(std::string(""), Utilities::stripSuffix("", ".replay"));
		CHECK_EQ(std::string("abc"), Utilities::stripSuffix("abc", ""));
	}

	void testWellFormedReplayNameRoundTrip(void)
	{
		// Parity with the old positional-erase behavior on well-formed
		// input: "replays/My_Game.replay" -> "My_Game" (the callbacks
		// then map '_' to ' ' themselves).
		CHECK_EQ(std::string("My_Game"), Utilities::stripSuffix(
				Utilities::stripPrefix("replays/My_Game.replay", "replays/"),
				".replay"));
	}
};
TEST_SUITE("FilenameStrip")
{
	TEST_CASE_FIXTURE(FilenameStripTest, "StripPrefixMatches") { testStripPrefixMatches(); }
	TEST_CASE_FIXTURE(FilenameStripTest, "StripPrefixKeepsSubdirectory") { testStripPrefixKeepsSubdirectory(); }
	TEST_CASE_FIXTURE(FilenameStripTest, "StripPrefixNoMatch") { testStripPrefixNoMatch(); }
	TEST_CASE_FIXTURE(FilenameStripTest, "StripPrefixShorterThanPrefix") { testStripPrefixShorterThanPrefix(); }
	TEST_CASE_FIXTURE(FilenameStripTest, "StripPrefixEmptyInputs") { testStripPrefixEmptyInputs(); }
	TEST_CASE_FIXTURE(FilenameStripTest, "StripSuffixMatches") { testStripSuffixMatches(); }
	TEST_CASE_FIXTURE(FilenameStripTest, "StripSuffixNoMatch") { testStripSuffixNoMatch(); }
	TEST_CASE_FIXTURE(FilenameStripTest, "StripSuffixShorterThanSuffix") { testStripSuffixShorterThanSuffix(); }
	TEST_CASE_FIXTURE(FilenameStripTest, "StripSuffixWholeString") { testStripSuffixWholeString(); }
	TEST_CASE_FIXTURE(FilenameStripTest, "StripSuffixEmptyInputs") { testStripSuffixEmptyInputs(); }
	TEST_CASE_FIXTURE(FilenameStripTest, "WellFormedReplayNameRoundTrip") { testWellFormedReplayNameRoundTrip(); }
}
