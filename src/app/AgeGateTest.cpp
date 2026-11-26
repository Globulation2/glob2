// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Age bands on the client (src/app/AgeGate.h): platform signals and the player's
// answer, lining the device up with the online account, whether chat is allowed,
// and the band's round trip through the settings file.

#include "HeadlessGlobals.h"
#include "AgeGate.h"
#include "Settings.h"
#include <FileManager.h>
#include <Toolkit.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace AgeGate;

namespace
{
std::filesystem::path profileFile(const std::string &name)
{
	return std::filesystem::path(GAGCore::Toolkit::getFileManager()->getDir(0)) / name;
}

// Restores the process-wide store and match chat state.
struct ScopedState
{
	Store previous = installedStore();
	~ScopedState()
	{
		setStore(previous);
		setMatchChatOn(true);
	}
};
}

TEST_SUITE("AgeGate")
{
	TEST_CASE("names round-trip and unknown names are refused")
	{
		for (Band b : {Band::Unknown, Band::Under13, Band::ThirteenPlus})
			CHECK(parseBand(bandName(b)) == b);
		for (Source s : {Source::None, Source::Self, Source::Apple, Source::GooglePlay, Source::Account})
			CHECK(parseSource(sourceName(s)) == s);
		CHECK_FALSE(parseBand("adult").has_value());
		CHECK_FALSE(parseSource("moderator").has_value());
		CHECK_FALSE(socialFeatures(Band::Unknown));
		CHECK_FALSE(socialFeatures(Band::Under13));
		CHECK(socialFeatures(Band::ThirteenPlus));
	}

	TEST_CASE("a platform signal that reports an age decides, but never lifts a self-declared under 13")
	{
		const Local unknown;
		CHECK(applySignal(unknown, Signal::Unavailable, Source::Apple).band == Band::Unknown);
		CHECK(applySignal(unknown, Signal::Declined, Source::Apple).band == Band::Unknown);
		const Local child = applySignal(unknown, Signal::Under13, Source::Apple);
		CHECK(child.band == Band::Under13);
		CHECK(child.source == Source::Apple);
		// The same platform reports the child turned 13.
		CHECK(applySignal(child, Signal::ThirteenPlus, Source::Apple).band == Band::ThirteenPlus);
		// A self-declared or account under 13 stays.
		CHECK(applySignal({Band::Under13, Source::Self}, Signal::ThirteenPlus, Source::Apple).band == Band::Under13);
		CHECK(applySignal({Band::Under13, Source::Account}, Signal::ThirteenPlus, Source::Apple).band == Band::Under13);
		// A platform under 13 replaces a self-declared 13 plus.
		CHECK(applySignal({Band::ThirteenPlus, Source::Self}, Signal::Under13, Source::GooglePlay).band == Band::Under13);
		// Declining keeps what was there.
		CHECK(applySignal({Band::ThirteenPlus, Source::Self}, Signal::Declined, Source::Apple).band == Band::ThirteenPlus);
	}

	TEST_CASE("the player's answer is kept when it is under 13")
	{
		CHECK(applyAnswer({}, Band::ThirteenPlus).band == Band::ThirteenPlus);
		CHECK(applyAnswer({}, Band::ThirteenPlus).source == Source::Self);
		const Local child = applyAnswer({}, Band::Under13);
		CHECK(child.band == Band::Under13);
		CHECK(applyAnswer(child, Band::ThirteenPlus).band == Band::Under13);
		CHECK(applyAnswer({}, Band::Unknown).band == Band::Unknown);
		CHECK(applyAnswer({Band::ThirteenPlus, Source::Self}, Band::Under13).band == Band::Under13);
		CHECK(shouldAsk({}));
		CHECK_FALSE(shouldAsk(child));
	}

	TEST_CASE("the device and the account line up, the more restrictive or better informed side winning")
	{
		// Nothing known anywhere: nothing to do.
		auto r = reconcile({}, Band::Unknown);
		CHECK_FALSE(r.push);
		CHECK_FALSE(r.adopt);
		// The account knows (another device, the website, a moderator): adopt it.
		r = reconcile({}, Band::Under13);
		CHECK(r.adopt == Band::Under13);
		CHECK_FALSE(r.push);
		// The device knows and the account does not: push, naming the platform.
		r = reconcile({Band::ThirteenPlus, Source::Apple}, Band::Unknown);
		CHECK(r.push == Band::ThirteenPlus);
		CHECK(r.pushSource == Source::Apple);
		r = reconcile({Band::Under13, Source::Account}, Band::Unknown);
		CHECK(r.push == Band::Under13);
		CHECK(r.pushSource == Source::Self);
		// Device under 13, account 13 plus: the account follows the device.
		r = reconcile({Band::Under13, Source::Self}, Band::ThirteenPlus);
		CHECK(r.push == Band::Under13);
		// Device self-declared 13 plus, account under 13: the account wins.
		r = reconcile({Band::ThirteenPlus, Source::Self}, Band::Under13);
		CHECK(r.adopt == Band::Under13);
		CHECK_FALSE(r.push);
		// A platform 13 plus asks the server, which lifts only its own under 13.
		r = reconcile({Band::ThirteenPlus, Source::GooglePlay}, Band::Under13);
		CHECK(r.push == Band::ThirteenPlus);
		CHECK(r.pushSource == Source::GooglePlay);
		// Agreement: nothing.
		r = reconcile({Band::ThirteenPlus, Source::Self}, Band::ThirteenPlus);
		CHECK_FALSE(r.push);
		CHECK_FALSE(r.adopt);
		// A moderator lifted the account (a parent's request): the device follows
		// instead of pushing its self-declared under 13 back and locking it again.
		r = reconcile({Band::Under13, Source::Self}, Band::ThirteenPlus, true);
		CHECK(r.adopt == Band::ThirteenPlus);
		CHECK_FALSE(r.push);
		// A band the device took from the account follows the account's changes.
		r = reconcile({Band::Under13, Source::Account}, Band::ThirteenPlus);
		CHECK(r.adopt == Band::ThirteenPlus);
		CHECK_FALSE(r.push);
	}

	TEST_CASE("a rematch's new match keeps chat off when the old match ends after it starts")
	{
		ScopedState scope;
		const auto first = beginMatchChat(true);
		const auto second = beginMatchChat(false); // created before the old match is destroyed
		endMatchChat(first);
		CHECK_FALSE(matchChatOn());
		endMatchChat(second);
		CHECK(matchChatOn());
	}

	TEST_CASE("chat needs a 13 plus band and a match that has not turned chat off")
	{
		ScopedState scope;
		// The installed store reads the settings of the global container; this case
		// replaces it with its own.
		CHECK_FALSE(chatAllowed({}, true));
		CHECK_FALSE(chatAllowed({Band::Under13, Source::Self}, true));
		CHECK(chatAllowed({Band::ThirteenPlus, Source::Self}, true));
		CHECK_FALSE(chatAllowed({Band::ThirteenPlus, Source::Self}, false));

		// Without a store (tools, harnesses) the band is unknown: no chat.
		setStore({});
		CHECK_FALSE(chatAllowedNow());
		Local saved;
		setStore({[&] { return saved; }, [&](const Local &v) { saved = v; return true; }});
		CHECK(store({Band::ThirteenPlus, Source::Self}));
		CHECK(chatAllowedNow());
		setMatchChatOn(false);
		CHECK_FALSE(chatAllowedNow());
		setMatchChatOn(true);
		CHECK(chatAllowedNow());
	}

	TEST_CASE("a build without a platform age API reports unavailable at once")
	{
		std::optional<Signal> got;
		requestPlatformSignal([&](Signal s) { got = s; });
		REQUIRE(got.has_value());
		CHECK(*got == Signal::Unavailable);
	}

	TEST_CASE("the settings file keeps the band and its source, and ignores unknown values")
	{
		glob2test::HeadlessGlobals globals;
		const std::string name = "age-gate-test.txt";
		{
			Settings settings;
			settings.ageBand = "under_13";
			settings.ageBandSource = "apple";
			REQUIRE(settings.save(name));
			Settings loaded;
			loaded.load(name);
			CHECK(loaded.ageBand == "under_13");
			CHECK(loaded.ageBandSource == "apple");
		}
		{
			std::ofstream out(profileFile(name));
			out << "ageBand=adult\nageBandSource=self\n";
		}
		Settings odd;
		odd.load(name);
		CHECK(odd.ageBand == "unknown");
		CHECK(odd.ageBandSource.empty());
		// Profiles from before age bands load as unknown: the question is asked.
		{
			std::ofstream out(profileFile(name));
			out << "username=old\n";
		}
		Settings old;
		old.load(name);
		CHECK(old.ageBand == "unknown");
		std::filesystem::remove(profileFile(name));
	}
}
