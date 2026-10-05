// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// Experimental gameplay features: off by default, switched on per player under
// Settings > Experiments, and baked into every new game's GameHeader so a save,
// replay or multiplayer peer plays exactly the set the game was started with.
//
// Adding one: append an ExperimentId before Count, add its definition to the
// table in ExperimentalFeatures.cpp with a stable kebab-case key, gate the
// simulation on game->gameHeader.hasExperiment(id), add the two translation
// keys ([experiment <key>] and [experiment <key> help]) to every catalog, and
// cover both sides of the gate in a test. See docs/features/experimental-features.md.
//
// Keys, not bit positions, are what saves and the network carry, so retiring
// an experiment never re-interprets an old file: an unknown key is dropped on
// load.

#include "Types.h"

#include <bitset>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace GAGCore
{
	class InputStream;
	class OutputStream;
}

enum class ExperimentId : Uint8
{
	// Free warriors spread between painted guard areas by crowding instead of
	// all taking the nearest one.
	GuardAreaBalancing = 0,
	// A fourth painted area: a harvest inside it draws from the ripest tile of
	// the connected field and keeps one grain on every tile as seed.
	FarmAreas = 1,
	IceTerrain = 2,
	RoadTerrain = 3,
	Count
};

struct ExperimentDefinition
{
	ExperimentId id;
	// Stable identifier used in preferences, saves, replays and network messages.
	const char *key;
	// English source text; the interface reads "[experiment <key>]" and
	// "[experiment <key> help]" from the string table instead.
	const char *label;
	const char *help;
};

// Every experiment this build knows, in ExperimentId order.
const std::vector<ExperimentDefinition> &experimentDefinitions();
const ExperimentDefinition &experimentDefinition(ExperimentId id);
std::optional<ExperimentId> parseExperimentKey(const std::string &key);

// The set of experiments a player enabled or a game carries. A value type: copy
// it, compare it, serialise it.
class ExperimentSet
{
public:
	static constexpr std::size_t COUNT = static_cast<std::size_t>(ExperimentId::Count);
	// Upper bound on the entries a stream may carry, so a corrupt count cannot
	// spin the loader.
	static constexpr Uint32 MAX_STORED = 64;

	bool has(ExperimentId id) const { return bits.test(static_cast<std::size_t>(id)); }
	void set(ExperimentId id, bool on = true) { bits.set(static_cast<std::size_t>(id), on); }
	void clear() { bits.reset(); }
	bool empty() const { return bits.none(); }
	std::size_t size() const { return bits.count(); }
	bool operator==(const ExperimentSet &other) const { return bits == other.bits; }
	bool operator!=(const ExperimentSet &other) const { return !(*this == other); }

	// Keys of the enabled experiments, in ExperimentId order.
	std::vector<std::string> keys() const;
	// Comma-separated keys, the preferences form; empty when nothing is enabled.
	std::string toText() const;
	// Parse keys (comma-separated, or a list). Unknown keys are ignored; when
	// `unknown` is given they are appended to it so the caller can report them.
	static ExperimentSet fromText(const std::string &text, std::vector<std::string> *unknown = nullptr);
	static ExperimentSet fromKeys(const std::vector<std::string> &keys, std::vector<std::string> *unknown = nullptr);

	// Save and load the set as its own stream section. load() clears the set,
	// reads nothing from streams older than FILE_FORMAT_VERSION_EXPERIMENTS,
	// drops unknown keys with one line on stderr each, and returns false only
	// when the section itself is malformed.
	void save(GAGCore::OutputStream *stream) const;
	bool load(GAGCore::InputStream *stream, Sint32 versionMinor, bool rejectUnknown = false);

private:
	std::bitset<COUNT> bits;
};

// The enabled experiments' translated labels, comma-separated, for the lobby
// footer and the multiplayer options screen.
std::string experimentLabelList(const ExperimentSet &set);
