// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// Experimental gameplay features: off by default, switched on per player under
// Settings > Experiments, and baked into every new game's GameHeader so a save,
// replay or multiplayer peer plays exactly the set the game was started with.
//
// Built-in engine experiments retain fixed bits. Building catalogs declare
// additional stable keys and English labels/help without extending the enum.
// Register installed catalog definitions before loading settings; embedded
// catalogs supply their own allowed keys when reading a game's set, without
// changing the process registry. See docs/features/experimental-features.md.
//
// Keys, not bit positions, are what saves and the network carry, so retiring
// an experiment never re-interprets an old file: an unknown key is dropped on
// load.

#include "Types.h"

#include <bitset>
#include <cstddef>
#include <optional>
#include <set>
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
	TrailTerrain = 3, // Legacy serialized key: road-terrain.
	MarketsV2 = 4,
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

// Built-in engine experiments only, in ExperimentId order. Catalog experiments
// deliberately have no enum ID; enumerate registeredExperimentDefinitions for UI.
const std::vector<ExperimentDefinition> &experimentDefinitions();
const ExperimentDefinition &experimentDefinition(ExperimentId id);
std::optional<ExperimentId> parseExperimentKey(const std::string &key);

struct CatalogExperimentDefinition
{
	std::string key, label, help;
	bool operator==(const CatalogExperimentDefinition &) const = default;
};

// Startup-only registry: installation is transactional, sorted by key and
// immutable afterward. Reinstalling identical definitions is harmless (fixtures
// can initialize multiple GlobalContainers); changing them is rejected.
class CatalogExperimentRegistry
{
public:
	void install(const std::vector<CatalogExperimentDefinition> &definitions);
	const std::vector<CatalogExperimentDefinition> &definitions() const { return entries; }
private:
	bool installed = false;
	std::vector<CatalogExperimentDefinition> entries;
};

// Pure validation shared by startup registration and embedded catalog loading.
void validateCatalogExperiments(const std::vector<CatalogExperimentDefinition> &definitions);
void registerCatalogExperiments(const std::vector<CatalogExperimentDefinition> &definitions);
// Built-ins first in their historical order, followed by installed dynamic keys.
std::vector<CatalogExperimentDefinition> registeredExperimentDefinitions();
bool knownExperimentKey(const std::string &key, const std::vector<std::string> &allowedKeys = {});
std::string experimentLabel(const CatalogExperimentDefinition &definition);
std::string experimentHelp(const CatalogExperimentDefinition &definition);

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
	void set(ExperimentId id, bool on = true);
	bool has(const std::string &key) const;
	// Throws for unknown keys; a validated embedded catalog can supply allowedKeys.
	void set(const std::string &key, bool on = true, const std::vector<std::string> &allowedKeys = {});
	void clear() { bits.reset(); dynamicKeys.clear(); }
	bool empty() const { return bits.none() && dynamicKeys.empty(); }
	std::size_t size() const { return bits.count() + dynamicKeys.size(); }
	bool operator==(const ExperimentSet &other) const { return bits == other.bits && dynamicKeys == other.dynamicKeys; }
	bool operator!=(const ExperimentSet &other) const { return !(*this == other); }

	// Built-in keys in ExperimentId order, then dynamic keys in byte-wise order.
	std::vector<std::string> keys() const;
	// Comma-separated keys, the preferences form; empty when nothing is enabled.
	std::string toText() const;
	// Parse keys (comma-separated, or a list). Unknown keys are ignored; when
	// `unknown` is given they are appended to it so the caller can report them.
	static ExperimentSet fromText(const std::string &text, std::vector<std::string> *unknown = nullptr,
		const std::vector<std::string> &allowedKeys = {});
	static ExperimentSet fromKeys(const std::vector<std::string> &keys, std::vector<std::string> *unknown = nullptr,
		const std::vector<std::string> &allowedKeys = {});

	// Save and load the set as its own stream section. load() clears the set,
	// reads nothing from streams older than FILE_FORMAT_VERSION_EXPERIMENTS,
	// drops unknown keys with one line on stderr each, and returns false only
	// when the section itself is malformed.
	void save(GAGCore::OutputStream *stream) const;
	bool load(GAGCore::InputStream *stream, Sint32 versionMinor, bool rejectUnknown = false,
		const std::vector<std::string> &allowedKeys = {});

private:
	std::bitset<COUNT> bits;
	std::set<std::string> dynamicKeys;
};

// The enabled experiments' translated labels, comma-separated, for the lobby
// footer and the multiplayer options screen.
std::string experimentLabelList(const ExperimentSet &set);
