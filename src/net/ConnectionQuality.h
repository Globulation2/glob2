// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors
#pragma once

// The connection-quality table of the game (docs/multiplayer/connection-quality.md).
//
// The source of truth is platform/packages/protocol/src/connectionQuality.ts, which the
// match page and the API use; its generator writes the table to
// platform/packages/protocol/fixtures/connection-quality.json (vendored as
// test/fixtures/protocol/connection-quality.json), and src/net/ConnectionQualityTest.cpp
// checks this copy against it. Change both together.
//
// Three quantities, all in milliseconds:
//   Ping    round trip between a player and the relay (before a match: a region probe,
//           an estimate)
//   Delay   your input delay, from click to effect for everyone
//   Behind  how far a player's game runs behind the match clock
//
// Presentation only: nothing simulated, networked or saved reads it.

#include <cstdio>
#include <string>

namespace ConnectionQuality
{
enum class Metric
{
	Ping,
	Delay,
	Behind,
};

enum class Rating
{
	Good,
	Fair,
	Poor,
};

struct Limits
{
	const char *id;  ///< "ping", "delay", "behind" (the JSON key)
	bool seconds;    ///< written in seconds ("1.4 s") rather than "ms"
	int fairMs;      ///< at or above: fair
	int poorMs;      ///< at or above: poor
};

constexpr Limits limits(Metric metric)
{
	switch (metric)
	{
	case Metric::Ping:
		return {"ping", false, 150, 300};
	case Metric::Delay:
		return {"delay", false, 200, 400};
	default:
		return {"behind", true, 1000, 2000};
	}
}

constexpr Rating rate(Metric metric, int valueMs)
{
	const Limits l = limits(metric);
	return valueMs >= l.poorMs ? Rating::Poor : valueMs >= l.fairMs ? Rating::Fair : Rating::Good;
}

constexpr Rating worst(Rating a, Rating b)
{
	return int(a) > int(b) ? a : b;
}

/// "good", "fair", "poor" (the JSON values); the words players read come from the
/// string table ("[conn good]" and so on).
constexpr const char *id(Rating rating)
{
	return rating == Rating::Good ? "good" : rating == Rating::Fair ? "fair" : "poor";
}

/// The string-table key of a rating's word ("Good", "Fair", "Poor").
constexpr const char *wordKey(Rating rating)
{
	return rating == Rating::Good ? "[conn good]" : rating == Rating::Fair ? "[conn fair]" : "[conn poor]";
}

// Game-only thresholds of the in-match HUD, derived from the table above where a
// metric covers them, so the HUD and the match page never disagree about "behind".
/// Jitter above this marks your own Delay as unstable (the HUD's "varies" note).
inline constexpr int UNSTABLE_JITTER_MS = 60;
/// Behind by more than this: the HUD shows the catching-up card (a real
/// fast-forward); less closes by itself.
constexpr int catchUpCardMs()
{
	return limits(Metric::Behind).poorMs;
}
/// Buffered (received, not yet run) by more than this: the "catching up" line.
constexpr int catchUpLineMs()
{
	return limits(Metric::Behind).fairMs;
}

/// The value with its unit, as every surface writes it: "42 ms", "1.4 s", "12 s".
inline std::string format(Metric metric, int valueMs)
{
	if (!limits(metric).seconds)
		return std::to_string(valueMs) + " ms";
	char buffer[32];
	if (valueMs < 10000)
		std::snprintf(buffer, sizeof buffer, "%.1f s", valueMs / 1000.0);
	else
		std::snprintf(buffer, sizeof buffer, "%d s", (valueMs + 500) / 1000);
	return buffer;
}

/// "Ping 42 ms · Good": a labelled value with its rating, given the translated label
/// and the translator of the rating word (`tr(wordKey(...))`).
template <typename Translate>
std::string labelled(const std::string &label, Metric metric, int valueMs, Translate tr)
{
	return label + " " + format(metric, valueMs) + " \xC2\xB7 " + std::string(tr(wordKey(rate(metric, valueMs))));
}
} // namespace ConnectionQuality
