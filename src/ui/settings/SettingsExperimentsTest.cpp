// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Settings > Experiments persistence: the enabled set survives a preferences
// round trip as comma-separated keys, and a key this build no longer knows is
// dropped rather than failing the load.

#include "EngineFixtures.h"
#include "ExperimentalFeatures.h"
#include "Settings.h"
#include <FileManager.h>
#include <StringTable.h>
#include <Toolkit.h>
#include <fstream>
#include <set>
#include <filesystem>
#include <string>

TEST_SUITE("SettingsExperiments")
{
	// The registry's label and help are the English source; the interface reads
	// "[experiment <key>]" and "[experiment <key> help]" from the string table,
	// which data/check_translations.py cannot connect to the registry because the
	// keys are built at run time. This case is that connection: every entry's keys
	// are listed in texts.keys.txt (so --strict then requires all catalogs to
	// translate them) and the English table matches the registry.
	//
	// The settings page's "No experiments in this build." row cannot be reached
	// while the registry has an entry; its key is checked here all the same.
	TEST_CASE("every experiment's label and help are in the string tables")
	{
		glob2test::GlobalsOptions options{.loadStrings = true};
		options.beforeLoad = [](GlobalContainer& globals) { globals.settings.language = "en"; };
		glob2test::CapturedStderr errors;
		glob2test::HeadlessGlobals globals(options);
		auto* strings = GAGCore::Toolkit::getStringTable();
		std::set<std::string> listed;
		{
			std::ifstream keys(glob2test::sourceRoot() / "data/texts.keys.txt");
			for (std::string line; std::getline(keys, line);)
				listed.insert(line);
		}
		ExperimentSet all;
		for (const auto& definition : experimentDefinitions())
		{
			const std::string label = "[experiment " + std::string(definition.key) + "]";
			const std::string help = "[experiment " + std::string(definition.key) + " help]";
			GLOB2_CHECK(listed.count(label), label + " is listed in data/texts.keys.txt");
			GLOB2_CHECK(listed.count(help), help + " is listed in data/texts.keys.txt");
			CHECK(strings->getString(label) == definition.label);
			CHECK(strings->getString(help) == definition.help);
			all.set(definition.id);
		}
		if (!all.empty())
			CHECK(experimentLabelList(all).find(experimentDefinitions().front().label) == 0);
		// The fixed interface strings this feature added.
		for (const char* key : {"[settings Experiments]", "[settings Try features we are still testing. They can change balance and pacing.]",
				 "[settings Experiments apply to new games you start or host, never to campaign missions. A saved game keeps the ones it started with.]",
				 "[settings No experiments in this build.]", "[Experiments]", "[Experiments set by the host]"})
		{
			GLOB2_CHECK(listed.count(key), std::string(key) + " is listed in data/texts.keys.txt");
			strings->getString(key);
		}
		GLOB2_CHECK(errors.text().find("no such key") == std::string::npos, "an experiments string is missing: " + errors.text());
	}

	TEST_CASE("preferences keep the enabled experiments and drop unknown keys")
	{
		glob2test::HeadlessGlobals globals;
		const std::string file = "experiments-test.txt";
		const std::filesystem::path path = std::filesystem::path(GAGCore::Toolkit::getFileManager()->getDir(0)) / file;

		Settings enabled;
		enabled.experiments.set(ExperimentId::GuardAreaBalancing);
		REQUIRE(enabled.save(file));
		CHECK(glob2test::readFile(path).find("experiments=guard-area-balancing\n") != std::string::npos);
		Settings loaded;
		loaded.load(file);
		CHECK(loaded.experiments.has(ExperimentId::GuardAreaBalancing));
		CHECK(loaded.experiments.size() == 1);

		// Nothing enabled writes an empty value, which loads as nothing enabled.
		Settings none;
		REQUIRE(none.save(file));
		CHECK(glob2test::readFile(path).find("experiments=\n") != std::string::npos);
		loaded.load(file);
		CHECK(loaded.experiments.empty());

		// A preferences file from a build with an experiment this one retired.
		glob2test::writeFile(path, "experiments=retired-thing,guard-area-balancing\nrememberUnit=1\n");
		Settings mixed;
		mixed.load(file);
		CHECK(mixed.experiments.has(ExperimentId::GuardAreaBalancing));
		CHECK(mixed.experiments.size() == 1);
		CHECK(mixed.rememberUnit == 1);

		// A file that predates the setting leaves the default: nothing enabled.
		glob2test::writeFile(path, "rememberUnit=1\n");
		Settings older;
		older.load(file);
		CHECK(older.experiments.empty());
		std::filesystem::remove(path);
	}
}
