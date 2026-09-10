// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "CustomGameSetup.h"
#include "LegacyGenerationDescriptor.h"
#include "Settings.h"
#include <FileManager.h>
#include <Stream.h>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <vector>

// Preferences contain a reusable draft, never a generated world or RNG state.
// Decode into a temporary object so a corrupt/truncated file cannot partially
// replace the current setup. Invalid but editable drafts (e.g. all Closed) are
// intentionally preserved; launch validation still belongs to the setup model.
struct CustomGamePreferences
{
	static constexpr const char *filename = "custom-game-settings.txt";
	CustomGameSetup setup;
	bool userMaps = false;
	std::string librarySelection[2];
	bool expanded[3] = {false, false, false};

	struct Field
	{
		const char *name;
		Sint32 MapGenerationDescriptor::*member;
		int minimum, maximum;
	};
	static const std::vector<Field> &fields()
	{
		// Bounds are a safe envelope across every generator method's current
		// registry range for the reused legacy field (e.g. riverDiameter also
		// stands in for lake size, channel width and bridge width; grassRatio
		// also stands in for Isles' island size), widened from the original
		// fixed 0-64 terrain-weight bound now that the registry allows 0-100
		// and Old Islands/Rugged Archipelago's island size reaches 70. This
		// only needs to be at least as wide as what setMethodDefaults() and
		// every registered control can produce; it is not a tight per-method
		// bound, matching how this field list worked before the modular
		// registry existed.
		static const std::vector<Field> values = {
#define FIELD(name, lo, hi) {#name, &MapGenerationDescriptor::name, lo, hi}
			FIELD(wDec, 6, 9), FIELD(hDec, 6, 9),
			FIELD(waterRatio, 0, 100), FIELD(sandRatio, 0, 100),
			FIELD(grassRatio, 0, 100), FIELD(desertRatio, 0, 100),
			FIELD(wheatRatio, 0, 64), FIELD(woodRatio, 0, 64),
			FIELD(fruitRatio, 0, 64), FIELD(algaeRatio, 0, 64),
			FIELD(stoneRatio, 0, 64), FIELD(riverDiameter, 1, 65),
			FIELD(craterDensity, 1, 64), FIELD(extraIslands, 0, 8),
			FIELD(oldIslandSize, 1, 70), FIELD(oldBeach, 0, 4),
			FIELD(smooth, 1, 8), FIELD(nbTeams, 2, Team::MAX_COUNT),
			FIELD(nbWorkers, 1, 8)
#undef FIELD
		};
		return values;
	}
	std::string encode() const
	{
		// The wire format still describes the legacy fixed-field descriptor;
		// the modular GenerationRequest converts through the compatibility
		// adapter so the persisted format and its corruption-recovery bounds
		// stay unchanged regardless of which generator module is selected.
		const auto legacy = toLegacyDescriptor(setup.generator);
		std::ostringstream out;
		out << "glob2-custom-game 1\n"
			<< "setup " << setup.random << ' ' << setup.capacity << ' '
			<< setup.prestige << ' ' << setup.revealed << ' ' << setup.locked << ' '
			<< setup.speed << ' ' << userMaps << '\n'
			<< "labels " << std::quoted(setup.format) << ' ' << std::quoted(setup.ruleset) << '\n'
			<< "map " << std::quoted(setup.premadeMap) << '\n'
			<< "libraries " << std::quoted(librarySelection[0]) << ' '
			<< std::quoted(librarySelection[1]) << '\n'
			<< "sections " << expanded[0] << ' ' << expanded[1] << ' ' << expanded[2] << '\n'
			<< "generator " << int(legacy.method) << ' '
			<< legacy.logRepeatAreaTimes << '\n';
		for (const auto &f : fields())
			out << f.name << ' ' << legacy.*(f.member) << '\n';
		out << "resources";
		for (auto value : legacy.resource) out << ' ' << value;
		out << "\ncolonies\n";
		for (const auto &c : setup.colonies)
			out << int(c.controller) << ' ' << int(c.ai) << ' ' << c.alliance << '\n';
		out << "end\n";
		return out.str();
	}
	bool decode(const std::string &text)
	{
		if (text.size() > 65536) return false;
		CustomGamePreferences draft;
		auto &s = draft.setup;
		std::istringstream in(text);
		auto word = [&](const char *expected) {
			std::string actual;
			return bool(in >> actual) && actual == expected;
		};
		auto number = [&](int &value, int lo, int hi) {
			return bool(in >> value) && value >= lo && value <= hi;
		};
		int version, random, prestige, revealed, locked, user, method, repeat;
		if (!word("glob2-custom-game") || !number(version, 1, 1) || !word("setup") ||
			!number(random, 0, 1) || !number(s.capacity, 1, Team::MAX_COUNT) ||
			!number(prestige, 0, 1) || !number(revealed, 0, 1) || !number(locked, 0, 1) ||
			!number(s.speed, 0, Settings::GAME_SPEED_MAXIMUM) || !number(user, 0, 1) ||
			!word("labels") || !(in >> std::quoted(s.format) >> std::quoted(s.ruleset))) return false;
		if (s.format != "FFA" && s.format != "2 vs 2" && s.format != "You vs all" && s.format != "Custom teams") return false;
		if (s.ruleset != "Standard" && s.ruleset != "Quick clash" && s.ruleset != "Open book" && s.ruleset != "Last colony standing" && s.ruleset != "Custom") return false;
		if (!word("map") || !(in >> std::quoted(s.premadeMap)) || !word("libraries") ||
			!(in >> std::quoted(draft.librarySelection[0]) >> std::quoted(draft.librarySelection[1])) ||
			!word("sections")) return false;
		for (auto &expanded : draft.expanded) {
			int value;
			if (!number(value, 0, 1)) return false;
			expanded = value;
		}
		if (!word("generator") || !number(method, 0, 1000) || !number(repeat, 0, 5)) return false;
		// Method validity is checked against the live registry rather than a
		// hardcoded range, so it stays correct as generators are added or
		// retired. Uniform is editor-only and never a valid lobby selection.
		const auto playable = GeneratorRegistry::builtins().methods(false);
		if (std::find(playable.begin(), playable.end(), method) == playable.end()) return false;
		MapGenerationDescriptor legacy;
		legacy.method = MapGenerationDescriptor::Method(method);
		for (const auto &f : fields()) {
			int value;
			if (!word(f.name) || !number(value, f.minimum, f.maximum)) return false;
			legacy.*(f.member) = value;
		}
		if (!word("resources")) return false;
		for (auto &value : legacy.resource) {
			int parsed;
			if (!number(parsed, 0, 64)) return false;
			value = parsed;
		}
		if (!word("colonies")) return false;
		int humans = 0;
		for (auto &c : s.colonies) {
			int controller, ai;
			if (!number(controller, 0, CustomGameSetup::Closed) || !number(ai, 0, AI::SIZE - 1) ||
				!number(c.alliance, 0, Team::MAX_COUNT - 1)) return false;
			c.controller = CustomGameSetup::Controller(controller);
			c.ai = AI::ImplementationID(ai);
			humans += controller == CustomGameSetup::Human || controller == CustomGameSetup::Shared;
		}
		if (humans > 1 || !word("end")) return false;
		in >> std::ws;
		if (!in.eof()) return false;
		s.random = random; s.prestige = prestige; s.revealed = revealed; s.locked = locked;
		draft.userMaps = user;
		legacy.logRepeatAreaTimes = repeat;
		s.generator = fromLegacyDescriptor(legacy, 0);
		if (s.random) s.capacity = s.generator.nbTeams;
		*this = draft;
		return true;
	}
	bool load(GAGCore::FileManager &files)
	{
		std::unique_ptr<std::ifstream> input(files.openIFStream(filename));
		if (!input || !*input) return false;
		char buffer[65537];
		input->read(buffer, sizeof(buffer));
		return !input->bad() && decode(std::string(buffer, size_t(input->gcount())));
	}
	bool save(GAGCore::FileManager &files) const
	{
		const auto text = encode();
		return files.writeAtomically(filename, [&](GAGCore::OutputStream &out) {
			out.write(text.data(), text.size(), "preferences");
		});
	}
};
