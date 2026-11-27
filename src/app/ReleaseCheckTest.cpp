// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// The release check's step list and result (src/app/ReleaseCheck.h); the
// scripted run itself is tools/release/release_check.py.

#include "Glob2Test.h"
#include "ReleaseCheck.h"

TEST_CASE("ReleaseCheck.parseSteps: all, 1 and lists")
{
	CHECK(ReleaseCheck::parseSteps("all") == ReleaseCheck::allSteps());
	CHECK(ReleaseCheck::parseSteps("1") == ReleaseCheck::allSteps());
	CHECK(ReleaseCheck::parseSteps("menu,,victory,") == std::vector<std::string>{"menu", "victory"});
	CHECK(ReleaseCheck::parseSteps("bogus") == std::vector<std::string>{"bogus"});
	CHECK(ReleaseCheck::allSteps().back() == "victory");
}

TEST_CASE("ReleaseCheck.result: unset until a check finishes")
{
	CHECK_FALSE(ReleaseCheck::result().has_value());
	ReleaseCheck::setResult(1);
	CHECK(ReleaseCheck::result() == 1);
}
