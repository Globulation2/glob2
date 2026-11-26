// SPDX-License-Identifier: GPL-3.0-or-later
#include "AgeGate.h"

#include <atomic>
#include <mutex>
#include <utility>
#include <vector>

// The platform age request. The App Store builds for iOS/iPadOS and macOS link
// platforms/apple/AgeSignal.swift, and Android links platforms/mobile/android/AgeSignal.cpp
// (Play Age Signals in the Google Play build), which define this symbol; it asks Declared Age
// Range with an age gate of 13 and calls back exactly once with 0 unavailable,
// 1 declined, 2 under 13, 3 aged 13 or over, on any thread. Everywhere else the
// weak default below reports "unavailable".
extern "C" void glob2_request_age_range(void (*done)(int));

#if defined(__APPLE__) || defined(__ANDROID__)
extern "C" __attribute__((weak)) void glob2_request_age_range(void (*done)(int))
{
	done(0);
}
#else
extern "C" void glob2_request_age_range(void (*done)(int))
{
	done(0);
}
#endif

namespace AgeGate
{
const char *bandName(Band band)
{
	switch (band)
	{
	case Band::Under13:
		return "under_13";
	case Band::ThirteenPlus:
		return "13_plus";
	case Band::Unknown:
		break;
	}
	return "unknown";
}

std::optional<Band> parseBand(std::string_view name)
{
	if (name == "unknown")
		return Band::Unknown;
	if (name == "under_13")
		return Band::Under13;
	if (name == "13_plus")
		return Band::ThirteenPlus;
	return std::nullopt;
}

const char *sourceName(Source source)
{
	switch (source)
	{
	case Source::Self:
		return "self";
	case Source::Apple:
		return "apple";
	case Source::GooglePlay:
		return "google_play";
	case Source::Account:
		return "account";
	case Source::None:
		break;
	}
	return "";
}

std::optional<Source> parseSource(std::string_view name)
{
	if (name.empty())
		return Source::None;
	for (Source s : {Source::Self, Source::Apple, Source::GooglePlay, Source::Account})
		if (name == sourceName(s))
			return s;
	return std::nullopt;
}

Local applySignal(Local current, Signal signal, Source platform)
{
	if (signal == Signal::Unavailable || signal == Signal::Declined)
		return current;
	if (signal == Signal::Under13)
		return {Band::Under13, platform};
	// A platform reporting 13 or over lifts only an Under13 the platform set.
	if (current.band == Band::Under13 && current.source != platform)
		return current;
	return {Band::ThirteenPlus, platform};
}

Local applyAnswer(Local current, Band answer)
{
	if (answer == Band::Unknown)
		return current;
	if (current.band == Band::Under13 && answer != Band::Under13)
		return current;
	// A platform signal already answered; the question is not shown then, but
	// keep it authoritative if it is.
	if (current.band != Band::Unknown && (current.source == Source::Apple || current.source == Source::GooglePlay) &&
		answer != Band::Under13)
		return current;
	return {answer, Source::Self};
}

Reconcile reconcile(const Local &local, Band accountBand, bool accountByModerator)
{
	Reconcile r;
	if (accountBand != Band::Unknown && local.band != accountBand &&
		(accountByModerator || local.source == Source::Account))
	{
		r.adopt = accountBand;
		return r;
	}
	const auto pushAs = [&](Band band) {
		r.push = band;
		r.pushSource = local.source == Source::Apple || local.source == Source::GooglePlay ? local.source : Source::Self;
	};
	if (local.band == accountBand)
		return r;
	if (local.band == Band::Unknown)
	{
		r.adopt = accountBand;
		return r;
	}
	if (accountBand == Band::Unknown)
	{
		pushAs(local.band);
		return r;
	}
	if (local.band == Band::Under13)
	{
		// The device is more restrictive: the account follows (always allowed).
		pushAs(Band::Under13);
		return r;
	}
	// Local 13 plus, account under 13. A platform signal may lift an account
	// band the same platform set (the server decides); otherwise the account wins.
	if (local.source == Source::Apple || local.source == Source::GooglePlay)
		pushAs(Band::ThirteenPlus);
	else
		r.adopt = Band::Under13;
	return r;
}

namespace
{
std::mutex stateMutex;
Store installed;
std::atomic<bool> chatOn{true};
std::mutex matchMutex;
std::uint64_t matchToken = 0;

// A platform answer arrives on any thread; it is delivered from the game thread.
std::mutex signalMutex;
std::vector<std::function<void(Signal)>> waiting;
std::optional<Signal> arrived;
bool requesting = false;

Signal signalOf(int code)
{
	switch (code)
	{
	case 1:
		return Signal::Declined;
	case 2:
		return Signal::Under13;
	case 3:
		return Signal::ThirteenPlus;
	default:
		return Signal::Unavailable;
	}
}

void platformAnswered(int code)
{
	std::lock_guard<std::mutex> lock(signalMutex);
	arrived = signalOf(code);
}
}

void setStore(Store value)
{
	std::lock_guard<std::mutex> lock(stateMutex);
	installed = std::move(value);
}

Store installedStore()
{
	std::lock_guard<std::mutex> lock(stateMutex);
	return installed;
}

Local local()
{
	Store copy;
	{
		std::lock_guard<std::mutex> lock(stateMutex);
		copy = installed;
	}
	return copy.load ? copy.load() : Local{};
}

bool store(const Local &value)
{
	Store copy;
	{
		std::lock_guard<std::mutex> lock(stateMutex);
		copy = installed;
	}
	return copy.save && copy.save(value);
}

std::uint64_t beginMatchChat(bool on)
{
	std::lock_guard<std::mutex> lock(matchMutex);
	chatOn = on;
	return ++matchToken;
}

void endMatchChat(std::uint64_t token)
{
	std::lock_guard<std::mutex> lock(matchMutex);
	if (token == matchToken)
		chatOn = true;
}

void setMatchChatOn(bool on)
{
	std::lock_guard<std::mutex> lock(matchMutex);
	++matchToken;
	chatOn = on;
}

bool matchChatOn()
{
	return chatOn;
}

bool chatAllowedNow()
{
	return chatAllowed(local(), matchChatOn());
}

Source platformSource()
{
#if defined(__APPLE__)
	return Source::Apple;
#elif defined(__ANDROID__)
	return Source::GooglePlay;
#else
	return Source::None;
#endif
}

void requestPlatformSignal(std::function<void(Signal)> done)
{
	bool start = false;
	{
		std::lock_guard<std::mutex> lock(signalMutex);
		waiting.push_back(std::move(done));
		if (!requesting)
			start = requesting = true;
	}
	if (start)
		glob2_request_age_range(&platformAnswered);
	pollPlatformSignal();
}

void pollPlatformSignal()
{
	std::vector<std::function<void(Signal)>> ready;
	Signal signal = Signal::Unavailable;
	{
		std::lock_guard<std::mutex> lock(signalMutex);
		if (!arrived)
			return;
		signal = *arrived;
		arrived.reset();
		requesting = false;
		ready.swap(waiting);
	}
	for (auto &callback : ready)
		if (callback)
			callback(signal);
}
}
