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
			CHECK(current->getPosition() == bytes.size());

			// The set is the last thing written, so a version 123 header is this
			// one without those bytes: it loads exactly, with no experiment.
			const std::string sectionBytes = bytesOf(original.getExperiments());
			REQUIRE(bytes.size() > sectionBytes.size());
			const size_t legacySize = bytes.size() - sectionBytes.size();
			auto* legacy = new MemoryStreamBackend(bytes.data(), legacySize);
			legacy->seekFromStart(0);
			BinaryInputStream old(legacy);
			GameHeader older;
			older.setExperiments(guardOnly());
			const bool read = form == 0 ? older.load(&old, FILE_FORMAT_VERSION_EXPERIMENTS - 1)
				: older.loadWithoutPlayerInfo(&old, FILE_FORMAT_VERSION_EXPERIMENTS - 1);
			GLOB2_REQUIRE(read, "version 123 form loads");
			CHECK(older.getExperiments().empty());
			CHECK(legacy->getPosition() == legacySize);
			if (form == 0)
				CHECK(older.getNumberOfPlayers() == original.getNumberOfPlayers());
		}

		// reset() clears the set like every other option.
		original.reset();
		CHECK(original.getExperiments().empty());
	}
}
