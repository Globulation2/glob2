// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Settings > Experiments persistence: the enabled set survives a preferences
// round trip as comma-separated keys, and a key this build no longer knows is
// dropped rather than failing the load.

#include "EngineFixtures.h"
#include "ExperimentalFeatures.h"
#include "Settings.h"
#include <FileManager.h>
#include <Toolkit.h>
#include <filesystem>
#include <string>

TEST_SUITE("SettingsExperiments")
{
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
