// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Regression harness for BH-066: WinningCondition::getWinningCondition
// returns a null shared_ptr on truncated input or an unrecognized type tag,
// and GameHeader::load / loadWithoutPlayerInfo used to push that null
// unchecked into winningConditions -- SIGSEGV on the next re-save or
// Team::checkWinConditions. Post-fix, both loaders go through
// WinningCondition::loadWinningConditions, which is exercised here:
//   1. round-trip of the default condition list -> true, types in order
//   2. unrecognized type tag mid-list -> false, no null in the output
//   3. size > 0 but stream truncated -> false, no null in the output
// Reuses the MapHeader / SGSL stubs in test/unit/stubs/ and links
// libgag_server.a for BinaryStream + MemoryStreamBackend.

#include "Glob2Test.h"
#include <cstdio>
#include <list>
#include <memory>
#include <SDL3/SDL.h>
#include "BinaryStream.h"
#include "StreamBackend.h"
#include "WinningConditions.h"

using namespace GAGCore;

namespace {


void check(bool ok, const char* what)
{
	CHECK_MESSAGE(ok, (what));
}

bool listHasNull(const std::list<std::shared_ptr<WinningCondition> >& l)
{
	for (const auto& wc : l)
		if (!wc)
			return true;
	return false;
}

// Serialize `conditions` in the exact section layout GameHeader::save uses.
void writeConditionList(OutputStream* stream, const std::list<std::shared_ptr<WinningCondition> >& conditions)
{
	stream->writeEnterSection("winningConditions");
	stream->writeUint32(conditions.size(), "size");
	int n = 0;
	for (const auto& wc : conditions)
	{
		stream->writeEnterSection(n);
		wc->encodeData(stream);
		stream->writeLeaveSection();
		n += 1;
	}
	stream->writeLeaveSection();
}

std::unique_ptr<BinaryInputStream> makeInputStream(const MemoryStreamBackend& written)
{
	// BinaryInputStream takes ownership of the backend.
	MemoryStreamBackend* copy = new MemoryStreamBackend(written);
	copy->seekFromStart(0);
	return std::make_unique<BinaryInputStream>(copy);
}

void testRoundTrip()
{
	MemoryStreamBackend* backend = new MemoryStreamBackend;
	BinaryOutputStream ostream(backend);
	const auto defaults = WinningCondition::getDefaultWinningConditions();
	writeConditionList(&ostream, defaults);

	auto istream = makeInputStream(*backend);
	std::list<std::shared_ptr<WinningCondition> > loaded;
	const bool ok = WinningCondition::loadWinningConditions(istream.get(), 100, loaded);

	check(ok, "roundTrip: loadWinningConditions returns true");
	check(loaded.size() == defaults.size(), "roundTrip: list size preserved");
	check(!listHasNull(loaded), "roundTrip: no null entries");
	auto a = defaults.begin();
	auto b = loaded.begin();
	bool typesMatch = true;
	for (; a != defaults.end() && b != loaded.end(); ++a, ++b)
		if ((*a)->getType() != (*b)->getType())
			typesMatch = false;
	check(typesMatch, "roundTrip: types preserved in order");
}

void testSuddenDeathRoundTrip()
{
	MemoryStreamBackend* backend = new MemoryStreamBackend;
	BinaryOutputStream ostream(backend);
	WinningConditionSuddenDeath original;
	original.endStepTick = 12345;
	std::list<std::shared_ptr<WinningCondition> > conditions;
	conditions.push_back(std::make_shared<WinningConditionSuddenDeath>(original));
	writeConditionList(&ostream, conditions);

	auto istream = makeInputStream(*backend);
	std::list<std::shared_ptr<WinningCondition> > loaded;
	const bool ok = WinningCondition::loadWinningConditions(istream.get(), 100, loaded);

	check(ok, "suddenDeathRoundTrip: loadWinningConditions returns true");
	const bool typeOk = loaded.size() == 1 && loaded.front()->getType() == WCSuddenDeath;
	check(typeOk, "suddenDeathRoundTrip: type preserved");
	if (typeOk)
	{
		auto& decoded = static_cast<WinningConditionSuddenDeath&>(*loaded.front());
		check(decoded.endStepTick == 12345, "suddenDeathRoundTrip: endStepTick preserved");
	}
}

void testUnknownTypeTag()
{
	MemoryStreamBackend* backend = new MemoryStreamBackend;
	BinaryOutputStream ostream(backend);
	ostream.writeEnterSection("winningConditions");
	ostream.writeUint32(2, "size");
	// Entry 0: a valid condition.
	ostream.writeEnterSection(0);
	WinningConditionDeath().encodeData(&ostream);
	ostream.writeLeaveSection();
	// Entry 1: a tag no WinningConditionType uses.
	ostream.writeEnterSection(1);
	ostream.writeUint8(0xC7, "type");
	ostream.writeLeaveSection();
	ostream.writeLeaveSection();

	auto istream = makeInputStream(*backend);
	std::list<std::shared_ptr<WinningCondition> > loaded;
	const bool ok = WinningCondition::loadWinningConditions(istream.get(), 100, loaded);

	check(!ok, "unknownTag: loadWinningConditions returns false");
	check(!listHasNull(loaded), "unknownTag: no null entries");
	check(loaded.size() == 1 && loaded.front()->getType() == WCDeath,
	      "unknownTag: entries before the bad tag survive");
}

void testTruncatedStream()
{
	MemoryStreamBackend* backend = new MemoryStreamBackend;
	BinaryOutputStream ostream(backend);
	// Claims 3 conditions but the stream ends right after the size field.
	ostream.writeEnterSection("winningConditions");
	ostream.writeUint32(3, "size");

	auto istream = makeInputStream(*backend);
	std::list<std::shared_ptr<WinningCondition> > loaded;
	const bool ok = WinningCondition::loadWinningConditions(istream.get(), 100, loaded);

	check(!ok, "truncated: loadWinningConditions returns false");
	check(!listHasNull(loaded), "truncated: no null entries");
	check(loaded.empty(), "truncated: nothing decoded");
}

}  // namespace

TEST_SUITE("WinningConditionDecode")
{
	TEST_CASE("RoundTrip") { testRoundTrip(); }
	TEST_CASE("SuddenDeathRoundTrip") { testSuddenDeathRoundTrip(); }
	TEST_CASE("UnknownTypeTag") { testUnknownTypeTag(); }
	TEST_CASE("TruncatedStream") { testTruncatedStream(); }
}
