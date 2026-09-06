// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <cppunit/extensions/HelperMacros.h>

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
class FilenameStripTest : public CPPUNIT_NS::TestCase
{
	CPPUNIT_TEST_SUITE(FilenameStripTest);
		CPPUNIT_TEST(testStripPrefixMatches);
		CPPUNIT_TEST(testStripPrefixKeepsSubdirectory);
		CPPUNIT_TEST(testStripPrefixNoMatch);
		CPPUNIT_TEST(testStripPrefixShorterThanPrefix);
		CPPUNIT_TEST(testStripPrefixEmptyInputs);
		CPPUNIT_TEST(testStripSuffixMatches);
		CPPUNIT_TEST(testStripSuffixNoMatch);
		CPPUNIT_TEST(testStripSuffixShorterThanSuffix);
		CPPUNIT_TEST(testStripSuffixWholeString);
		CPPUNIT_TEST(testStripSuffixEmptyInputs);
		CPPUNIT_TEST(testWellFormedReplayNameRoundTrip);
	CPPUNIT_TEST_SUITE_END();

protected:
	void testStripPrefixMatches(void)
	{
		CPPUNIT_ASSERT_EQUAL(std::string("My_Game.replay"),
			Utilities::stripPrefix("replays/My_Game.replay", "replays/"));
	}

	void testStripPrefixKeepsSubdirectory(void)
	{
		// FileList can recurse into subdirectories; only the listing root
		// is stripped, the relative path below it must survive.
		CPPUNIT_ASSERT_EQUAL(std::string("old/My_Game.replay"),
			Utilities::stripPrefix("replays/old/My_Game.replay", "replays/"));
	}

	void testStripPrefixNoMatch(void)
	{
		CPPUNIT_ASSERT_EQUAL(std::string("games/foo.game"),
			Utilities::stripPrefix("games/foo.game", "replays/"));
	}

	void testStripPrefixShorterThanPrefix(void)
	{
		// The old erase(0, 8) truncated names shorter than the prefix.
		CPPUNIT_ASSERT_EQUAL(std::string("abc"),
			Utilities::stripPrefix("abc", "replays/"));
	}

	void testStripPrefixEmptyInputs(void)
	{
		CPPUNIT_ASSERT_EQUAL(std::string(""),
			Utilities::stripPrefix("", "replays/"));
		CPPUNIT_ASSERT_EQUAL(std::string("abc"),
			Utilities::stripPrefix("abc", ""));
	}

	void testStripSuffixMatches(void)
	{
		CPPUNIT_ASSERT_EQUAL(std::string("My_Game"),
			Utilities::stripSuffix("My_Game.replay", ".replay"));
	}

	void testStripSuffixNoMatch(void)
	{
		// The crashing case: a stray non-.replay file in replays/ must
		// pass through instead of throwing std::out_of_range.
		CPPUNIT_ASSERT_EQUAL(std::string("notes.txt"),
			Utilities::stripSuffix("notes.txt", ".replay"));
	}

	void testStripSuffixShorterThanSuffix(void)
	{
		CPPUNIT_ASSERT_EQUAL(std::string("ab"),
			Utilities::stripSuffix("ab", ".replay"));
	}

	void testStripSuffixWholeString(void)
	{
		CPPUNIT_ASSERT_EQUAL(std::string(""),
			Utilities::stripSuffix(".replay", ".replay"));
	}

	void testStripSuffixEmptyInputs(void)
	{
		CPPUNIT_ASSERT_EQUAL(std::string(""),
			Utilities::stripSuffix("", ".replay"));
		CPPUNIT_ASSERT_EQUAL(std::string("abc"),
			Utilities::stripSuffix("abc", ""));
	}

	void testWellFormedReplayNameRoundTrip(void)
	{
		// Parity with the old positional-erase behavior on well-formed
		// input: "replays/My_Game.replay" -> "My_Game" (the callbacks
		// then map '_' to ' ' themselves).
		CPPUNIT_ASSERT_EQUAL(std::string("My_Game"),
			Utilities::stripSuffix(
				Utilities::stripPrefix("replays/My_Game.replay", "replays/"),
				".replay"));
	}
};
CPPUNIT_TEST_SUITE_REGISTRATION(FilenameStripTest);
