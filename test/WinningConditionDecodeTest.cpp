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
// Reuses WinningConditionsTestStubs.cpp (MapHeader / SGSL stubs) and links
// libgag_server.a for BinaryStream + MemoryStreamBackend.

#include <cstdio>
#include <list>
#include <memory>
#include <SDL.h>
#include "BinaryStream.h"
#include "StreamBackend.h"
#include "WinningConditions.h"

using namespace GAGCore;

// SHA1 is wired in this project by direct .c-into-.cpp inclusion (see
// YOGServerPasswordRegistry.cpp); the .h has no extern "C" wrapper, so
// libgag_server.a's BinaryOutputStream references C++-mangled SHA1 names.
// Same trick as NetSendOrderDecodeTest.cpp to satisfy the linker.
#include "../gnupg/sha1.c"

namespace {

int failures = 0;

void check(bool ok, const char* what)
{
	std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok)
		++failures;
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

int main(int /*argc*/, char* /*argv*/[])
{
	testRoundTrip();
	testUnknownTypeTag();
	testTruncatedStream();
	std::printf(failures == 0 ? "ALL PASS\n" : "FAILURES: %d\n", failures);
	return failures == 0 ? 0 : 1;
}
