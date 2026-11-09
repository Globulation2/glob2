// SPDX-License-Identifier: GPL-3.0-or-later
// The deep checksum identity (Game::checkSum) across save/reload: the map-header word
// covers simulation inputs, not the file format version a save rewrites.
#include "HeadlessGlobals.h"
#include "Continuation.h"
#include "Engine.h"
#include "GameGUI.h"
#include "Version.h"

TEST_SUITE("ChecksumScheme")
{
	TEST_CASE("only the frozen legacy layout mixes the format version into the map-header word")
	{
		MapHeader older, newer;
		older.versionMinor = 81;
		newer.versionMinor = VERSION_MINOR;
		older.setNumberOfTeams(3);
		newer.setNumberOfTeams(3);
		CHECK(older.checkSum() == newer.checkSum());
		CHECK(older.checkSum(ChecksumScheme::Current) == newer.checkSum(ChecksumScheme::Current));
		CHECK(older.checkSum(ChecksumScheme::Legacy152) != newer.checkSum(ChecksumScheme::Legacy152));
		MapHeader fewer = newer;
		fewer.setNumberOfTeams(2);
		CHECK(fewer.checkSum() != newer.checkSum());
	}

	TEST_CASE("a save/reload continuation ends with the uninterrupted run's total checksum")
	{
		glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.loadStrings = true});
		GameGUI source;
		GameHeader header;
		header.setNumberOfPlayers(1);
		header.setRandomSeed(123456);
		header.getBasePlayer(0) = BasePlayer(0, "Test", 0, BasePlayer::P_LOCAL);
		auto mapHeader = Engine::loadMapHeader("maps/balanced.map");
		REQUIRE(source.loadFromHeaders(mapHeader, header, true, true));
		Game& game = source.game;
		for (int tick = 0; tick < 64; ++tick)
			glob2test::stepEngine(game);
		// The started game keeps the map file's old format version; the reload
		// carries the version Game::save writes. Only the format differs.
		REQUIRE(game.mapHeader.getVersionMinor() < VERSION_MINOR);
		const auto total = [](Game& g) { return g.checkSum(nullptr, nullptr, nullptr, true); };
		const auto routine = [](Game& g) { return g.checkSum(); };
		const auto result = glob2test::expectContinuation(game, {.ticks = 128, .name = "checksum scheme continuation",
			.afterLoad = [](Game&, Game& loaded) { CHECK(loaded.mapHeader.getVersionMinor() == VERSION_MINOR); },
			.each = [&](Game& reference, Game& loaded, int tick)
			{
				CAPTURE(tick);
				CHECK(total(reference) == total(loaded));
				CHECK(routine(reference) == routine(loaded));
			}});
		REQUIRE(result.matched);
		CHECK(total(game) == total(result.loaded->game));
	}
}
