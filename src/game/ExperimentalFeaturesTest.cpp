// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// The experiments registry and the set a game carries: stable keys, the
// preferences text form, stream round trips, unknown keys dropped, and the
// GameHeader forms that carry the set (with a pre-124 header reading none).

#include "Glob2Test.h"
#include "ExperimentalFeatures.h"
#include "FileFormatVersions.h"
#include "GameHeader.h"
#include "Version.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <TextStream.h>
#include <cctype>
#include <set>
#include <string>
#include <stdexcept>

using namespace GAGCore;

namespace
{
	std::string bytesOf(const ExperimentSet& set)
	{
		auto* memory = new MemoryStreamBackend;
		BinaryOutputStream out(memory);
		set.save(&out);
		out.flush();
		return std::string(memory->getBuffer(), memory->getPosition());
	}

	// Loads `bytes` as a set written at `versionMinor`; reports the bytes consumed.
	bool loadBytes(const std::string& bytes, Sint32 versionMinor, ExperimentSet& set, size_t* consumed = nullptr)
	{
		auto* memory = new MemoryStreamBackend(bytes.data(), bytes.size());
		memory->seekFromStart(0);
		BinaryInputStream in(memory);
		const bool ok = set.load(&in, versionMinor);
		if (consumed)
			*consumed = memory->getPosition();
		return ok;
	}

	ExperimentSet guardOnly()
	{
		ExperimentSet set;
		set.set(ExperimentId::GuardAreaBalancing);
		return set;
	}
}

TEST_SUITE("ExperimentalFeatures")
{
	TEST_CASE("catalog definitions are validated sorted and immutable without allocating enum ids")
	{
		CatalogExperimentRegistry registry;
		const CatalogExperimentDefinition alpha{"alpha-building", "Alpha building", "Enables alpha."};
		const CatalogExperimentDefinition zeta{"zeta-building", "Zeta building", "Enables zeta."};
		registry.install({zeta, alpha, {"markets-v2", "Catalog markets", "Uses the existing gate."}});
		CHECK(registry.definitions() == std::vector<CatalogExperimentDefinition>{alpha, zeta});
		CHECK_NOTHROW(registry.install({alpha, zeta}));
		CHECK_THROWS_AS(registry.install({alpha}), std::logic_error);
		CHECK(registry.definitions() == std::vector<CatalogExperimentDefinition>{alpha, zeta});
		CHECK(!parseExperimentKey(alpha.key));

		CatalogExperimentRegistry invalid;
		CHECK_THROWS_AS(invalid.install({alpha, alpha}), std::invalid_argument);
		for (const std::string &key : std::vector<std::string>{"", "Upper-case", "contains space", "-leading", "trailing-", "two--hyphens", std::string(129, 'a')})
			CHECK_THROWS_AS(invalid.install({{key, "Label", "Help"}}), std::invalid_argument);
		CHECK_THROWS_AS(invalid.install({{"empty-label", "", "Help"}}), std::invalid_argument);
		CHECK_THROWS_AS(invalid.install({{"empty-help", "Label", ""}}), std::invalid_argument);
		// Failed validation never partially installs or freezes the registry.
		CHECK_NOTHROW(invalid.install({alpha}));
		CHECK(invalid.definitions() == std::vector<CatalogExperimentDefinition>{alpha});
	}

	TEST_CASE("embedded catalog keys survive independently of installed definitions")
	{
		const std::vector<std::string> catalogKeys{"zeta-building", "alpha-building"};
		CHECK(!knownExperimentKey("alpha-building"));
		CHECK(knownExperimentKey("alpha-building", catalogKeys));
		CHECK(!knownExperimentKey("bad key", {"bad key"}));
		std::vector<std::string> unknown;
		auto enabled = ExperimentSet::fromKeys({"zeta-building", "farm-areas", "alpha-building", "zeta-building", "unknown-building"}, &unknown, catalogKeys);
		CHECK(unknown == std::vector<std::string>{"unknown-building"});
		CHECK(enabled.keys() == std::vector<std::string>{"farm-areas", "alpha-building", "zeta-building"});
		CHECK(enabled.size() == 3);
		CHECK(enabled.has(ExperimentId::FarmAreas));
		CHECK(enabled.has("farm-areas"));
		CHECK(enabled.has("alpha-building"));
		CHECK(ExperimentSet::fromText(enabled.toText(), nullptr, catalogKeys) == enabled);
		CHECK_THROWS_AS(enabled.set("unknown-building"), std::invalid_argument);
		enabled.set("alpha-building", false);
		CHECK(!enabled.has("alpha-building"));
		enabled.set("alpha-building", true, catalogKeys);
		enabled.set("farm-areas", false);
		CHECK(!enabled.has(ExperimentId::FarmAreas));

		const std::string bytes = bytesOf(enabled);
		for (const bool allowCatalog : {false, true})
		{
			auto *memory = new MemoryStreamBackend(bytes.data(), bytes.size());
			memory->seekFromStart(0);
			BinaryInputStream input(memory);
			ExperimentSet restored;
			CHECK(restored.load(&input, VERSION_MINOR, true, allowCatalog ? catalogKeys : std::vector<std::string>{}) == allowCatalog);
			if (allowCatalog)
			{
				CHECK(restored == enabled);
				CHECK(memory->getPosition() == bytes.size());
			}
		}
		// Loading one game never makes its catalog valid for another game.
		CHECK(!knownExperimentKey("alpha-building"));
		enabled.clear();
		CHECK(enabled.empty());
		CHECK(enabled.size() == 0);
	}

	TEST_CASE("dynamic experiment text streams round trip and sets respect the stored count bound")
	{
		std::vector<std::string> catalogKeys;
		for (unsigned i = 0; i <= ExperimentSet::MAX_STORED; ++i)
			catalogKeys.push_back("building-" + std::to_string(i));
		ExperimentSet enabled;
		for (unsigned i = 0; i < ExperimentSet::MAX_STORED; ++i)
			enabled.set(catalogKeys[i], true, catalogKeys);
		CHECK_THROWS_AS(enabled.set(catalogKeys.back(), true, catalogKeys), std::length_error);
		CHECK_THROWS_AS(enabled.set(ExperimentId::FarmAreas), std::length_error);
		CHECK(enabled.size() == ExperimentSet::MAX_STORED);
		MemoryStreamBackend copy;
		{
			auto *memory = new MemoryStreamBackend;
			TextOutputStream output(memory);
			enabled.save(&output);
			output.flush();
			copy = *memory;
		}
		copy.seekFromStart(0);
		TextInputStream input(&copy);
		ExperimentSet restored;
		REQUIRE(restored.load(&input, VERSION_MINOR, true, catalogKeys));
		CHECK(restored == enabled);
	}

	TEST_CASE("registry keys are unique kebab-case and parse back to their id")
	{
		const auto& definitions = experimentDefinitions();
		REQUIRE(definitions.size() == ExperimentSet::COUNT);
		std::set<std::string> keys;
		for (size_t i = 0; i < definitions.size(); ++i)
		{
			const auto& definition = definitions[i];
			CHECK(static_cast<size_t>(definition.id) == i);
			const std::string key = definition.key;
			CHECK(!key.empty());
			for (char c : key)
				GLOB2_CHECK(std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '-',
					"key is kebab-case: " + key);
			CHECK(keys.insert(key).second);
			CHECK(parseExperimentKey(key) == definition.id);
			CHECK(std::string(definition.label).size() > 0);
			CHECK(std::string(definition.help).size() > 0);
			CHECK(&experimentDefinition(definition.id) == &definition);
		}
		CHECK(!parseExperimentKey("").has_value());
		CHECK(!parseExperimentKey("Guard-Area-Balancing").has_value());
	}

	TEST_CASE("text form round trips and ignores keys this build does not know")
	{
		ExperimentSet empty;
		CHECK(empty.empty());
		CHECK(empty.toText().empty());
		CHECK(ExperimentSet::fromText("") == empty);

		const ExperimentSet guard = guardOnly();
		CHECK(guard.toText() == "guard-area-balancing");
		CHECK(ExperimentSet::fromText(guard.toText()) == guard);
		CHECK(guard.keys() == std::vector<std::string>{"guard-area-balancing"});

		std::vector<std::string> unknown;
		const auto mixed = ExperimentSet::fromText(" retired-thing , guard-area-balancing,, ", &unknown);
		CHECK(mixed == guard);
		CHECK(unknown == std::vector<std::string>{"retired-thing"});
		CHECK(ExperimentSet::fromText("retired-thing").empty());

		ExperimentSet toggled = guard;
		toggled.set(ExperimentId::GuardAreaBalancing, false);
		CHECK(toggled.empty());
		CHECK(toggled != guard);
	}

	TEST_CASE("stream round trip; a pre-124 stream reads nothing; corrupt input is refused")
	{
		const ExperimentSet guard = guardOnly();
		const std::string bytes = bytesOf(guard);
		ExperimentSet loaded;
		size_t consumed = 0;
		REQUIRE(loadBytes(bytes, VERSION_MINOR, loaded, &consumed));
		CHECK(loaded == guard);
		CHECK(consumed == bytes.size());

		// Before the gate the section does not exist: nothing is read and a set
		// that held something is cleared.
		loaded = guard;
		REQUIRE(loadBytes(bytes, FILE_FORMAT_VERSION_EXPERIMENTS - 1, loaded, &consumed));
		CHECK(loaded.empty());
		CHECK(consumed == 0);

		// Unknown keys are dropped with a note on stderr; the rest of the set survives.
		{
			auto* memory = new MemoryStreamBackend;
			BinaryOutputStream out(memory);
			out.writeEnterSection("experiments");
			out.writeUint32(2, "count");
			out.writeEnterSection(0u);
			out.writeText("retired-thing", "key");
			out.writeLeaveSection();
			out.writeEnterSection(1u);
			out.writeText("guard-area-balancing", "key");
			out.writeLeaveSection();
			out.writeLeaveSection();
			out.flush();
			const std::string mixed(memory->getBuffer(), memory->getPosition());
			glob2test::CapturedStderr stderrText;
			REQUIRE(loadBytes(mixed, VERSION_MINOR, loaded, &consumed));
			CHECK(loaded == guard);
			CHECK(consumed == mixed.size());
			CHECK(stderrText.text().find("retired-thing") != std::string::npos);
		}

		// A count beyond MAX_STORED is corruption, not a long list.
		{
			auto* memory = new MemoryStreamBackend;
			BinaryOutputStream out(memory);
			out.writeEnterSection("experiments");
			out.writeUint32(ExperimentSet::MAX_STORED + 1, "count");
			out.writeLeaveSection();
			out.flush();
			const std::string corrupt(memory->getBuffer(), memory->getPosition());
			CHECK(!loadBytes(corrupt, VERSION_MINOR, loaded));
		}
	}

	TEST_CASE("text streams keep the set")
	{
		const ExperimentSet guard = guardOnly();
		MemoryStreamBackend copy;
		{
			auto* owned = new MemoryStreamBackend;
			TextOutputStream out(owned);
			guard.save(&out);
			out.flush();
			copy = *owned;
		}
		copy.seekFromStart(0);
		TextInputStream in(&copy);
		ExperimentSet loaded;
		REQUIRE(loaded.load(&in, VERSION_MINOR));
		CHECK(loaded == guard);
	}

	TEST_CASE("GameHeader carries the set in its full and player-less forms and a version 123 header reads none")
	{
		GameHeader original;
		original.setNumberOfPlayers(2);
		original.getBasePlayer(0) = BasePlayer(0, "one", 0, BasePlayer::P_LOCAL);
		original.getBasePlayer(1) = BasePlayer(1, "two", 1, BasePlayer::P_AI);
		original.setExperiments(guardOnly());
        original.setAIOrderDelay(8);
		REQUIRE(original.hasExperiment(ExperimentId::GuardAreaBalancing));

		for (int form = 0; form < 2; ++form)
		{
			auto* memory = new MemoryStreamBackend;
			BinaryOutputStream out(memory);
			if (form == 0) original.save(&out); else original.saveWithoutPlayerInfo(&out);
			out.flush();
			const std::string bytes(memory->getBuffer(), memory->getPosition());

			auto* current = new MemoryStreamBackend(bytes.data(), bytes.size());
			current->seekFromStart(0);
			BinaryInputStream in(current);
			GameHeader loaded;
			const bool ok = form == 0 ? loaded.load(&in, VERSION_MINOR) : loaded.loadWithoutPlayerInfo(&in, VERSION_MINOR);
			GLOB2_REQUIRE(ok, "form loads");
			CHECK(loaded.getExperiments() == original.getExperiments());
            CHECK(loaded.getAIOrderDelay()==8);
			CHECK(current->getPosition() == bytes.size());

            // Build the older wire layout explicitly: version 123 has neither
            // experiment/catalog tails nor the format-140 delay byte following
            // the common gameLatency/orderRate prefix.
            const std::string sectionBytes = bytesOf(original.getExperiments());
            REQUIRE(bytes.size() > sectionBytes.size()+sizeof(Uint32)+sizeof(Uint8));
            std::string legacyBytes=bytes.substr(0,bytes.size()-sectionBytes.size()-sizeof(Uint32));
            legacyBytes.erase(sizeof(Sint32)+sizeof(Uint8),sizeof(Uint8));
            const size_t legacySize=legacyBytes.size();
            auto* legacy = new MemoryStreamBackend(legacyBytes.data(),legacySize);
			legacy->seekFromStart(0);
			BinaryInputStream old(legacy);
			GameHeader older;
			older.setExperiments(guardOnly());
			const bool read = form == 0 ? older.load(&old, FILE_FORMAT_VERSION_EXPERIMENTS - 1)
				: older.loadWithoutPlayerInfo(&old, FILE_FORMAT_VERSION_EXPERIMENTS - 1);
			GLOB2_REQUIRE(read, "version 123 form loads");
			CHECK(older.getExperiments().empty());
            CHECK(older.getAIOrderDelay()==0);
			CHECK(legacy->getPosition() == legacySize);
			if (form == 0)
				CHECK(older.getNumberOfPlayers() == original.getNumberOfPlayers());
		}

		// reset() clears the set like every other option.
		original.reset();
		CHECK(original.getExperiments().empty());
	}
}
